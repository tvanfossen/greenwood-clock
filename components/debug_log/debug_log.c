// components/debug_log/debug_log.c

#include "debug_log.h"
#include "sdcard.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static const char* TAG = "debug_log";

#define DEBUG_LOG_SLOT_COUNT  3
#define DEBUG_LOG_MAX_SIZE    (5 * 1024 * 1024)

// Disable SD logging after this many consecutive write failures. A failing/flaky
// SD card makes every write time out; writing synchronously from the log hook then
// stalls the caller (tripping the LVGL display watchdog) and, when the sdmmc error
// is itself logged, recurses on a tiny stack → panic loop. Giving up on the SD and
// falling back to console-only keeps the device alive on a bad card.
#define DEBUG_LOG_MAX_WRITE_FAILURES  3

// fsync() forces a physical SD flush (rewrites FAT + directory entry). Doing it on
// every log line means constant SD writes: flash wear, and — because the log hook
// runs synchronously in the context of whatever task logged — a UI-thread log would
// block the LVGL task on the SD bus (display watchdog) and starve the C6 WiFi, which
// shares the SDMMC controller (sdio_rx panic). Instead we fflush() every line (cheap:
// stdio buffer → FATFS RAM cache, preserving crash lead-up) but fsync() to the card at
// most once per interval below. Bounds crash-log loss to ~2s of lead-up; the panic
// frame itself goes to RTC memory, not this log, so that loss is acceptable.
#define DEBUG_LOG_FSYNC_INTERVAL_US   (2 * 1000 * 1000)

// Spinlock protecting log_file pointer.
// Held only for pointer load/store (never while doing file I/O).
// Safe from task context; short enough that interrupt latency is negligible.
static portMUX_TYPE s_log_mux = portMUX_INITIALIZER_UNLOCKED;

static FILE*          log_file          = NULL;
static bool           is_active         = false;
static bool           s_in_vprintf      = false;
static int            s_slot            = 0;
static size_t         s_bytes_written   = 0;
static int            s_write_failures  = 0;   // consecutive SD write failures
static int64_t        s_last_fsync_us   = 0;   // last physical SD flush (esp_timer clock)
static char           s_slot_paths[DEBUG_LOG_SLOT_COUNT][64];

// Original vprintf function (for console output)
static vprintf_like_t original_vprintf = NULL;

// =============================================================================
// Internal helpers
// =============================================================================

static void debug_log_init_slot_paths(void)
{
    for (int i = 0; i < DEBUG_LOG_SLOT_COUNT; i++) {
        snprintf(s_slot_paths[i], sizeof(s_slot_paths[i]),
                 "/sdcard/logs/debug_log%d.log", i);
    }
}


/**
 * @brief Shift log files down FIFO: slot2 deleted, slot1→slot2, slot0→slot1.
 *
 * Slot 0 is free for writing after this call.
 * Used on boot and on mid-session 5 MB rotation.
 * rename()/unlink() on non-existent files fail silently — correct on first boot.
 * No mtime dependency: gettimeofday() returns 0 before SNTP sync.
 */
static void debug_log_fifo_shift(void)
{
    unlink(s_slot_paths[2]);
    rename(s_slot_paths[1], s_slot_paths[2]);
    rename(s_slot_paths[0], s_slot_paths[1]);
}

/**
 * @brief Write rotation footer, close @p old_f, and FIFO-shift slot files.
 */
static void debug_log_close_and_shift(FILE* old_f)
{
    fprintf(old_f,
            "\n===== LOG ROTATION: slot full (%.1f MB), shifting =====\n",
            (double)s_bytes_written / (1024.0 * 1024.0));
    fflush(old_f);
    fsync(fileno(old_f));
    fclose(old_f);
    debug_log_fifo_shift();
}

/**
 * @brief Open slot 0 for writing, write the rotation header, and publish
 *        the new file handle under s_log_mux.
 *
 * Sets is_active = false if fopen fails.
 */
static void debug_log_open_fresh_slot0(void)
{
    FILE* new_f = fopen(s_slot_paths[0], "w");
    if (!new_f) {
        is_active = false;
        return;
    }
    s_bytes_written = 0;
    struct timeval tv;
    gettimeofday(&tv, NULL);
    fprintf(new_f,
            "===== LOG ROTATION: new slot 0 at %ld =====\n\n", tv.tv_sec);
    fflush(new_f);
    fsync(fileno(new_f));
    taskENTER_CRITICAL(&s_log_mux);
    log_file = new_f;
    taskEXIT_CRITICAL(&s_log_mux);
}

