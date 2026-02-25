// components/debug_log/debug_log.h

#ifndef DEBUG_LOG_H
#define DEBUG_LOG_H

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize debug log system.
 *
 * Selects the best available log slot (3×5 MB ring buffer), opens it in
 * append mode, and redirects ESP_LOG* output to both console and SD card.
 *
 * @return ESP_OK on success.
 */
esp_err_t debug_log_init(void);

/**
 * @brief Stop debug log system and close log file.
 *
 * @return ESP_OK on success.
 */
esp_err_t debug_log_deinit(void);

/**
 * @brief Check if debug logging is active.
 *
 * @return true if logging to SD card.
 */
bool debug_log_is_active(void);

/**
 * @brief Get the active log file path.
 *
 * @return Path to current log slot, or NULL if not active.
 */
const char* debug_log_get_path(void);

/**
 * @brief Get the filesystem path for a specific log slot.
 *
 * May be called even when logging is inactive (e.g. for HTTP file listing).
 *
 * @param slot  Slot index 0–2.
 * @return Path string, or NULL if slot is out of range.
 */
const char* debug_log_get_slot_path(int slot);

/**
 * @brief Get the currently active log slot index.
 *
 * @return Slot index (0–2), or -1 if logging is not active.
 */
int debug_log_get_active_slot(void);

/**
 * @brief Flush log buffer to SD card (fflush + fsync).
 */
void debug_log_flush(void);

/**
 * @brief Write a test message directly to log file (for debugging).
 *
 * @return ESP_OK on success.
 */
esp_err_t debug_log_test_write(void);

/**
 * @brief Flush and close the log write handle to allow a concurrent read.
 *
 * FAT32 does not support multiple file handles on the same file.  Call this
 * before opening the log for reading (e.g. to serve it over HTTP), then call
 * debug_log_reopen() when the read is complete.
 *
 * Any ESP_LOG* calls between pause and reopen are forwarded to the console
 * only (file write is skipped while the handle is closed).
 *
 * @return Path to the log file, or NULL if debug logging is not active.
 */
const char* debug_log_pause_for_read(void);

/**
 * @brief Reopen the log write handle after debug_log_pause_for_read().
 *
 * No-op if logging is not active or the handle is already open.
 */
void debug_log_reopen(void);

#ifdef __cplusplus
}
#endif

#endif // DEBUG_LOG_H
