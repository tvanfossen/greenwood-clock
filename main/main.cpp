// main/main.cpp

#include "esp_log.h"
#include "esp_err.h"
#include "esp_spiffs.h"
#include "esp_system.h"
#include "esp_task_wdt.h"
#include "bsp/esp-bsp.h"
#include "bsp/display.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "network.h"
#include "time_sync.h"
#include "ui.h"
#include "weather.h"
#include "secrets.h"
#include "settings.h"
#include "ota.h"
#include "sdcard.h"
#include "http_api.h"
#include "debug_log.h"
#include "lvgl.h"
#include "libs/fsdrv/lv_fsdrv.h"

#include <fcntl.h>
#include <sys/types.h>
#include <dirent.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

static const char* TAG = "main";

// Custom LVGL filesystem driver for SPIFFS (B: drive)
static lv_fs_drv_t spiffs_fs_drv;
static const char* spiffs_base_path = "/spiffs";

// POSIX filesystem callbacks for SPIFFS driver
static void * spiffs_fs_open(lv_fs_drv_t * drv, const char * path, lv_fs_mode_t mode)
{
    LV_UNUSED(drv);

    ESP_LOGI(TAG, "[B: DRIVER] Open request: path='%s', mode=%d", path, mode);

    int flags = 0;
    if(mode == LV_FS_MODE_WR) flags = O_WRONLY | O_CREAT;
    else if(mode == LV_FS_MODE_RD) flags = O_RDONLY;
    else if(mode == (LV_FS_MODE_WR | LV_FS_MODE_RD)) flags = O_RDWR | O_CREAT;

    char buf[256];
    snprintf(buf, sizeof(buf), "%s%s", spiffs_base_path, path);
    ESP_LOGI(TAG, "[B: DRIVER] Full path: %s", buf);

    int fd = open(buf, flags, 0666);
    if(fd < 0) {
        ESP_LOGE(TAG, "[B: DRIVER] FAILED to open: %s, errno: %d (%s)", buf, errno, strerror(errno));
        return NULL;
    }

    ESP_LOGI(TAG, "[B: DRIVER] Successfully opened fd=%d", fd);
    return (void *)(intptr_t)(fd + 1);  // +1 because fd can be 0
}

static lv_fs_res_t spiffs_fs_close(lv_fs_drv_t * drv, void * file_p)
{
    LV_UNUSED(drv);
    int fd = (intptr_t)file_p - 1;
    close(fd);
    return LV_FS_RES_OK;
}

static lv_fs_res_t spiffs_fs_read(lv_fs_drv_t * drv, void * file_p, void * buf, uint32_t btr, uint32_t * br)
{
    LV_UNUSED(drv);
    int fd = (intptr_t)file_p - 1;
    ssize_t ret = read(fd, buf, btr);
    if(ret < 0) {
        *br = 0;
        return LV_FS_RES_UNKNOWN;
    }
    *br = ret;
    return LV_FS_RES_OK;
}

static lv_fs_res_t spiffs_fs_write(lv_fs_drv_t * drv, void * file_p, const void * buf, uint32_t btw, uint32_t * bw)
{
    LV_UNUSED(drv);
    int fd = (intptr_t)file_p - 1;
    ssize_t ret = write(fd, buf, btw);
    if(ret < 0) {
        *bw = 0;
        return LV_FS_RES_UNKNOWN;
    }
    *bw = ret;
    return LV_FS_RES_OK;
}

static lv_fs_res_t spiffs_fs_seek(lv_fs_drv_t * drv, void * file_p, uint32_t pos, lv_fs_whence_t whence)
{
    LV_UNUSED(drv);
    int fd = (intptr_t)file_p - 1;
    int w;
    if(whence == LV_FS_SEEK_SET) w = SEEK_SET;
    else if(whence == LV_FS_SEEK_CUR) w = SEEK_CUR;
    else if(whence == LV_FS_SEEK_END) w = SEEK_END;
    else return LV_FS_RES_UNKNOWN;

    if(lseek(fd, pos, w) < 0) return LV_FS_RES_UNKNOWN;
    return LV_FS_RES_OK;
}

