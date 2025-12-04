// components/ota/ota.h

#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief OTA update state
 */
typedef enum {
    OTA_STATE_IDLE,
    OTA_STATE_CHECKING,
    OTA_STATE_DOWNLOADING,
    OTA_STATE_VERIFYING,
    OTA_STATE_FLASHING,
    OTA_STATE_SUCCESS,
    OTA_STATE_ERROR
} ota_state_t;

/**
 * @brief OTA status information
 */
typedef struct {
    ota_state_t state;
    int progress_percent;
    size_t downloaded_bytes;
    size_t total_bytes;
    char error_msg[128];
} ota_status_t;

/**
 * @brief OTA progress callback function
 *
 * @param status Current OTA status
 * @param user_data User-provided data pointer
 */
typedef void (*ota_progress_cb_t)(const ota_status_t* status, void* user_data);

/**
 * @brief Initialize OTA subsystem
 *
 * Must be called before any other OTA functions.
 * Checks partition table and current boot partition.
 *
 * @return ESP_OK on success
 */
esp_err_t ota_init(void);

/**
 * @brief Get current firmware version
 *
 * @return Version string from app descriptor
 */
const char* ota_get_current_version(void);

/**
 * @brief Get current running partition name
 *
 * @return Partition name (e.g., "factory", "ota_0", "ota_1")
 */
const char* ota_get_running_partition(void);

/**
 * @brief Check for firmware update from server
 *
 * Connects to server and checks if firmware is available.
 * Does not download or flash anything.
 *
 * @param server_url Base URL of firmware server (e.g., "http://192.168.1.100:8000")
 * @return ESP_OK if update available and reachable, ESP_ERR_NOT_FOUND if no update
 */
esp_err_t ota_check_update(const char* server_url);

/**
 * @brief Perform OTA update from server
 *
 * Downloads firmware from server, verifies it, and flashes to OTA partition.
 * Device will reboot automatically on success.
 *
 * @param server_url Base URL of firmware server
 * @param progress_cb Optional callback for progress updates (can be NULL)
 * @param user_data Optional user data passed to callback (can be NULL)
 * @return ESP_OK on success (will reboot), error code on failure
 */
esp_err_t ota_perform_update(const char* server_url,
                              ota_progress_cb_t progress_cb,
                              void* user_data);

/**
 * @brief Mark current firmware as valid
 *
 * Prevents automatic rollback to previous firmware.
 * Call this after verifying new firmware works correctly.
 *
 * @return ESP_OK on success
 */
esp_err_t ota_mark_app_valid(void);

/**
 * @brief Get last OTA error message
 *
 * @return Error message string (static buffer, no need to free)
 */
const char* ota_get_last_error(void);

/**
 * @brief Rollback to previous firmware
 *
 * Manually trigger rollback to the previous OTA partition.
 * Device will reboot after successful rollback setup.
 *
 * @return ESP_OK on success (will reboot), error code on failure
 */
esp_err_t ota_rollback(void);

#ifdef __cplusplus
}
#endif
