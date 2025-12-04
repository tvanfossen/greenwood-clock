// components/debug_log/debug_log.h

#ifndef DEBUG_LOG_H
#define DEBUG_LOG_H

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize debug log system
 *
 * Redirects ESP log output to both console and SD card file.
 * Creates /sdcard/logs/debug.log
 *
 * @return ESP_OK on success
 */
esp_err_t debug_log_init(void);

/**
 * @brief Stop debug log system and close log file
 *
 * @return ESP_OK on success
 */
esp_err_t debug_log_deinit(void);

/**
 * @brief Check if debug logging is active
 *
 * @return true if logging to SD card
 */
bool debug_log_is_active(void);

/**
 * @brief Get current log file path
 *
 * @return Path to current log file, or NULL if not active
 */
const char* debug_log_get_path(void);

/**
 * @brief Flush log buffer to SD card
 */
void debug_log_flush(void);

/**
 * @brief Write a test message directly to log file (for debugging)
 *
 * @return ESP_OK on success
 */
esp_err_t debug_log_test_write(void);

#ifdef __cplusplus
}
#endif

#endif // DEBUG_LOG_H
