// main/main.cpp

#include "esp_log.h"
#include "esp_err.h"
#include "esp_system.h"
#include "esp_task_wdt.h"
#include "esp_timer.h"
#include "bsp/esp-bsp.h"
#include "bsp/display.h"
#include "driver/gpio.h"
#include "driver/temperature_sensor.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "esp_private/panic_internal.h"
#include "esp_attr.h"
#include "riscv/rvruntime-frames.h"

#include "network.h"
#include "time_sync.h"
#include "secrets.h"
#include "settings.h"
#include "ota.h"
#include "sdcard.h"
#include "http_api.h"
#include "debug_log.h"
#include "display_fsm.h"
#include "nws.h"
#include "mdns.h"
#include "lvgl.h"
#include "libs/fsdrv/lv_fsdrv.h"

#include <string.h>

static const char* TAG = "main";

// =============================================================================
// RTC memory — crash context that survives hardware WDT resets
// Written in panic handler (zero file I/O, safe in any context).
// Read and logged on the next boot, after the SD log is open.
// Cleared on power-on reset; preserved through WDT / software resets.
//
// IMPORTANT: Must use RTC_NOINIT_ATTR (not RTC_DATA_ATTR).
// RTC_DATA_ATTR zero-initialized variables land in .rtc.bss, which
// cpu_start.c clears on every non-deep-sleep reset — including PANIC.
// RTC_NOINIT_ATTR places variables in .rtc.noinit, which is never zeroed
// by startup code, so values survive through PANIC / WDT resets.
// =============================================================================

#define CRASH_MAGIC 0xDEADBEEF

static RTC_NOINIT_ATTR uint32_t s_crash_magic;
static RTC_NOINIT_ATTR uint32_t s_crash_pc;        // mepc at fault
static RTC_NOINIT_ATTR uint32_t s_crash_ra;        // return address at fault
static RTC_NOINIT_ATTR uint32_t s_crash_sp;        // stack pointer at fault
static RTC_NOINIT_ATTR char     s_crash_reason[64];
static RTC_NOINIT_ATTR char     s_crash_details[128]; // abort() details (file:line, assert expr)
static RTC_NOINIT_ATTR char     s_boot_stage[32];  // last stage reached before crash
static RTC_NOINIT_ATTR bool     s_panic_restart;   // skip SD I/O in shutdown handler

static inline void set_boot_stage(const char *stage)
{
    strncpy(s_boot_stage, stage, sizeof(s_boot_stage) - 1);
    s_boot_stage[sizeof(s_boot_stage) - 1] = '\0';
}

// =============================================================================
// LVGL heartbeat watchdog
// heartbeat_cb runs inside the LVGL task every 1 s.  If the task hangs (blue
// screen / render deadlock), it stops updating s_lvgl_heartbeat_ms.
// display_watchdog_task checks from outside and forces a restart after 15 s
// of silence — recovering the device without requiring a power cycle.
// =============================================================================

#define LVGL_WATCHDOG_TIMEOUT_MS  15000
#define LVGL_WATCHDOG_CHECK_MS     5000

static volatile uint32_t s_lvgl_heartbeat_ms = 0;

// =============================================================================
// Internal chip-temperature sensor — sampled in boot_health_loop
// =============================================================================
static temperature_sensor_handle_t s_tsens_handle = NULL;
static float s_tsens_min_c =  1000.0f;  // sentinel: any real read updates
static float s_tsens_max_c = -1000.0f;
static float s_tsens_last_c = 0.0f;

static void tsens_init(void)
{
    temperature_sensor_config_t cfg = TEMPERATURE_SENSOR_CONFIG_DEFAULT(-10, 90);
    if (temperature_sensor_install(&cfg, &s_tsens_handle) != ESP_OK) {
        ESP_LOGW(TAG, "[BOOT] tsens install failed — chip-temp telemetry disabled");
        s_tsens_handle = NULL;
        return;
    }
    if (temperature_sensor_enable(s_tsens_handle) != ESP_OK) {
        ESP_LOGW(TAG, "[BOOT] tsens enable failed");
        temperature_sensor_uninstall(s_tsens_handle);
        s_tsens_handle = NULL;
        return;
    }
    ESP_LOGI(TAG, "[BOOT] tsens installed (range -10..90 °C)");
}

// Read once and update min/max trackers. Returns true on success.
static bool tsens_sample(float *out_c)
{
    if (!s_tsens_handle) return false;
    float c = 0.0f;
    if (temperature_sensor_get_celsius(s_tsens_handle, &c) != ESP_OK) return false;
    s_tsens_last_c = c;
    if (c < s_tsens_min_c) s_tsens_min_c = c;
    if (c > s_tsens_max_c) s_tsens_max_c = c;
    if (out_c) *out_c = c;
    return true;
}

// Public accessors for /api/status (extern "C" linkage matches http_api caller).
extern "C" bool main_tsens_get_last(float *out_last, float *out_min, float *out_max)
{
    if (!s_tsens_handle) return false;
    if (out_last) *out_last = s_tsens_last_c;
    if (out_min)  *out_min  = s_tsens_min_c;
    if (out_max)  *out_max  = s_tsens_max_c;
    return true;
}