/**
 * @brief Close the full slot, shift files FIFO, and open a fresh slot 0.
 *
 * Called from within debug_log_vprintf (s_in_vprintf is true), so any
 * ESP_LOG* calls here go to console only — no reentrancy risk.
 */
static void debug_log_rotate(void)
{
    // Null out log_file BEFORE fclose so concurrent vprintf calls see NULL
    // and skip the write rather than calling vfprintf on a closing handle.
    FILE* old_f = NULL;
    taskENTER_CRITICAL(&s_log_mux);
    old_f    = log_file;
    log_file = NULL;
    taskEXIT_CRITICAL(&s_log_mux);

    if (!old_f) return;
    debug_log_close_and_shift(old_f);
    debug_log_open_fresh_slot0();
}

/**
 * @brief Write @p fmt/@p args to file handle @p f, tracking bytes and rotating
 *        if the slot limit is reached.
 *
 * @p f must be a valid, open file handle obtained while holding s_log_mux.
 * Must only be called with s_in_vprintf set to prevent reentrancy.
 *
 * @param f    File handle to write to.
 * @param fmt  Format string.
 * @param args Variadic argument list (a va_copy is made internally).
 */
// Returns false if the SD write failed (card timing out / removed) so the caller
// can count failures and eventually give up on the SD.
static bool debug_log_write_to_file(FILE* f, const char* fmt, va_list args)
{
    va_list file_args;
    va_copy(file_args, args);
    int n = vfprintf(f, fmt, file_args);
    va_end(file_args);
    if (n < 0) return false;                      // write failed (SD error)
    s_bytes_written += (size_t)n;
    if (fflush(f) != 0) return false;             // stdio buffer → FATFS RAM cache
    // Physical SD flush is throttled: at most once per DEBUG_LOG_FSYNC_INTERVAL_US,
    // not every line. See the interval macro for the rationale.
    int64_t now = esp_timer_get_time();
    if (now - s_last_fsync_us >= DEBUG_LOG_FSYNC_INTERVAL_US) {
        if (fsync(fileno(f)) != 0) return false;
        s_last_fsync_us = now;
    }
    if (s_bytes_written >= DEBUG_LOG_MAX_SIZE) {
        debug_log_rotate();
    }
    return true;
}

/**
 * @brief Snapshot the active log_file under the spinlock and, if writable,
 *        write the formatted message and clear the reentrancy guard.
 *
 * Thread safety: s_log_mux spinlock protects the log_file pointer snapshot
 * so a concurrent debug_log_pause_for_read() cannot fclose() the handle
 * between the NULL check and the vfprintf() call.
 */
static void debug_log_acquire_and_write(const char* fmt, va_list args)
{
    FILE* f = NULL;
    taskENTER_CRITICAL(&s_log_mux);
    if (log_file != NULL && is_active && !s_in_vprintf) {
        f = log_file;
        s_in_vprintf = true;
    }
    taskEXIT_CRITICAL(&s_log_mux);

    if (f) {
        bool ok = debug_log_write_to_file(f, fmt, args);
        bool giving_up = false;
        taskENTER_CRITICAL(&s_log_mux);
        s_in_vprintf = false;
        if (ok) {
            s_write_failures = 0;
        } else if (++s_write_failures >= DEBUG_LOG_MAX_WRITE_FAILURES) {
            is_active = false;          // SD is failing — stop writing (console-only)
            giving_up = true;
        }
        taskEXIT_CRITICAL(&s_log_mux);
        if (giving_up) {
            // esp_rom_printf writes straight to UART — safe from the log hook (no recursion).
            esp_rom_printf("debug_log: SD writes failing — disabling SD log (console-only)\n");
        }
    }
}

/**
 * @brief Custom vprintf that tees output to both the console and the log file.
 *
 * Tracks bytes written and triggers rotation when the slot reaches 5 MB.
 * A reentrancy guard prevents rotate's ESP_LOG calls from looping back.
 */
