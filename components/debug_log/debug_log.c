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
static FILE* log_file = NULL;
static bool is_active = false;
static char log_path[128] = "/sdcard/logs/debug.log";

// Original vprintf function (for console output)
static vprintf_like_t original_vprintf = NULL;

// =============================================================================
// Internal helpers
// =============================================================================

/**
 * @brief Custom vprintf that tees output to both the console and the log file.
 *
 * @param fmt   Format string.
 * @param args  Argument list (consumed internally; do not use after return).
 * @return Return value of the console vprintf call.
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
    if (log_file != NULL && is_active) {
        va_list file_args;
        va_copy(file_args, args);
        vfprintf(log_file, fmt, file_args);
        va_end(file_args);
        fflush(log_file);
        fsync(fileno(log_file));
    }
    return ret;
}

/**
 * @brief Ensure the /sdcard/logs directory exists, creating it if necessary.
 *
 * @return ESP_OK or ESP_FAIL if mkdir fails.
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
 * @brief Open the log file in append mode and write the session header.
 *
 * Sets the global @c log_file on success.
 *
 * @return ESP_OK or ESP_FAIL.
 */
static esp_err_t debug_log_open_file(void)
{
    log_file = fopen(log_path, "a");
    if (!log_file) {
        ESP_LOGE(TAG, "Failed to open log file: %s", log_path);
        return ESP_FAIL;
    }
    struct timeval tv;
    gettimeofday(&tv, NULL);
    fprintf(log_file, "\n\n===== Log session started at %ld =====\n", tv.tv_sec);
    fprintf(log_file, "Debug log initialized successfully\n");
    fflush(log_file);
    fsync(fileno(log_file));
    return ESP_OK;
}

/**
 * @brief Ensure the log directory exists and open the log file.
 *
 * @return ESP_OK or ESP_FAIL on any failure.
 */
static esp_err_t debug_log_prepare_file(void)
{
    esp_err_t err = debug_log_ensure_dir();
    if (err != ESP_OK) return err;
    return debug_log_open_file();
}

/**
 * @brief Write the test block to @p f and force a sync.
 *
 * @param f   Open file handle.
 * @param tv  Current time of the test.
 */
static void debug_log_write_test_block(FILE* f, const struct timeval* tv)
{
    fprintf(f, "[TEST] Debug log test write - if you see this, file I/O works!\n");
    fprintf(f, "[TEST] Timestamp: %ld\n", (long)tv->tv_sec);
    fprintf(f, "[TEST] is_active: %d\n", is_active);
    fprintf(f, "[TEST] log_file: %p\n", (void*)log_file);
    fprintf(f, "[TEST] original_vprintf: %p\n", (void*)original_vprintf);
    fflush(f);
    fsync(fileno(f));
}

// =============================================================================
// Public API
// =============================================================================

/**
 * @brief Initialise debug logging to the SD card.
 *
 * Opens /sdcard/logs/debug.log in append mode and redirects ESP_LOG* output
 * to the file in addition to the serial console.
 *
 * @return ESP_OK, or ESP_FAIL if the SD card is not mounted, the log directory
 *         cannot be created, or the log file cannot be opened.
 */
esp_err_t debug_log_init(void)
{
    ESP_LOGI(TAG, "Initializing debug log system");
    if (!sdcard_is_mounted()) {
        ESP_LOGW(TAG, "SD card not mounted, cannot initialize debug logging");
        return ESP_FAIL;
    }
    esp_err_t err = debug_log_prepare_file();
    if (err != ESP_OK) return err;
    is_active = true;
    original_vprintf = esp_log_set_vprintf(debug_log_vprintf);
    ESP_LOGI(TAG, "Debug logging started, writing to: %s", log_path);
    ESP_LOGI(TAG, "vprintf redirection: original=%p", original_vprintf);
    fflush(log_file);
    return ESP_OK;
}

esp_err_t debug_log_deinit(void)
{
    if (!is_active) {
        return ESP_OK;
    }
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
    return is_active ? log_path : NULL;
}

void debug_log_flush(void)
{
    if (log_file != NULL && is_active) {
        fflush(log_file);
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
 * @return Path to the log file, or NULL if logging is not active.
 */
const char* debug_log_pause_for_read(void)
{
    if (!is_active || !log_file) return NULL;
    fflush(log_file);
    fsync(fileno(log_file));
    fclose(log_file);
    log_file = NULL;
    ESP_LOGI(TAG, "Log write handle closed for read — console only until reopen");
    return log_path;
}

/**
 * @brief Reopen the log write handle after debug_log_pause_for_read().
 *
 * No-op if logging is not active or the handle is already open.
 */
void debug_log_reopen(void)
{
    if (!is_active || log_file != NULL) return;
    log_file = fopen(log_path, "a");
    if (!log_file) {
        ESP_LOGE(TAG, "Failed to reopen log file: %s — disabling logging", log_path);
        is_active = false;
        return;
    }
    ESP_LOGI(TAG, "Log write handle reopened: %s", log_path);
}

/**
 * @brief Close and reopen the log file, then write a test diagnostic block.
 *
 * Forces the file to be flushed to SD card storage and verifies that file
 * I/O is functional.
 *
 * @return ESP_OK, or ESP_FAIL if logging is not active or the reopen fails.
 */
esp_err_t debug_log_test_write(void)
{
    if (!is_active || log_file == NULL) return ESP_FAIL;
    fclose(log_file);
    log_file = fopen(log_path, "a");
    if (!log_file) {
        is_active = false;
        return ESP_FAIL;
    }
    struct timeval tv;
    gettimeofday(&tv, NULL);
    debug_log_write_test_block(log_file, &tv);
    return ESP_OK;
}