static lv_fs_res_t spiffs_fs_tell(lv_fs_drv_t * drv, void * file_p, uint32_t * pos_p)
{
    LV_UNUSED(drv);
    int fd = (intptr_t)file_p - 1;
    off_t pos = lseek(fd, 0, SEEK_CUR);
    if(pos < 0) return LV_FS_RES_UNKNOWN;
    *pos_p = pos;
    return LV_FS_RES_OK;
}

static void lv_fs_spiffs_init(void)
{
    lv_fs_drv_init(&spiffs_fs_drv);

    spiffs_fs_drv.letter = 'B';
    spiffs_fs_drv.cache_size = 0;

    spiffs_fs_drv.open_cb = spiffs_fs_open;
    spiffs_fs_drv.close_cb = spiffs_fs_close;
    spiffs_fs_drv.read_cb = spiffs_fs_read;
    spiffs_fs_drv.write_cb = spiffs_fs_write;
    spiffs_fs_drv.seek_cb = spiffs_fs_seek;
    spiffs_fs_drv.tell_cb = spiffs_fs_tell;

    spiffs_fs_drv.dir_close_cb = NULL;
    spiffs_fs_drv.dir_open_cb = NULL;
    spiffs_fs_drv.dir_read_cb = NULL;

    lv_fs_drv_register(&spiffs_fs_drv);
}

// LVGL heartbeat watchdog
// heartbeat_cb runs inside the LVGL task every 1 s.  If the task hangs (blue
// screen / render deadlock), it stops updating s_lvgl_heartbeat_ms.
// display_watchdog_task checks from outside and forces a restart after 15 s
// of silence — recovering the device without requiring a power cycle.
#define LVGL_WATCHDOG_TIMEOUT_MS  15000
#define LVGL_WATCHDOG_CHECK_MS     5000

static volatile uint32_t s_lvgl_heartbeat_ms = 0;

static void heartbeat_cb(lv_timer_t *timer)
{
    (void)timer;
    s_lvgl_heartbeat_ms = lv_tick_get();
}

static void display_watchdog_task(void *arg)
{
    (void)arg;
    // Grace period: give LVGL time to start before watching
    vTaskDelay(pdMS_TO_TICKS(LVGL_WATCHDOG_TIMEOUT_MS));

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(LVGL_WATCHDOG_CHECK_MS));
        uint32_t elapsed = lv_tick_elaps(s_lvgl_heartbeat_ms);
        if (elapsed > LVGL_WATCHDOG_TIMEOUT_MS) {
            ESP_LOGE(TAG, "===== DISPLAY HANG DETECTED ===== (no LVGL heartbeat for %lu ms)",
                     (unsigned long)elapsed);
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
    ESP_LOGE(TAG, "========== LVGL ASSERT FAILED ==========");
    ESP_LOGE(TAG, "Heap free: %lu bytes", (unsigned long)esp_get_free_heap_size());
    ESP_LOGE(TAG, "LVGL assertion triggered, resetting device...");
    debug_log_flush();  // commit log before restart so the assert is visible

    // Do NOT call vTaskDelay() here - it can cause crashes from interrupt/invalid context
    // Instead, use a direct restart with minimal delay
    for (volatile int i = 0; i < 100000000; i++) {
        // Busy wait to give time for log to flush to console
    }

    esp_restart();
}

// Shutdown handler — fires for ALL esp_restart() calls (OTA, assert, panic).
// Actual panic detection uses esp_reset_reason() at [1.5] on the next boot.
static void panic_handler_hook(void) {
    debug_log_flush();  // commit log before halt/reboot so last bytes are not lost
    ESP_LOGE(TAG, "========== SHUTDOWN HANDLER ==========");
    ESP_LOGE(TAG, "Heap free: %lu bytes", (unsigned long)esp_get_free_heap_size());
    ESP_LOGE(TAG, "Min heap ever: %lu bytes", (unsigned long)esp_get_minimum_free_heap_size());

    // Log stack watermarks for all tasks
    char task_list[1024];
    vTaskList(task_list);
    ESP_LOGE(TAG, "Task list:\n%s", task_list);

    ESP_LOGE(TAG, "====================================");
}