// =============================================================================
// Boot history — circular buffer of last 16 boots persisted to NVS.
// Lets us see reset_reason recurrence across power cycles without needing
// to be watching live logs. Each entry is {reset_reason u8, timestamp u32}.
// =============================================================================
#define BOOT_HIST_NAMESPACE "boot_hist"
#define BOOT_HIST_ENTRIES   16
#define BOOT_HIST_KEY_HEAD  "head"

struct boot_hist_entry_t {
    uint8_t  reset_reason;   // esp_reset_reason_t cast to u8
    uint8_t  _pad[3];
    uint32_t timestamp;      // unix time at entry write (0 if SNTP not synced)
};
static_assert(sizeof(boot_hist_entry_t) == 8, "boot_hist_entry_t must be 8 bytes");

static void boot_history_record(void)
{
    nvs_handle_t h;
    if (nvs_open(BOOT_HIST_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) {
        ESP_LOGW(TAG, "[BOOT] boot_history: nvs_open failed — skipping record");
        return;
    }

    uint8_t head = 0;
    nvs_get_u8(h, BOOT_HIST_KEY_HEAD, &head);  // ok if missing — defaults 0

    boot_hist_entry_t entry = {};
    entry.reset_reason = (uint8_t)esp_reset_reason();
    time_t now = 0;
    time(&now);
    entry.timestamp = (uint32_t)now;  // 0 if SNTP not yet synced

    char key[8];
    snprintf(key, sizeof(key), "h%u", (unsigned)head);
    if (nvs_set_blob(h, key, &entry, sizeof(entry)) == ESP_OK) {
        head = (uint8_t)((head + 1) % BOOT_HIST_ENTRIES);
        nvs_set_u8(h, BOOT_HIST_KEY_HEAD, head);
        nvs_commit(h);
        ESP_LOGI(TAG, "[BOOT] boot_history: recorded reason=%u ts=%lu (head→%u)",
                 entry.reset_reason, (unsigned long)entry.timestamp, head);
    } else {
        ESP_LOGW(TAG, "[BOOT] boot_history: set_blob failed");
    }
    nvs_close(h);
}

// Public read API used by /api/boot_history. Returns oldest-first chronological
// order in @p out_entries (caller-provided buffer of capacity @p capacity).
// @p out_count is set to the number of valid entries written. Entries whose
// blob is missing in NVS are skipped (sparse buffer after fresh erase).
extern "C" size_t main_boot_history_get(struct boot_hist_entry_t *out_entries, size_t capacity)
{
    if (!out_entries || capacity == 0) return 0;
    nvs_handle_t h;
    if (nvs_open(BOOT_HIST_NAMESPACE, NVS_READONLY, &h) != ESP_OK) return 0;

    uint8_t head = 0;
    nvs_get_u8(h, BOOT_HIST_KEY_HEAD, &head);

    size_t written = 0;
    // Walk oldest-first: starting from head (next-write slot) wrapping forward
    for (size_t i = 0; i < BOOT_HIST_ENTRIES && written < capacity; i++) {
        uint8_t idx = (uint8_t)((head + i) % BOOT_HIST_ENTRIES);
        char key[8];
        snprintf(key, sizeof(key), "h%u", (unsigned)idx);
        size_t sz = sizeof(boot_hist_entry_t);
        if (nvs_get_blob(h, key, &out_entries[written], &sz) == ESP_OK
            && sz == sizeof(boot_hist_entry_t)) {
            written++;
        }
    }
    nvs_close(h);
    return written;
}

static void heartbeat_cb(lv_timer_t *timer)
{
    (void)timer;
    s_lvgl_heartbeat_ms = lv_tick_get();
}

// Log a self-heartbeat every Nth check cycle so we know the watchdog itself
// is alive. At 5s check interval, every 12 cycles = once per minute.
#define LVGL_WATCHDOG_SELF_LOG_EVERY  12

static void display_watchdog_task(void *arg)
{
    (void)arg;
    // Grace period: give LVGL time to start before watching
    vTaskDelay(pdMS_TO_TICKS(LVGL_WATCHDOG_TIMEOUT_MS));

    uint32_t cycle = 0;
    UBaseType_t initial_hw_words = uxTaskGetStackHighWaterMark(NULL);
    ESP_LOGI(TAG, "[wdog] alive (initial stack_hw=%u words = %u bytes)",
             (unsigned)initial_hw_words,
             (unsigned)(initial_hw_words * sizeof(StackType_t)));

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(LVGL_WATCHDOG_CHECK_MS));
        cycle++;
        uint32_t elapsed = lv_tick_elaps(s_lvgl_heartbeat_ms);

        // Periodic self-heartbeat — proves the watchdog task is still scheduled
        // and reports its own stack high-water for early overflow detection.
        if ((cycle % LVGL_WATCHDOG_SELF_LOG_EVERY) == 0) {
            UBaseType_t hw_words = uxTaskGetStackHighWaterMark(NULL);
            ESP_LOGI(TAG, "[wdog] tick cycle=%lu lvgl_elapsed=%lu ms stack_hw=%u bytes",
                     (unsigned long)cycle, (unsigned long)elapsed,
                     (unsigned)(hw_words * sizeof(StackType_t)));
        }

        if (elapsed > LVGL_WATCHDOG_TIMEOUT_MS) {
            ESP_LOGE(TAG, "===== DISPLAY HANG DETECTED ===== (no LVGL heartbeat for %lu ms)",
                     (unsigned long)elapsed);

            // Schedule unconditional restart via esp_timer (1s from now).
            // This guarantees restart even if debug_log_flush() blocks on
            // SD card I/O (which uses SDMMC DMA — fails when DRAM exhausted).
            esp_timer_handle_t restart_timer;
            esp_timer_create_args_t timer_args = {
                .callback = [](void *) { esp_restart(); },
                .name = "wdog_restart"
            };
            if (esp_timer_create(&timer_args, &restart_timer) == ESP_OK) {
                esp_timer_start_once(restart_timer, 1000000); // 1 second
            }

            // Best-effort flush — may block if SD I/O is broken, but the
            // timer above will force restart regardless.
            debug_log_flush();
            esp_restart();
        }
    }
}