static int debug_log_vprintf(const char* fmt, va_list args)
{
    int ret = 0;
    if (original_vprintf) {
        va_list console_args;
        va_copy(console_args, args);
        ret = original_vprintf(fmt, console_args);
        va_end(console_args);
    }
    debug_log_acquire_and_write(fmt, args);
    return ret;
}

/**
 * @brief Ensure the /sdcard/logs directory exists, creating it if necessary.
 */
static esp_err_t debug_log_ensure_dir(void)
{
    const char* log_dir = "/sdcard/logs";
    struct stat st;
    if (stat(log_dir, &st) == 0) return ESP_OK;
    ESP_LOGI(TAG, "Creating logs directory: %s", log_dir);
    if (mkdir(log_dir, 0755) != 0) {
        ESP_LOGE(TAG, "Failed to create logs directory");
        return ESP_FAIL;
    }
    return ESP_OK;
}

/**
 * @brief Write the session-start header to the already-open log_file and sync.
 */
static void debug_log_write_session_header(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    fprintf(log_file, "\n\n===== Log session started at %ld (slot %d) =====\n",
            tv.tv_sec, s_slot);
    fprintf(log_file, "Debug log initialized successfully\n");
    fflush(log_file);
    fsync(fileno(log_file));
}

/**
 * @brief Open the active slot fresh (overwrite) and write the session header.
 *
 * Each boot starts a new file so the previous boot's content is preserved
 * in other slots until they are naturally rotated.
 */
static esp_err_t debug_log_open_file(void)
{
    log_file = fopen(s_slot_paths[s_slot], "w");
    if (!log_file) {
        ESP_LOGE(TAG, "Failed to open log file: %s", s_slot_paths[s_slot]);
        return ESP_FAIL;
    }
    s_bytes_written = 0;
    debug_log_write_session_header();
    return ESP_OK;
}

static esp_err_t debug_log_prepare_file(void)
{
    esp_err_t err = debug_log_ensure_dir();
    if (err != ESP_OK) return err;
    return debug_log_open_file();
}

/**
 * @brief Write the test block to @p f and force a sync.
 */
static void debug_log_write_test_block(FILE* f, const struct timeval* tv)
{
    fprintf(f, "[TEST] Debug log test write - if you see this, file I/O works!\n");
    fprintf(f, "[TEST] Timestamp: %ld\n", (long)tv->tv_sec);
    fprintf(f, "[TEST] is_active: %d\n", is_active);
    fprintf(f, "[TEST] log_file: %p\n", (void*)log_file);
    fprintf(f, "[TEST] slot: %d path: %s\n", s_slot, s_slot_paths[s_slot]);
    fprintf(f, "[TEST] original_vprintf: %p\n", (void*)original_vprintf);
    fflush(f);
    fsync(fileno(f));
}

/**
 * @brief Mark logging active and redirect ESP log output to the log file.
 *
 * Logs confirmation messages via the newly redirected vprintf so they appear
 * in both console and SD card log.
 */
static void debug_log_activate(void)
{
    is_active = true;
    original_vprintf = esp_log_set_vprintf(debug_log_vprintf);
    ESP_LOGI(TAG, "Debug logging started: %s (%.1f MB used)",
             s_slot_paths[s_slot], (double)s_bytes_written / (1024.0 * 1024.0));
    ESP_LOGI(TAG, "vprintf redirection: original=%p", original_vprintf);
    fflush(log_file);
}

// =============================================================================
// Public API
// =============================================================================

esp_err_t debug_log_init(void)
{
    ESP_LOGI(TAG, "Initializing debug log system");
    if (!sdcard_is_mounted()) {
        ESP_LOGW(TAG, "SD card not mounted, cannot initialize debug logging");
        return ESP_FAIL;
    }
    debug_log_init_slot_paths();
    debug_log_fifo_shift();
    s_slot = 0;
    ESP_LOGI(TAG, "Log slot 0 (previous boot at slot 1): %s", s_slot_paths[s_slot]);
    esp_err_t err = debug_log_prepare_file();
    if (err != ESP_OK) return err;
    debug_log_activate();
    return ESP_OK;
}