#define DEFAULT_FD_NUM      2
#define DEFAULT_MOUNT_POINT "/spiffs"  // Mount SPIFFS at /spiffs


esp_err_t bsp_spiffs_init(const char *partition_label, const char *mount_point, size_t max_files)
{
    esp_vfs_spiffs_conf_t conf = {
        .base_path = mount_point,
        .partition_label = partition_label,
        .max_files = max_files,
        .format_if_mount_failed = false,
    };

    esp_err_t ret_val = esp_vfs_spiffs_register(&conf);

    if (ESP_OK != ret_val) {
        ESP_LOGE(TAG, "SPIFFS register failed: %d", ret_val);
        return ret_val;
    }

    size_t total = 0, used = 0;

    ret_val = esp_spiffs_info(conf.partition_label, &total, &used);
    if (ret_val != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get SPIFFS partition information (%s)", esp_err_to_name(ret_val));
    }
    else {
        ESP_LOGI(TAG, "Partition size: total: %d, used: %d", total, used);
    }

    return ret_val;
}

esp_err_t bsp_spiffs_init_default(void)
{
    return bsp_spiffs_init(NULL, DEFAULT_MOUNT_POINT, DEFAULT_FD_NUM);
}

esp_err_t bsp_spiffs_deinit(const char *partition_label)
{
    return esp_vfs_spiffs_unregister(partition_label);
}

esp_err_t bsp_spiffs_deinit_default(void)
{
    return bsp_spiffs_deinit(NULL);
}