// LVGL log callback — routes LVGL ERROR/WARN/INFO through ESP_LOG so they
// reach the SD card debug log and UDP stream.
static void lvgl_log_cb(lv_log_level_t level, const char* buf)
{
    switch (level) {
        case LV_LOG_LEVEL_ERROR: ESP_LOGE("lvgl", "%s", buf); break;
        case LV_LOG_LEVEL_WARN:  ESP_LOGW("lvgl", "%s", buf); break;
        default:                 ESP_LOGI("lvgl", "%s", buf); break;
    }
}

// LVGL assert handler - reset instead of hanging
extern "C" void lv_assert_handler(void) {
    set_boot_stage("lvgl_assert");
    s_crash_magic = CRASH_MAGIC;
    strncpy(s_crash_reason, "LVGL assert", sizeof(s_crash_reason) - 1);
    debug_log_reopen();
    ESP_LOGE(TAG, "========== LVGL ASSERT FAILED ==========");
    ESP_LOGE(TAG, "Heap free: %lu bytes", (unsigned long)esp_get_free_heap_size());
    ESP_LOGE(TAG, "LVGL assertion triggered, resetting device...");
    debug_log_flush();  // commit log before restart so the assert is visible

    // Do NOT call vTaskDelay() here - it can cause crashes from interrupt/invalid context
    // Instead, use a direct restart with minimal delay
    for (int i = 0; i < 100000000; i++) {
        asm volatile("" : : : "memory");  // prevent optimization, flush to console
    }

    esp_restart();
}

// Panic handler wrapper — intercepts esp_panic_handler via --wrap linker flag.
//
// CRITICAL: Do NOT attempt SD file I/O here. Panic context (especially INT WDT)
// runs at high interrupt priority. The SD bus mutex is almost certainly held by
// whichever task was interrupted, so any fflush/fsync call will deadlock, the
// RTC watchdog fires as backstop, and no crash info reaches the log at all.
//
// Instead: write crash context to RTC memory (plain struct assignments, zero I/O).
// RTC memory survives WDT and software resets. boot_sdcard_init() reads and logs
// it on the next boot after the SD log is open and all mutexes are clean.
//
// The real handler prints the full register dump + backtrace to serial (UART, safe
// in any context), then calls esp_restart().
extern "C" void __wrap_esp_panic_handler(panic_info_t *info)
{
    // --- RTC memory write: zero file I/O, safe in interrupt context ---
    s_panic_restart = true;
    s_crash_magic   = CRASH_MAGIC;

    if (info->frame) {
        const RvExcFrame *f = (const RvExcFrame *)info->frame;
        s_crash_pc = f->mepc;
        s_crash_ra = f->ra;
        s_crash_sp = f->sp;
    } else {
        s_crash_pc = (uint32_t)(uintptr_t)info->addr;
        s_crash_ra = 0;
        s_crash_sp = 0;
    }
    strncpy(s_crash_reason,
            info->reason ? info->reason : "unknown",
            sizeof(s_crash_reason) - 1);
    s_crash_reason[sizeof(s_crash_reason) - 1] = '\0';

    // Capture abort() details — g_panic_abort_details contains the message
    // from ESP_ERROR_CHECK, assert(), or abort() (file:line, expression, etc.)
    // This is set by panic_abort() BEFORE the unimp trap that brings us here.
    extern char *g_panic_abort_details;
    extern bool  g_panic_abort;
    if (g_panic_abort && g_panic_abort_details) {
        strncpy(s_crash_details, g_panic_abort_details,
                sizeof(s_crash_details) - 1);
        s_crash_details[sizeof(s_crash_details) - 1] = '\0';
    } else {
        s_crash_details[0] = '\0';
    }
    // s_boot_stage already set by the most-recent set_boot_stage() call

    // Real handler: serial register dump + backtrace + esp_restart()
    void __real_esp_panic_handler(panic_info_t *);
    __real_esp_panic_handler(info);
}

