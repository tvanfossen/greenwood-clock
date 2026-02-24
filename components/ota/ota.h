// components/ota/ota.h

#pragma once

#include "esp_err.h"
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief OTA update state (push-based flow)
 */
typedef enum {
    OTA_STATE_IDLE,        /**< No OTA in progress */
    OTA_STATE_RECEIVING,   /**< Receiving and writing firmware chunks */
    OTA_STATE_VALIDATING,  /**< esp_ota_end() verification in progress */
    OTA_STATE_REBOOTING,   /**< About to call esp_restart() */
    OTA_STATE_ERROR        /**< Fatal error; check error_msg */
} ota_state_t;

/**
 * @brief Snapshot of push-OTA progress (returned by ota_get_status)
 */
typedef struct {
    ota_state_t state;
    size_t      bytes_written;
    size_t      total_bytes;
    char        error_msg[128];
} ota_status_t;

// =============================================================================
// Init / info
// =============================================================================

/**
 * @brief Initialize OTA subsystem.
 *
 * Identifies the running partition and logs firmware version + OTA state.
 * Must be called before any other OTA function.
 *
 * @return ESP_OK on success.
 */
esp_err_t ota_init(void);

/**
 * @brief Get current firmware version string from app descriptor.
 */
const char* ota_get_current_version(void);

/**
 * @brief Get running partition name (e.g. "factory", "ota_0").
 */
const char* ota_get_running_partition(void);

/**
 * @brief Get last OTA error message (static buffer, do not free).
 */
const char* ota_get_last_error(void);

/**
 * @brief Get current push-OTA status snapshot.
 */
ota_status_t ota_get_status(void);

/**
 * @brief Mark current firmware as valid, cancelling automatic rollback.
 *
 * Call after verifying the new firmware boots and operates correctly.
 *
 * @return ESP_OK on success.
 */
esp_err_t ota_mark_app_valid(void);

/**
 * @brief Rollback to previous OTA partition and reboot.
 *
 * Only returns on failure; success path calls esp_restart().
 *
 * @return Error code on failure.
 */
esp_err_t ota_rollback(void);

// =============================================================================
// Push OTA — called by the HTTP handler
// =============================================================================

/**
 * @brief Begin a push OTA session.
 *
 * Selects the next OTA partition and calls esp_ota_begin().
 * Must be called once before any ota_push_write() calls.
 *
 * @param image_size Expected firmware image size in bytes (OTA_SIZE_UNKNOWN to skip).
 * @return ESP_OK on success; ESP_ERR_INVALID_STATE if an OTA is already in progress.
 */
esp_err_t ota_push_begin(size_t image_size);

/**
 * @brief Write a firmware chunk to the OTA partition.
 *
 * Wraps esp_ota_write().  Must only be called between ota_push_begin()
 * and ota_push_finish() / ota_push_abort().
 *
 * @param data Pointer to firmware bytes.
 * @param len  Number of bytes to write.
 * @return ESP_OK on success.
 */
esp_err_t ota_push_write(const void* data, size_t len);

/**
 * @brief Validate the OTA image and set the boot partition.
 *
 * Calls esp_ota_end() then esp_ota_set_boot_partition().  Does NOT reboot —
 * call ota_push_commit() after sending any HTTP response to the client.
 *
 * @return ESP_OK on success; error code on failure (state → ERROR).
 */
esp_err_t ota_push_finish(void);

/**
 * @brief Commit the flashed image and reboot the device.
 *
 * Transitions state to REBOOTING, waits 1 second, then calls esp_restart().
 * Must only be called after a successful ota_push_finish().
 * Does not return.
 */
void ota_push_commit(void);

/**
 * @brief Abort an in-progress push OTA session.
 *
 * Calls esp_ota_abort() and resets state to IDLE.
 * No-op if already IDLE, or if in VALIDATING/REBOOTING state.
 */
void ota_push_abort(void);

#ifdef __cplusplus
}
#endif