extern "C" void app_main()
{
    esp_err_t err;
    time_t now;
    struct tm ti;
    clock_settings_t cfg;

    ESP_LOGI(TAG, "=== app_main starting ===");

    // Assert display panel RESET immediately — holds EK79007 in reset through
    // the entire boot sequence ([0]–[1.5]) so it never enters a clock-starved
    // error state after esp_restart().  BSP releases it properly via
    // esp_lcd_panel_reset() at [2].  Without this, GPIO27 floats for ~5 s
    // after a SW reset, causing intermittent blue screens on the first post-OTA
    // boot.
    gpio_set_direction(BSP_LCD_RST, GPIO_MODE_OUTPUT);
    gpio_set_level(BSP_LCD_RST, 0);

    // Register panic handler
    esp_register_shutdown_handler(panic_handler_hook);
    ESP_LOGI(TAG, "[0] Panic handler registered");

    // 0) Initialize settings
    ESP_LOGI(TAG, "[0] Initializing settings...");
    err = settings_init();
    ESP_LOGI(TAG, "[0] %s", err == ESP_OK
             ? "Settings initialized"
             : esp_err_to_name(err));

    ESP_LOGI(TAG, "[0] Loading settings...");
    err = settings_load(&cfg);
    ESP_LOGI(TAG, "[0] %s", err == ESP_OK
             ? "Settings loaded"
             : esp_err_to_name(err));

    // 1) Mount SPIFFS
    ESP_LOGI(TAG, "[1] Mounting SPIFFS...");
    err = bsp_spiffs_init_default();
    ESP_LOGI(TAG, "[1] %s", err == ESP_OK
             ? "SPIFFS mounted successfully"
             : esp_err_to_name(err));

    // Verify SPIFFS contents
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "[1] Verifying SPIFFS contents:");
        DIR* dir = opendir("/spiffs");
        if (dir) {
            struct dirent* entry;
            while ((entry = readdir(dir)) != NULL) {
                ESP_LOGI(TAG, "[1]   - %s", entry->d_name);
            }
            closedir(dir);

            // Check if splash.png exists
            FILE* f = fopen("/spiffs/splash.png", "r");
            if (f) {
                fseek(f, 0, SEEK_END);
                long size = ftell(f);
                fclose(f);
                ESP_LOGI(TAG, "[1] ✓ splash.png exists (%ld bytes)", size);
            } else {
                ESP_LOGE(TAG, "[1] ✗ splash.png NOT FOUND in SPIFFS!");
            }
        } else {
            ESP_LOGE(TAG, "[1] Could not open /spiffs directory!");
        }
    }

    // 1.5) Check reset reason — annotates the log at the start of each session.
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
    ESP_LOGI(TAG, "[1.5] Reset reason: %s (%d)", reset_name, (int)reset_reason);

    // 1.5) Initialize and mount SD card
    ESP_LOGI(TAG, "[1.5] Initializing SD card...");
    sdcard_init();
    err = sdcard_mount(false);  // Don't auto-format, just try to mount
    if (err == ESP_OK) {
        sdcard_info_t info;
        if (sdcard_get_info(&info) == ESP_OK) {
            ESP_LOGI(TAG, "[1.5] SD card mounted: %s", info.card_type);
            ESP_LOGI(TAG, "[1.5] Capacity: %.2f GB, Used: %.2f GB (%.0f%%)",
                     info.total_bytes / (1024.0 * 1024.0 * 1024.0),
                     info.used_bytes / (1024.0 * 1024.0 * 1024.0),
                     (info.used_bytes * 100.0) / info.total_bytes);

            // Create standard directories
            sdcard_create_directories();

            // Start debug logging to SD card
            ESP_LOGI(TAG, "[1.5] Starting debug log capture...");
            err = debug_log_init();
            if (err == ESP_OK) {
                ESP_LOGI(TAG, "[1.5] Debug logging started: %s", debug_log_get_path());
                // Log reset reason into SD log now that debug_log is open.
                // The earlier ESP_LOGI at [1.5] only reached serial console.
                ESP_LOGI(TAG, "[1.5] Reset reason (SD): %s (%d)", reset_name, (int)reset_reason);
                // Emit a clearly searchable crash marker at the start of the log
                // session so abnormal prior boots are immediately visible.
                if (is_abnormal_reset) {
                    ESP_LOGE(TAG,
                             "===== ABNORMAL RESET: reason=%s ===== (see previous log slot)",
                             reset_name);
                }
            } else {
                ESP_LOGW(TAG, "[1.5] Failed to start debug logging: %s", esp_err_to_name(err));
            }

            // Check for splash.png on SD card
            FILE* f = fopen("/sdcard/splash.png", "r");
            if (f) {
                fseek(f, 0, SEEK_END);
                long size = ftell(f);
                fclose(f);
                ESP_LOGI(TAG, "[1.5] ✓ /sdcard/splash.png exists (%ld bytes)", size);
            } else {
                ESP_LOGW(TAG, "[1.5] ✗ /sdcard/splash.png not found (this is OK, will use SPIFFS)");
            }
        }
    } else {
        ESP_LOGW(TAG, "[1.5] SD card not available: %s", esp_err_to_name(err));
        if (err == ESP_FAIL) {
            ESP_LOGW(TAG, "[1.5] SD card may need formatting");
            ESP_LOGW(TAG, "[1.5] To format, build with sdcard_mount(true) or use UI format option");
        }
        ESP_LOGW(TAG, "[1.5] Continuing without SD card support");
    }
    lvgl_port_cfg_t port_config =   {
        .task_priority = 5,       // Increased from 4 for better responsiveness
        .task_stack = 16*1024,       // Increased from 7168 for GIF decoding
        .task_affinity = -1,
        .task_max_sleep_ms = 100,  // Reduced from 500 for snappier responsiveness
        .timer_period_ms = 16,    // ~60 FPS - GIF decoding is CPU intensive, 200 FPS causes lag
    };
    // 2) Configure & start display
    ESP_LOGI(TAG, "[2] Configuring display…");
    bsp_display_cfg_t disp_cfg = {
        .lvgl_port_cfg  = port_config,
        .buffer_size    = 1024 * 600,  // Full-screen buffer for maximum rendering performance
        .double_buffer  = 1,  // Enable double buffering for smooth animations
        .hw_cfg = {
            .hdmi_resolution    = BSP_HDMI_RES_NONE,
            .dsi_bus = {
                .phy_clk_src         = MIPI_DSI_PHY_CLK_SRC_DEFAULT,
                .lane_bit_rate_mbps  = BSP_LCD_MIPI_DSI_LANE_BITRATE_MBPS,
            }
        },
        .flags = { .buff_dma = true, .buff_spiram = true, .sw_rotate = false }  // Enabled DMA for faster transfers
    };
    bsp_display_start_with_config(&disp_cfg);
    lv_log_register_print_cb(lvgl_log_cb);
    ESP_LOGI(TAG, "[2] LVGL log callback registered — errors/warnings routed to debug log + UDP");

    // Start LVGL heartbeat timer (fires inside LVGL task every 1 s)
    lvgl_port_lock(0);
    s_lvgl_heartbeat_ms = lv_tick_get();
    lv_timer_create(heartbeat_cb, 1000, NULL);
    lvgl_port_unlock();
    // Start display watchdog task (monitors heartbeat from outside LVGL task)
    xTaskCreate(display_watchdog_task, "disp_wdog", 2048, NULL, 3, NULL);
    ESP_LOGI(TAG, "[2] Display watchdog started (timeout %d ms)", LVGL_WATCHDOG_TIMEOUT_MS);

    bsp_display_backlight_on();
    bsp_display_brightness_set(cfg.brightness);
    ESP_LOGI(TAG, "[2] Brightness set to %d%%", cfg.brightness);

    // Log PPA hardware acceleration status
    #if defined(CONFIG_LV_USE_PPA) && CONFIG_LV_USE_PPA
        ESP_LOGI(TAG, "[2] ✓ PPA hardware acceleration: ENABLED");
        #if defined(CONFIG_LV_USE_PPA_IMG) && CONFIG_LV_USE_PPA_IMG
            ESP_LOGI(TAG, "[2] ✓ PPA image operations: ENABLED");
        #else
            ESP_LOGW(TAG, "[2] ✗ PPA image operations: DISABLED");
        #endif
        #if defined(CONFIG_LVGL_PORT_ENABLE_PPA) && CONFIG_LVGL_PORT_ENABLE_PPA
            ESP_LOGI(TAG, "[2] ✓ PPA in BSP display driver: ENABLED");
        #else
            ESP_LOGW(TAG, "[2] ✗ PPA in BSP display driver: DISABLED (critical!)");
        #endif
    #else
        ESP_LOGW(TAG, "[2] ✗ PPA hardware acceleration: DISABLED");
    #endif
    ESP_LOGI(TAG, "[2] Buffer alignment: %d bytes (cache line size)", CONFIG_LV_DRAW_BUF_ALIGN);
    ESP_LOGI(TAG, "[2] Stride alignment: %d bytes", CONFIG_LV_DRAW_BUF_STRIDE_ALIGN);

    // Initialize LVGL POSIX filesystem driver for SD card access (A: drive)
    ESP_LOGI(TAG, "[2] Initializing LVGL POSIX filesystem driver for SD card (A:)");
    lv_fs_posix_init();
    ESP_LOGI(TAG, "[2] ✓ A: drive registered (base path: /sdcard)");

    // Register B: drive for SPIFFS access
    ESP_LOGI(TAG, "[2] Registering LVGL SPIFFS driver (B:)");
    lv_fs_spiffs_init();
    ESP_LOGI(TAG, "[2] ✓ B: drive registered (base path: /spiffs)");

    // Test filesystem drivers
    ESP_LOGI(TAG, "[2] Testing LVGL filesystem drivers:");

    // Test B: drive
    lv_fs_file_t f;
    lv_fs_res_t res = lv_fs_open(&f, "B:/splash.png", LV_FS_MODE_RD);
    if (res == LV_FS_RES_OK) {
        ESP_LOGI(TAG, "[2] ✓ B:/splash.png opened successfully via LVGL");
        lv_fs_close(&f);
    } else {
        ESP_LOGE(TAG, "[2] ✗ B:/splash.png FAILED to open via LVGL (res=%d)", res);
    }

    // Test A: drive
    res = lv_fs_open(&f, "A:/splash.png", LV_FS_MODE_RD);
    if (res == LV_FS_RES_OK) {
        ESP_LOGI(TAG, "[2] ✓ A:/splash.png opened successfully via LVGL");
        lv_fs_close(&f);
    } else {
        ESP_LOGW(TAG, "[2] ⚠ A:/splash.png not found (res=%d) - this is OK if not on SD card", res);
    }
    if (lv_indev_t* t = lv_indev_get_next(NULL)) {
        lv_indev_enable(t, cfg.enable_touch);
        ESP_LOGI(TAG, "[2] Touch input %s", cfg.enable_touch ? "enabled" : "disabled");
    } else {
        ESP_LOGW(TAG, "[2] No touch input device found");
    }

    // 3) Splash
    ESP_LOGI(TAG, "[3] Showing splash…");
    ui_show_splash();
    ESP_LOGI(TAG, "[3] Splash done");

    // 4) Wi‑Fi & SNTP
    ESP_LOGI(TAG, "[4] network_init…");
    if (cfg.wifi_configured && cfg.wifi_ssid[0] != '\0') {
        ESP_LOGI(TAG, "[4] Using WiFi credentials from settings");
        err = network_init(cfg.wifi_ssid, cfg.wifi_password);
        ESP_LOGI(TAG, "[4] network_init: %s",
                 err == ESP_OK ? "OK" : esp_err_to_name(err));
        ESP_LOGI(TAG, "[4] Waiting for SNTP…");
        network_wait_for_time();
        ESP_LOGI(TAG, "[4] SNTP sync complete");
    } else {
        ESP_LOGW(TAG, "[4] WiFi not configured! Please configure via touchscreen settings.");
        ESP_LOGW(TAG, "[4] Initializing WiFi infrastructure for scanning...");
        // Initialize network infrastructure but don't connect
        err = network_init_infrastructure();
        ESP_LOGI(TAG, "[4] network_init_infrastructure: %s",
                 err == ESP_OK ? "OK" : esp_err_to_name(err));
    }

    // 5) TZ
    ESP_LOGI(TAG, "[5] Setting TZ to %s", cfg.timezone);
    time_sync_setup(cfg.timezone);

    // 5.5) Initialize OTA and check if new firmware needs validation
    ESP_LOGI(TAG, "[5.5] Initializing OTA...");
    ota_init();
    ota_mark_app_valid();  // Mark this boot as successful
    ESP_LOGI(TAG, "[5.5] Running partition: %s, version: %s",
             ota_get_running_partition(),
             ota_get_current_version());

    // 6) Clock UI
    time_sync_get_local(&now, &ti);
    ESP_LOGI(TAG, "[6] Local time %04d-%02d-%02d %02d:%02d:%02d",
             ti.tm_year+1900, ti.tm_mon+1, ti.tm_mday,
             ti.tm_hour, ti.tm_min, ti.tm_sec);
    weather_init();

    // 6) HTTP API Server
    ESP_LOGI(TAG, "[6] Starting HTTP API server...");
    err = http_api_start();
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "[6] HTTP API server started on port 80");
    } else {
        ESP_LOGW(TAG, "[6] Failed to start HTTP API server: %s", esp_err_to_name(err));
    }

    ESP_LOGI(TAG, "[7] Showing start screen (bootloader mode)…");
    ui_show_start_screen(&ti, &cfg);
    ESP_LOGI(TAG, "[7] Start screen ready - press Start to launch clock");
    
    // 7) Idle loop with heap monitoring
    ESP_LOGI(TAG, "[7] Entering idle loop with heap monitoring");
    size_t last_free_heap = esp_get_free_heap_size();
    uint32_t loop_count = 0;

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(60000));  // Every 60 seconds
        loop_count++;

        size_t free_heap = esp_get_free_heap_size();
        size_t min_heap = esp_get_minimum_free_heap_size();

        ESP_LOGI(TAG, "[HEALTH] Loop %lu: Heap free=%lu, min=%lu, delta=%ld",
                 (unsigned long)loop_count,
                 (unsigned long)free_heap,
                 (unsigned long)min_heap,
                 (long)(free_heap - last_free_heap));

        if (free_heap < 20000) {
            ESP_LOGW(TAG, "[HEALTH] LOW HEAP WARNING! Free: %lu bytes", (unsigned long)free_heap);
        }

        last_free_heap = free_heap;
    }
}