// Stack overflow hook — called by FreeRTOS when a task overflows its stack.
// Runs in an undefined/corrupt context so we keep it minimal: log the task
// name via ESP_LOGE (which goes through our vprintf hook to SD), flush, restart.
// General panics still output backtrace via esp_rom_printf (serial only).
extern "C" void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    (void)xTask;
    set_boot_stage("stack_overflow");
    s_crash_magic = CRASH_MAGIC;
    snprintf(s_crash_reason, sizeof(s_crash_reason), "stack overflow: %s",
             pcTaskName ? pcTaskName : "?");
    debug_log_reopen();
    ESP_LOGE(TAG, "===== STACK OVERFLOW: task='%s' =====", pcTaskName ? pcTaskName : "?");
    debug_log_flush();
    esp_restart();
}

// Shutdown handler — fires for ALL esp_restart() calls (OTA, assert, panic).
// Actual panic detection uses esp_reset_reason() at boot_sdcard_init() on the next boot.
static void panic_handler_hook(void) {
    // After a panic, the SD bus mutex may be held by the interrupted task.
    // Attempting file I/O here deadlocks and prevents the restart from completing.
    // Crash context is already in RTC memory — skip SD I/O for panic restarts.
    if (s_panic_restart) {
        s_panic_restart = false;
        return;
    }
    debug_log_flush();
    ESP_LOGE(TAG, "========== SHUTDOWN HANDLER ==========");
    ESP_LOGE(TAG, "Heap free: %lu bytes", (unsigned long)esp_get_free_heap_size());
    ESP_LOGE(TAG, "Min heap ever: %lu bytes", (unsigned long)esp_get_minimum_free_heap_size());

    char task_list[1024];
    vTaskList(task_list);
    ESP_LOGE(TAG, "Task list:\n%s", task_list);

    ESP_LOGE(TAG, "====================================");
}

// =============================================================================
// Boot stage functions
// =============================================================================

static void boot_settings_init(clock_settings_t *cfg)
{
    set_boot_stage("settings_init");
    // Assert display panel RESET immediately — holds EK79007 in reset through
    // the entire boot sequence so it never enters a clock-starved error state
    // after esp_restart().  BSP releases it properly via esp_lcd_panel_reset()
    // in boot_display_init().  Without this, GPIO27 floats for ~5 s after a
    // SW reset, causing intermittent blue screens on the first post-OTA boot.
    gpio_set_direction(BSP_LCD_RST, GPIO_MODE_OUTPUT);
    gpio_set_level(BSP_LCD_RST, 0);

    // Register panic handler early so any crash before boot_sdcard_init
    // still flushes the debug log.
    esp_register_shutdown_handler(panic_handler_hook);
    ESP_LOGI(TAG, "[BOOT] Panic handler registered");

    esp_err_t err = settings_init();
    ESP_LOGI(TAG, "[BOOT] settings_init: %s", err == ESP_OK ? "OK" : esp_err_to_name(err));

    err = settings_load(cfg);
    ESP_LOGI(TAG, "[BOOT] settings_load: %s", err == ESP_OK ? "OK" : esp_err_to_name(err));
}


// Crash context captured at the very top of app_main(), before any
// set_boot_stage() call can clobber s_boot_stage.
typedef struct {
    uint32_t magic;
    uint32_t pc;
    uint32_t ra;
    uint32_t sp;
    char     reason[64];
    char     stage[32];
    char     details[128];  // abort() message: file:line, assert expr, ESP_ERROR_CHECK msg
} crash_ctx_t;

