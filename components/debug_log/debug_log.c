// components/debug_log/debug_log.c

#include "debug_log.h"
#include "sdcard.h"
#include "esp_log.h"
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

static FILE*          log_file        = NULL;
static bool           is_active       = false;
static bool           s_in_vprintf    = false;
static int            s_slot          = 0;
static size_t         s_bytes_written = 0;
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
 * @brief Find the non-full slot with the highest mtime.
 *
 * @param st     Array of stat results for each slot.
 * @param exists Array of booleans indicating which slots exist.
 * @return Slot index, or -1 if all slots are full.
 */
static int debug_log_find_newest_nonfull(const struct stat* st, const bool* exists)
{
    int    best       = -1;
    time_t best_mtime = -1;
    for (int i = 0; i < DEBUG_LOG_SLOT_COUNT; i++) {
        bool non_full = (!exists[i] || st[i].st_size < (off_t)DEBUG_LOG_MAX_SIZE);
        if (!non_full) continue;
        time_t mtime = exists[i] ? st[i].st_mtime : 0;
        if (best == -1 || mtime > best_mtime) {
            best       = i;
            best_mtime = mtime;
        }
    }
    return best;
}

/**
 * @brief Find the oldest slot by mtime (smallest value) for overwrite.
 *
 * @param st  Array of stat results for each slot.
 * @return Slot index of the oldest slot.
 */
static int debug_log_find_oldest(const struct stat* st)
{
    int oldest = 0;
    for (int i = 1; i < DEBUG_LOG_SLOT_COUNT; i++) {
        if (st[i].st_mtime < st[oldest].st_mtime) oldest = i;
    }
    return oldest;
}

/**
 * @brief Scan all slots and return the best one to use on init.
 *
 * Prefers the most-recently-modified non-full slot.  If all slots are full
 * or missing, returns the oldest (smallest mtime) for overwrite.
 */
static int debug_log_select_slot(void)
{
    struct stat st[DEBUG_LOG_SLOT_COUNT];
    bool        exists[DEBUG_LOG_SLOT_COUNT];
    for (int i = 0; i < DEBUG_LOG_SLOT_COUNT; i++) {
        exists[i] = (stat(s_slot_paths[i], &st[i]) == 0);
    }
    int best = debug_log_find_newest_nonfull(st, exists);
    return (best != -1) ? best : debug_log_find_oldest(st);
}

/**
 * @brief Write the rotation footer and close the current log file handle.
 */
static void debug_log_close_slot(void)
{
    fprintf(log_file,
            "\n===== LOG ROTATION: slot %d full (%.1f MB), rotating =====\n",
            s_slot, (double)s_bytes_written / (1024.0 * 1024.0));
    fflush(log_file);
    fsync(fileno(log_file));
    fclose(log_file);
    log_file = NULL;
}

/**
 * @brief Advance the ring index, wipe the next slot, and open a fresh file.
 *
 * Sets is_active to false on failure. Any ESP_LOG calls here go to console
 * only — this runs inside the s_in_vprintf guard.
 */
static void debug_log_open_next_slot(void)
{
    s_slot = (s_slot + 1) % DEBUG_LOG_SLOT_COUNT;
    unlink(s_slot_paths[s_slot]);
    log_file = fopen(s_slot_paths[s_slot], "w");
    if (!log_file) {
        ESP_LOGE(TAG, "Log rotation failed: cannot open slot %d: %s",
                 s_slot, s_slot_paths[s_slot]);
        is_active = false;
        return;
    }
    s_bytes_written = 0;
    struct timeval tv;
    gettimeofday(&tv, NULL);
    fprintf(log_file,
            "===== LOG ROTATION: new slot %d started at %ld =====\n\n",
            s_slot, tv.tv_sec);
    fflush(log_file);
    fsync(fileno(log_file));
    ESP_LOGI(TAG, "Log rotated to slot %d: %s", s_slot, s_slot_paths[s_slot]);
}

/**
 * @brief Close current slot, advance ring index, delete next slot, open fresh.
 *
 * Called from within debug_log_vprintf (s_in_vprintf is true), so any
 * ESP_LOG* calls here go to console only — no reentrancy risk.
 */
static void debug_log_rotate(void)
{
    debug_log_close_slot();
    debug_log_open_next_slot();
}

/**
 * @brief Write @p fmt/@p args to the log file, tracking bytes and rotating
 *        if the slot limit is reached.
 *
 * Must only be called with s_in_vprintf set to prevent reentrancy.
 *
 * @param fmt  Format string.
 * @param args Variadic argument list (a va_copy is made internally).
 */
static void debug_log_write_to_file(const char* fmt, va_list args)
{
    va_list file_args;
    va_copy(file_args, args);
    int n = vfprintf(log_file, fmt, file_args);
    va_end(file_args);
    if (n > 0) s_bytes_written += (size_t)n;
    fflush(log_file);
    fsync(fileno(log_file));
    if (s_bytes_written >= DEBUG_LOG_MAX_SIZE) {
        debug_log_rotate();
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
    if (log_file != NULL && is_active && !s_in_vprintf) {
        s_in_vprintf = true;
        debug_log_write_to_file(fmt, args);
        s_in_vprintf = false;
    }
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
 * @brief Open the active slot in append mode and write the session header.
 */
static esp_err_t debug_log_open_file(void)
{
    log_file = fopen(s_slot_paths[s_slot], "a");
    if (!log_file) {
        ESP_LOGE(TAG, "Failed to open log file: %s", s_slot_paths[s_slot]);
        return ESP_FAIL;
    }
    fseek(log_file, 0, SEEK_END);
    s_bytes_written = (size_t)ftell(log_file);
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
    s_slot = debug_log_select_slot();
    ESP_LOGI(TAG, "Selected log slot %d: %s", s_slot, s_slot_paths[s_slot]);
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
    if (log_file != NULL) {
        fprintf(log_file, "===== Log session ended =====\n\n");
        fflush(log_file);
        fclose(log_file);
        log_file = NULL;
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
    if (log_file != NULL && is_active) {
        fflush(log_file);
        fsync(fileno(log_file));
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
 * @return Path to the active log file, or NULL if logging is not active.
 */
const char* debug_log_pause_for_read(void)
{
    if (!is_active || !log_file) return NULL;
    fflush(log_file);
    fsync(fileno(log_file));
    fclose(log_file);
    log_file = NULL;
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
    log_file = fopen(s_slot_paths[s_slot], "a");
    if (!log_file) {
        ESP_LOGE(TAG, "Failed to reopen log file: %s — disabling logging",
                 s_slot_paths[s_slot]);
        is_active = false;
        return;
    }
    ESP_LOGI(TAG, "Log write handle reopened: %s", s_slot_paths[s_slot]);
}

/**
 * @brief Close and reopen the log file, then write a test diagnostic block.
 */
esp_err_t debug_log_test_write(void)
{
    if (!is_active || log_file == NULL) return ESP_FAIL;
    fclose(log_file);
    log_file = fopen(s_slot_paths[s_slot], "a");
    if (!log_file) {
        is_active = false;
        return ESP_FAIL;
    }
    struct timeval tv;
    gettimeofday(&tv, NULL);
    debug_log_write_test_block(log_file, &tv);
    return ESP_OK;
}
