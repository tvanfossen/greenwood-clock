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

/**
 * @brief Custom vprintf that writes to both console and log file
 */
static int debug_log_vprintf(const char* fmt, va_list args) {
    int ret = 0;

    // Write to console - make a copy of args
    if (original_vprintf) {
        va_list console_args;
        va_copy(console_args, args);
        ret = original_vprintf(fmt, console_args);
        va_end(console_args);
    }

    // Write to log file - make ANOTHER copy of args (va_list can only be traversed once!)
    if (log_file != NULL && is_active) {
        va_list file_args;
        va_copy(file_args, args);
        vfprintf(log_file, fmt, file_args);
        va_end(file_args);

        // ALWAYS flush immediately to ensure logs are written to SD card
        fflush(log_file);
        fsync(fileno(log_file));
    }

    return ret;
}

esp_err_t debug_log_init(void) {
    ESP_LOGI(TAG, "Initializing debug log system");

    // Check if SD card is mounted
    if (!sdcard_is_mounted()) {
        ESP_LOGW(TAG, "SD card not mounted, cannot initialize debug logging");
        return ESP_FAIL;
    }

    // Create logs directory if it doesn't exist
    const char* log_dir = "/sdcard/logs";
    struct stat st;
    if (stat(log_dir, &st) != 0) {
        ESP_LOGI(TAG, "Creating logs directory: %s", log_dir);
        if (mkdir(log_dir, 0755) != 0) {
            ESP_LOGE(TAG, "Failed to create logs directory");
            return ESP_FAIL;
        }
    }

    // Open log file in append mode
    log_file = fopen(log_path, "a");
    if (log_file == NULL) {
        ESP_LOGE(TAG, "Failed to open log file: %s", log_path);
        return ESP_FAIL;
    }

    // Write header to log file
    struct timeval tv;
    gettimeofday(&tv, NULL);
    fprintf(log_file, "\n\n===== Log session started at %ld =====\n", tv.tv_sec);
    fprintf(log_file, "Debug log initialized successfully\n");
    fflush(log_file);
    fsync(fileno(log_file));

    // Mark as active BEFORE setting vprintf (so vprintf can use it)
    is_active = true;

    // Redirect ESP log output to our custom vprintf
    original_vprintf = esp_log_set_vprintf(debug_log_vprintf);

    // Log via ESP_LOG to test if redirection works
    ESP_LOGI(TAG, "Debug logging started, writing to: %s", log_path);
    ESP_LOGI(TAG, "vprintf redirection: original=%p", original_vprintf);

    // Flush again to ensure init messages are written
    fflush(log_file);

    return ESP_OK;
}

esp_err_t debug_log_deinit(void) {
    if (!is_active) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Stopping debug log system");

    // Restore original vprintf
    if (original_vprintf) {
        esp_log_set_vprintf(original_vprintf);
        original_vprintf = NULL;
    }

    // Close log file
    if (log_file != NULL) {
        fprintf(log_file, "===== Log session ended =====\n\n");
        fflush(log_file);
        fclose(log_file);
        log_file = NULL;
    }

    is_active = false;
    return ESP_OK;
}

bool debug_log_is_active(void) {
    return is_active;
}

const char* debug_log_get_path(void) {
    return is_active ? log_path : NULL;
}

void debug_log_flush(void) {
    if (log_file != NULL && is_active) {
        fflush(log_file);
    }
}

esp_err_t debug_log_test_write(void) {
    if (!is_active || log_file == NULL) {
        return ESP_FAIL;
    }

    // Try closing and reopening the log file to force a flush
    fclose(log_file);

    log_file = fopen(log_path, "a");
    if (log_file == NULL) {
        is_active = false;
        return ESP_FAIL;
    }

    struct timeval tv;
    gettimeofday(&tv, NULL);

    FILE* f = log_file;
    void* file_ptr = (void*)log_file;
    void* vprintf_ptr = (void*)original_vprintf;

    fprintf(f, "[TEST] Debug log test write - if you see this, file I/O works!\n");
    fprintf(f, "[TEST] Timestamp: %ld\n", (long)tv.tv_sec);
    fprintf(f, "[TEST] is_active: %d\n", is_active);
    fprintf(f, "[TEST] log_file: %p\n", file_ptr);
    fprintf(f, "[TEST] original_vprintf: %p\n", vprintf_ptr);
    fflush(f);

    // Force sync to disk
    fsync(fileno(f));

    return ESP_OK;
}