static void boot_sdcard_init(const crash_ctx_t *ctx)
{
    // ctx was snapshotted at the top of app_main() before boot_settings_init()
    // had a chance to overwrite s_boot_stage — so ctx->stage is the real crash stage.
    set_boot_stage("sdcard_init");
    // Annotate this log session with the reset reason.  Called before SD init
    // so the reason reaches serial even if SD is absent.
    esp_reset_reason_t reset_reason = esp_reset_reason();
    const char* reset_name = "UNKNOWN";
    bool is_abnormal_reset = false;
    switch (reset_reason) {
        case ESP_RST_PANIC:    reset_name = "PANIC";    is_abnormal_reset = true; break;
        case ESP_RST_INT_WDT:  reset_name = "INT_WDT";  is_abnormal_reset = true; break;
        case ESP_RST_TASK_WDT: reset_name = "TASK_WDT"; is_abnormal_reset = true; break;
        case ESP_RST_WDT:      reset_name = "WDT";      is_abnormal_reset = true; break;
        case ESP_RST_BROWNOUT: reset_name = "BROWNOUT"; is_abnormal_reset = true; break;
        case ESP_RST_SW:       reset_name = "SW_RESET";  break;
        case ESP_RST_POWERON:  reset_name = "POWER_ON";  break;
        default: break;
    }
    ESP_LOGI(TAG, "[BOOT] Reset reason: %s (%d)", reset_name, (int)reset_reason);

    sdcard_init();
    esp_err_t err = sdcard_mount(false);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "[BOOT] SD card not available: %s", esp_err_to_name(err));
        if (err == ESP_FAIL) {
            ESP_LOGW(TAG, "[BOOT] SD card may need formatting");
        }
        ESP_LOGW(TAG, "[BOOT] Continuing without SD card support");
        return;
    }

    sdcard_info_t info;
    if (sdcard_get_info(&info) == ESP_OK) {
        ESP_LOGI(TAG, "[BOOT] SD: %s, %.2f GB total, %.2f GB used (%.0f%%)",
                 info.card_type,
                 info.total_bytes / (1024.0 * 1024.0 * 1024.0),
                 info.used_bytes  / (1024.0 * 1024.0 * 1024.0),
                 (info.used_bytes * 100.0) / info.total_bytes);
    }

    sdcard_create_directories();

    err = debug_log_init();
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "[BOOT] Debug log: %s", debug_log_get_path());
        ESP_LOGI(TAG, "[BOOT] Reset reason (SD): %s (%d)", reset_name, (int)reset_reason);
        if (is_abnormal_reset) {
            ESP_LOGE(TAG, "===== ABNORMAL RESET: reason=%s ===== (see previous log slot)",
                     reset_name);
        }

        // Dump RTC crash context from previous boot (survives hardware WDT resets).
        // Panic handler writes to RTC memory only (no SD I/O) — this is where it lands.
        // Values were snapshotted at function entry before set_boot_stage() clobbered them.
        // Always log the raw magic so we can confirm the mechanism is working.
        ESP_LOGI(TAG, "[BOOT] RTC crash magic: 0x%08lx (%s)",
                 (unsigned long)ctx->magic,
                 ctx->magic == CRASH_MAGIC ? "VALID — crash context follows" : "clean");
        if (ctx->magic == CRASH_MAGIC) {
            ESP_LOGE(TAG, "===== PREVIOUS CRASH (RTC memory) =====");
            ESP_LOGE(TAG, "  Stage : %s",
                     ctx->stage[0] ? ctx->stage : "(unknown)");
            ESP_LOGE(TAG, "  Reason: %s",
                     ctx->reason[0] ? ctx->reason : "(hardware WDT — panic handler did not run)");
            ESP_LOGE(TAG, "  PC=0x%08lx  RA=0x%08lx  SP=0x%08lx",
                     (unsigned long)ctx->pc,
                     (unsigned long)ctx->ra,
                     (unsigned long)ctx->sp);
            if (ctx->details[0]) {
                ESP_LOGE(TAG, "  Details: %s", ctx->details);
            }
            ESP_LOGE(TAG, "========================================");
        }
    } else {
        ESP_LOGW(TAG, "[BOOT] Debug log init failed: %s", esp_err_to_name(err));
    }

}

static void boot_network_early(const clock_settings_t *cfg)
{
    set_boot_stage("network_early");
    esp_err_t err;
    if (cfg->wifi_configured && cfg->wifi_ssid[0] != '\0') {
        ESP_LOGI(TAG, "[BOOT] Wi-Fi: connecting to '%s'", cfg->wifi_ssid);
        err = network_init(cfg->wifi_ssid, cfg->wifi_password);
        ESP_LOGI(TAG, "[BOOT] network_init: %s", err == ESP_OK ? "OK" : esp_err_to_name(err));
    } else {
        ESP_LOGW(TAG, "[BOOT] Wi-Fi: not configured — initializing infrastructure for scanning");
        err = network_init_infrastructure();
        ESP_LOGI(TAG, "[BOOT] network_init_infrastructure: %s",
                 err == ESP_OK ? "OK" : esp_err_to_name(err));
    }

    // OTA init and mark_valid BEFORE display init.  This is the rollback
    // boundary: if the device reaches here, networking works and we can push
    // a fix over OTA even if the display later crashes.
    ESP_LOGI(TAG, "[BOOT] OTA init...");
    ota_init();
    ota_mark_app_valid();
    ESP_LOGI(TAG, "[BOOT] OTA valid: slot=%s, version=%s",
             ota_get_running_partition(),
             ota_get_current_version());
}

static void boot_sntp_wait(const clock_settings_t *cfg)
{
    set_boot_stage("sntp_wait");
    if (cfg->wifi_configured && cfg->wifi_ssid[0] != '\0') {
        ESP_LOGI(TAG, "[BOOT] Waiting for SNTP time sync...");
        network_wait_for_time();
        ESP_LOGI(TAG, "[BOOT] SNTP sync complete");
    } else {
        ESP_LOGW(TAG, "[BOOT] Wi-Fi not configured — skipping SNTP");
    }
}

static void boot_timezone(const clock_settings_t *cfg)
{
    set_boot_stage("timezone");
    ESP_LOGI(TAG, "[BOOT] TZ: %s", cfg->timezone);
    time_sync_setup(cfg->timezone);
}