esp_err_t debug_log_deinit(void)
{
    if (!is_active) return ESP_OK;
    ESP_LOGI(TAG, "Stopping debug log system");
    if (original_vprintf) {
        esp_log_set_vprintf(original_vprintf);
        original_vprintf = NULL;
    }

    // Null log_file BEFORE fclose — same ordering as pause_for_read.
    FILE* f = NULL;
    taskENTER_CRITICAL(&s_log_mux);
    f        = log_file;
    log_file = NULL;
    taskEXIT_CRITICAL(&s_log_mux);

    if (f) {
        fprintf(f, "===== Log session ended =====\n\n");
        fflush(f);
        fclose(f);
    }
    is_active = false;
    return ESP_OK;
}

bool debug_log_is_active(void)
{
    return is_active;
}

const char* debug_log_get_path(void)
{
    return is_active ? s_slot_paths[s_slot] : NULL;
}

const char* debug_log_get_slot_path(int slot)
{
    if (slot < 0 || slot >= DEBUG_LOG_SLOT_COUNT) return NULL;
    // Return path regardless of active state — used for HTTP listing.
    debug_log_init_slot_paths();  // idempotent: just fills the array
    return s_slot_paths[slot];
}

int debug_log_get_active_slot(void)
{
    return is_active ? s_slot : -1;
}

void debug_log_flush(void)
{
    FILE* f = NULL;
    taskENTER_CRITICAL(&s_log_mux);
    f = log_file;
    taskEXIT_CRITICAL(&s_log_mux);

    if (f != NULL && is_active) {
        fflush(f);
        fsync(fileno(f));
    }
}

/**
 * @brief Flush and close the log write handle to allow a concurrent read.
 *
 * FAT32 prohibits multiple open handles on the same file.  Closes the write
 * handle so a reader (e.g. the HTTP log download handler) can open the file.
 * Any log output produced between this call and debug_log_reopen() is
 * forwarded to the console only.
 *
 * Thread safety: log_file is nulled under s_log_mux BEFORE fclose so that
 * any concurrent debug_log_vprintf() call sees NULL and skips the write
 * rather than calling vfprintf() on a handle that is being closed.
 *
 * @return Path to the active log file, or NULL if logging is not active.
 */
const char* debug_log_pause_for_read(void)
{
    if (!is_active || !log_file) return NULL;

    FILE* f = NULL;
    taskENTER_CRITICAL(&s_log_mux);
    f        = log_file;
    log_file = NULL;   // NULL first — concurrent vprintf will skip, not crash
    taskEXIT_CRITICAL(&s_log_mux);

    fflush(f);
    fsync(fileno(f));
    fclose(f);
    ESP_LOGI(TAG, "Log write handle closed for read — console only until reopen");
    return s_slot_paths[s_slot];
}

/**
 * @brief Reopen the log write handle after debug_log_pause_for_read().
 *
 * No-op if logging is not active or the handle is already open.
 */
void debug_log_reopen(void)
{
    if (!is_active || log_file != NULL) return;
    FILE* f = fopen(s_slot_paths[s_slot], "a");
    if (!f) {
        ESP_LOGE(TAG, "Failed to reopen log file: %s — disabling logging",
                 s_slot_paths[s_slot]);
        is_active = false;
        return;
    }
    taskENTER_CRITICAL(&s_log_mux);
    log_file = f;
    taskEXIT_CRITICAL(&s_log_mux);
    ESP_LOGI(TAG, "Log write handle reopened: %s", s_slot_paths[s_slot]);
}

/**
 * @brief Close and reopen the log file, then write a test diagnostic block.
 */
esp_err_t debug_log_test_write(void)
{
    if (!is_active || log_file == NULL) return ESP_FAIL;

    FILE* old_f = NULL;
    taskENTER_CRITICAL(&s_log_mux);
    old_f    = log_file;
    log_file = NULL;
    taskEXIT_CRITICAL(&s_log_mux);

    fclose(old_f);
    FILE* new_f = fopen(s_slot_paths[s_slot], "a");
    if (!new_f) {
        is_active = false;
        return ESP_FAIL;
    }
    struct timeval tv;
    gettimeofday(&tv, NULL);
    debug_log_write_test_block(new_f, &tv);

    taskENTER_CRITICAL(&s_log_mux);
    log_file = new_f;
    taskEXIT_CRITICAL(&s_log_mux);
    return ESP_OK;
}