static void boot_services(const clock_settings_t *cfg)
{
    set_boot_stage("services");
    time_t now;
    struct tm ti;
    time_sync_get_local(&now, &ti);
    ESP_LOGI(TAG, "[BOOT] Local time: %04d-%02d-%02d %02d:%02d:%02d",
             ti.tm_year+1900, ti.tm_mon+1, ti.tm_mday,
             ti.tm_hour, ti.tm_min, ti.tm_sec);

    // mDNS — advertise greenwood-clock.local for phone access
    esp_err_t err = mdns_init();
    if (err == ESP_OK) {
        mdns_hostname_set(cfg->hostname[0] ? cfg->hostname : "greenwood-clock");
        mdns_instance_name_set("Greenwood Clock");
        mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);
        ESP_LOGI(TAG, "[BOOT] mDNS: %s.local", cfg->hostname[0] ? cfg->hostname : "greenwood-clock");
    } else {
        ESP_LOGW(TAG, "[BOOT] mDNS init failed: %s", esp_err_to_name(err));
    }

    err = http_api_start();
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "[BOOT] HTTP API started on port 80");
    } else {
        ESP_LOGW(TAG, "[BOOT] HTTP API failed: %s", esp_err_to_name(err));
    }
}

// =============================================================================
// boot_await_launch — OTA SAFETY WINDOW
//
// Blocks until 60 s elapsed OR POST /debug/launch received.
// This is the ONLY thing standing between a bad OTA and a bricked device.
//
// CONTRACT:
//   - MUST be called AFTER boot_services() (HTTP API must be live)
//   - MUST be called BEFORE any task that processes external data
//   - NO tasks that could crash from bad data may be spawned before this returns
//   - NO LVGL, no file parsing, no JSON, no network fetches before this returns
//
// If you are considering moving this call or adding code before it:
//   DON'T. A crash before this window means the device is unrecoverable
//   without physical access. This has already caused a wall-dismount incident.
// =============================================================================

static SemaphoreHandle_t s_launch_sem;

static void autolaunch_timer_cb(void* arg)
{
    (void)arg;
    xSemaphoreGive(s_launch_sem);
}

static void boot_await_launch(void)
{
    set_boot_stage("await_launch");
    s_launch_sem = xSemaphoreCreateBinary();
    configASSERT(s_launch_sem);

    http_api_set_launch_sem(s_launch_sem);

    esp_timer_handle_t t;
    const esp_timer_create_args_t args = {
        .callback = autolaunch_timer_cb,
        .arg = NULL,
        .name = "autolaunch",
    };
    ESP_ERROR_CHECK(esp_timer_create(&args, &t));
    ESP_ERROR_CHECK(esp_timer_start_once(t, 60ULL * 1000 * 1000));

    // Snapshot system state before blocking — gives context for any crash in the window.
    {
        char task_list[1024];
        vTaskList(task_list);
        ESP_LOGI(TAG, "[BOOT] Pre-launch heap: free=%lu min=%lu",
                 (unsigned long)esp_get_free_heap_size(),
                 (unsigned long)esp_get_minimum_free_heap_size());
        ESP_LOGI(TAG, "[BOOT] Pre-launch tasks:\n%s", task_list);
    }

    ESP_LOGI(TAG, "[BOOT] Awaiting launch (60 s or POST /debug/launch) — display dark");
    xSemaphoreTake(s_launch_sem, portMAX_DELAY);
    ESP_LOGI(TAG, "[BOOT] Launch signaled — heap free=%lu",
             (unsigned long)esp_get_free_heap_size());

    // Semaphore is no longer needed; deregister before display init.
    http_api_set_launch_sem(NULL);
    esp_timer_stop(t);   // no-op if already fired
    esp_timer_delete(t);
    vSemaphoreDelete(s_launch_sem);
    s_launch_sem = NULL;
}

static void boot_display_init(const clock_settings_t *cfg)
{
    set_boot_stage("display_init");
    ESP_LOGI(TAG, "[BOOT] display_init: heap free=%lu", (unsigned long)esp_get_free_heap_size());

    lvgl_port_cfg_t port_config = {
        .task_priority    = 5,
        .task_stack       = 24 * 1024,  /* ThorVG rasterizes on dedicated render_task (SPIRAM); 24 KB for LVGL layout + lottie constructor */
        .task_affinity    = -1,
        .task_max_sleep_ms = 100,
        .timer_period_ms  = 16,    // ~60 FPS
    };

    bsp_display_cfg_t disp_cfg = {
        .lvgl_port_cfg = port_config,
        .buffer_size   = 1024 * 600,
        .double_buffer = 1,
        .hw_cfg = {
            .hdmi_resolution = BSP_HDMI_RES_NONE,
            .dsi_bus = {
                .phy_clk_src        = MIPI_DSI_PHY_CLK_SRC_DEFAULT,
                .lane_bit_rate_mbps = BSP_LCD_MIPI_DSI_LANE_BITRATE_MBPS,
            }
        },
        .flags = { .buff_dma = true, .buff_spiram = true, .sw_rotate = false }
    };
    set_boot_stage("bsp_display_start");
    ESP_LOGI(TAG, "[BOOT] bsp_display_start_with_config...");
    bsp_display_start_with_config(&disp_cfg);
    set_boot_stage("lvgl_started");
    ESP_LOGI(TAG, "[BOOT] bsp_display_start done, heap free=%lu", (unsigned long)esp_get_free_heap_size());
    lv_log_register_print_cb(lvgl_log_cb);
    ESP_LOGI(TAG, "[BOOT] LVGL log callback registered");

    // Heartbeat timer (inside LVGL task) + watchdog task (outside)
    lvgl_port_lock(0);
    s_lvgl_heartbeat_ms = lv_tick_get();
    lv_timer_create(heartbeat_cb, 1000, NULL);
    lvgl_port_unlock();
    // 6 KB stack: ESP_LOGE varargs + esp_timer_create can push the original 2 KB
    // over its limit if a hang is detected. Headroom > diagnostic value > minor
    // internal-DRAM cost.
    xTaskCreate(display_watchdog_task, "disp_wdog", 6144, NULL, 3, NULL);
    ESP_LOGI(TAG, "[BOOT] Display watchdog started (timeout %d ms)", LVGL_WATCHDOG_TIMEOUT_MS);

    bsp_display_backlight_on();
    bsp_display_brightness_set(cfg->brightness);
    ESP_LOGI(TAG, "[BOOT] Brightness: %d%%", cfg->brightness);

    // PPA status report
    #if defined(CONFIG_LV_USE_PPA) && CONFIG_LV_USE_PPA
        ESP_LOGI(TAG, "[BOOT] ✓ PPA hardware acceleration: ENABLED");
        #if defined(CONFIG_LV_USE_PPA_IMG) && CONFIG_LV_USE_PPA_IMG
            ESP_LOGI(TAG, "[BOOT] ✓ PPA image operations: ENABLED");
        #else
            ESP_LOGW(TAG, "[BOOT] ✗ PPA image operations: DISABLED");
        #endif
        #if defined(CONFIG_LVGL_PORT_ENABLE_PPA) && CONFIG_LVGL_PORT_ENABLE_PPA
            ESP_LOGI(TAG, "[BOOT] ✓ PPA in BSP display driver: ENABLED");
        #else
            ESP_LOGW(TAG, "[BOOT] ✗ PPA in BSP display driver: DISABLED (critical!)");
        #endif
    #else
        ESP_LOGW(TAG, "[BOOT] ✗ PPA hardware acceleration: DISABLED");
    #endif
    ESP_LOGI(TAG, "[BOOT] Buffer alignment: %d bytes", CONFIG_LV_DRAW_BUF_ALIGN);
    ESP_LOGI(TAG, "[BOOT] Stride alignment: %d bytes", CONFIG_LV_DRAW_BUF_STRIDE_ALIGN);

    // LVGL filesystem driver — A: drive maps to /sdcard/ via POSIX
    set_boot_stage("lvgl_fs_init");
    lv_fs_posix_init();
    ESP_LOGI(TAG, "[BOOT] A: drive registered (/sdcard)");

    if (lv_indev_t* t = lv_indev_get_next(NULL)) {
        lv_indev_enable(t, cfg->enable_touch);
        ESP_LOGI(TAG, "[BOOT] Touch: %s", cfg->enable_touch ? "enabled" : "disabled");
    } else {
        ESP_LOGW(TAG, "[BOOT] No touch device found");
    }

    set_boot_stage("display_ready");
    ESP_LOGI(TAG, "[BOOT] Display ready");
}

static void boot_health_loop(void)
{
    size_t last_free_heap = esp_get_free_heap_size();
    uint32_t loop_count = 0;

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(60000));
        loop_count++;

        size_t free_heap     = esp_get_free_heap_size();
        size_t min_heap      = esp_get_minimum_free_heap_size();
        size_t internal_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
        size_t internal_min  = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
        size_t spiram_free   = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);

        float tsens_c = 0.0f;
        bool  tsens_ok = tsens_sample(&tsens_c);

        if (tsens_ok) {
            ESP_LOGI(TAG, "[HEALTH] Loop %lu: free=%lu min=%lu internal=%lu int_lowwater=%lu spiram=%lu delta=%ld tsens=%.1fC (lo=%.1f hi=%.1f)",
                     (unsigned long)loop_count,
                     (unsigned long)free_heap,
                     (unsigned long)min_heap,
                     (unsigned long)internal_free,
                     (unsigned long)internal_min,
                     (unsigned long)spiram_free,
                     (long)(free_heap - last_free_heap),
                     tsens_c, s_tsens_min_c, s_tsens_max_c);
        } else {
            ESP_LOGI(TAG, "[HEALTH] Loop %lu: free=%lu min=%lu internal=%lu int_lowwater=%lu spiram=%lu delta=%ld",
                     (unsigned long)loop_count,
                     (unsigned long)free_heap,
                     (unsigned long)min_heap,
                     (unsigned long)internal_free,
                     (unsigned long)internal_min,
                     (unsigned long)spiram_free,
                     (long)(free_heap - last_free_heap));
        }

        // Internal DRAM is the scarce pool (DMA/WiFi/LVGL contend for it). When it
        // runs low, large allocations that REQUIRE internal memory stall or fail —
        // the signature of display lag without a crash. Flag it distinctly.
        if (internal_free < 12000) {
            ESP_LOGW(TAG, "[HEALTH] LOW INTERNAL DRAM! free=%lu lowwater=%lu bytes",
                     (unsigned long)internal_free, (unsigned long)internal_min);
        }

        // Log LVGL rendering stats if available
        lv_display_t *disp = lv_display_get_default();
        if (disp) {
            uint32_t elapsed = lv_tick_get() - (loop_count > 1 ? 60000 : 0);
            ESP_LOGI(TAG, "[HEALTH] LVGL tick=%lu", (unsigned long)lv_tick_get());
        }

        if (free_heap < 20000) {
            ESP_LOGW(TAG, "[HEALTH] LOW HEAP! Free: %lu bytes", (unsigned long)free_heap);
        }

        last_free_heap = free_heap;
    }
}

// =============================================================================
// app_main
// =============================================================================

extern "C" void app_main()
{
    // Snapshot RTC crash context HERE — before boot_settings_init() calls
    // set_boot_stage("settings_init") and clobbers s_boot_stage.
    // This is the only place where the previous crash's stage is still intact.
    crash_ctx_t ctx;
    ctx.magic = s_crash_magic;
    ctx.pc    = s_crash_pc;
    ctx.ra    = s_crash_ra;
    ctx.sp    = s_crash_sp;
    memcpy(ctx.reason,  s_crash_reason,  sizeof(ctx.reason));
    memcpy(ctx.stage,   s_boot_stage,    sizeof(ctx.stage));
    memcpy(ctx.details, s_crash_details, sizeof(ctx.details));
    ctx.reason[sizeof(ctx.reason)   - 1] = '\0';
    ctx.stage[sizeof(ctx.stage)     - 1] = '\0';
    ctx.details[sizeof(ctx.details) - 1] = '\0';
    s_crash_magic = 0;  // clear before this boot can produce a false re-report

    clock_settings_t cfg;

    ESP_LOGI(TAG, "=== app_main starting ===");

    boot_settings_init(&cfg);
    boot_sdcard_init(&ctx);
    tsens_init();               // ← internal chip-temp telemetry available from here
    boot_network_early(&cfg);   // ← ROLLBACK BOUNDARY: ota_mark_app_valid() fires here
    boot_sntp_wait(&cfg);
    boot_timezone(&cfg);
    boot_history_record();      // ← persist reset_reason + boot timestamp to NVS
    boot_services(&cfg);        // HTTP API up, OTA pushable, UDP log streamable

    // =================================================================
    // OTA SAFETY WINDOW — DO NOT MOVE OR BYPASS
    //
    // boot_await_launch() blocks for 60 seconds (or until POST /debug/launch).
    // During this window the device is:
    //   - Reachable via HTTP API (OTA push, reboot, file ops)
    //   - NOT running any user-facing features that could crash
    //
    // NOTHING that processes external data (network fetches, JSON parsing,
    // LVGL rendering, file loading) may run before this call returns.
    // If a crash occurs before this point, the device is OTA-recoverable.
    // If a crash occurs after, the device may require physical access.
    //
    // This saved us from a wall-dismount once. Do not touch.
    // =================================================================
    boot_await_launch();
    // =================================================================
    // END OTA SAFETY WINDOW — features below here are crash-tolerant
    // (device is already proven bootable + HTTP-reachable)
    // =================================================================

    // Start NWS weather polling AFTER the safety window.
    // If this task crashes, HTTP API is still up — push a fix via OTA.
    if (cfg.wifi_configured && cfg.enable_weather) {
        esp_err_t nws_err = nws_init(cfg.latitude, cfg.longitude, display_fsm_send_event);
        if (nws_err == ESP_OK) {
            ESP_LOGI(TAG, "[BOOT] NWS weather task started");
        } else {
            ESP_LOGW(TAG, "[BOOT] NWS init failed: %s (weather disabled)",
                     esp_err_to_name(nws_err));
        }
    } else {
        ESP_LOGI(TAG, "[BOOT] NWS weather: %s",
                 !cfg.wifi_configured ? "skipped (no WiFi)" : "disabled in settings");
    }
    boot_display_init(&cfg);    // LVGL init — after safety window

    // Display FSM takes ownership of the clock display.
    // Creates ClockWidget, loads background + Lottie, registers gestures,
    // starts clock update timer. Replaces the old ui_launch_clock() path.
    set_boot_stage("fsm_init");
    ESP_LOGI(TAG, "[BOOT] display_fsm_init...");
    display_fsm_init();
    ESP_LOGI(TAG, "[BOOT] display_fsm_init done — entering health loop");
    set_boot_stage("health_loop");
    boot_health_loop();         // never returns
}
