// components/ota/ota.c

#include "ota.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_app_format.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdarg.h>
#include <string.h>

static const char* TAG = "ota";

// =============================================================================
// Module state
// =============================================================================

static char                   s_last_error[128]   = {0};
static const esp_partition_t* s_running_partition  = NULL;
static esp_app_desc_t         s_running_app_info;

static esp_ota_handle_t       s_ota_handle        = 0;
static const esp_partition_t* s_update_partition  = NULL;
static ota_status_t           s_status            = { .state = OTA_STATE_IDLE };

// =============================================================================
// Private: error reporting
// =============================================================================

/** @brief Format and store an error message; also emit it at ESP_LOGE level. */
static void set_error(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vsnprintf(s_last_error, sizeof(s_last_error), fmt, args);
    va_end(args);
    ESP_LOGE(TAG, "%s", s_last_error);
}

// =============================================================================
// Private: init helpers
// =============================================================================

/**
 * @brief Log the current firmware version and compilation date.
 *
 * @param err Return code from esp_ota_get_partition_description().
 */
static void ota_log_app_description(esp_err_t err) {
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to get app description: %d", (int)err);
        return;
    }
    ESP_LOGI(TAG, "Current firmware version: %s", s_running_app_info.version);
    ESP_LOGI(TAG, "Compiled: %s %s", s_running_app_info.date, s_running_app_info.time);
}

/**
 * @brief Log the OTA state of the running partition (pending, valid, invalid).
 *
 * Emits a warning if the image is pending verification so the operator is
 * reminded to call ota_mark_app_valid().
 */
static void ota_log_partition_state(void) {
    esp_ota_img_states_t ota_state;
    esp_err_t err = esp_ota_get_state_partition(s_running_partition, &ota_state);
    if (err != ESP_OK) return;
    if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
        ESP_LOGW(TAG, "New OTA image pending verification!");
        ESP_LOGW(TAG, "Call ota_mark_app_valid() after verifying firmware");
    } else if (ota_state == ESP_OTA_IMG_VALID) {
        ESP_LOGI(TAG, "OTA image marked as valid");
    } else if (ota_state == ESP_OTA_IMG_INVALID) {
        ESP_LOGW(TAG, "OTA image marked as invalid");
    }
}

// =============================================================================
// Public: init / info
// =============================================================================

esp_err_t ota_init(void) {
    s_running_partition = esp_ota_get_running_partition();
    if (s_running_partition == NULL) {
        ESP_LOGE(TAG, "Failed to get running partition");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Running partition: %s (type=%d subtype=%d offset=0x%lx size=0x%lx)",
             s_running_partition->label,
             s_running_partition->type,
             s_running_partition->subtype,
             (unsigned long)s_running_partition->address,
             (unsigned long)s_running_partition->size);
    esp_err_t err = esp_ota_get_partition_description(s_running_partition, &s_running_app_info);
    ota_log_app_description(err);
    ota_log_partition_state();
    return ESP_OK;
}

const char* ota_get_current_version(void) {
    return s_running_app_info.version;
}

const char* ota_get_running_partition(void) {
    return s_running_partition ? s_running_partition->label : "unknown";
}

const char* ota_get_last_error(void) {
    return s_last_error;
}

ota_status_t ota_get_status(void) {
    return s_status;
}

// =============================================================================
// Private: push OTA helpers
// =============================================================================

/**
 * @brief Select the next OTA update partition.
 *
 * @return ESP_OK on success; ESP_FAIL if no OTA partition is available.
 */
static esp_err_t ota_push_get_partition(void) {
    s_update_partition = esp_ota_get_next_update_partition(NULL);
    if (!s_update_partition) {
        set_error("No OTA update partition available");
        return ESP_FAIL;
    }
    return ESP_OK;
}

/**
 * @brief Call esp_ota_begin() to open a write handle for the update partition.
 *
 * @param image_size Expected image size (OTA_SIZE_UNKNOWN to skip pre-erase).
 * @return ESP_OK on success.
 */
static esp_err_t ota_push_open_handle(size_t image_size) {
    esp_err_t err = esp_ota_begin(s_update_partition, image_size, &s_ota_handle);
    if (err != ESP_OK) set_error("esp_ota_begin failed: %d", (int)err);
    return err;
}

/**
 * @brief Initialise the in-progress status fields for a new session.
 *
 * @param image_size Total firmware size in bytes.
 */
static void ota_push_init_status(size_t image_size) {
    s_status.state         = OTA_STATE_RECEIVING;
    s_status.bytes_written = 0;
    s_status.total_bytes   = image_size;
    s_status.error_msg[0]  = '\0';
}

/**
 * @brief Finalise the write handle via esp_ota_end() and clear it.
 *
 * @return ESP_OK on success.
 */
static esp_err_t ota_push_end_handle(void) {
    esp_err_t err = esp_ota_end(s_ota_handle);
    s_ota_handle = 0;
    if (err != ESP_OK) set_error("esp_ota_end failed: %d", (int)err);
    return err;
}

/**
 * @brief Set the update partition as next boot target.
 *
 * @return ESP_OK on success.
 */
static esp_err_t ota_push_set_boot(void) {
    esp_err_t err = esp_ota_set_boot_partition(s_update_partition);
    if (err != ESP_OK) set_error("esp_ota_set_boot_partition failed: %d", (int)err);
    return err;
}

/**
 * @brief Transition to REBOOTING state, wait briefly, then restart.
 *
 * Does not return.  Called from both ota_push_finish() (private path) and
 * ota_push_commit() (public path after HTTP response is sent).
 */
static void ota_push_reboot_internal(void) {
    ESP_LOGI(TAG, "OTA complete — rebooting in 1 s...");
    s_status.state = OTA_STATE_REBOOTING;
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
}

/**
 * @brief Abort the active OTA write handle and clear it.
 */
static void ota_push_clear_handle(void) {
    if (s_ota_handle) {
        esp_ota_abort(s_ota_handle);
        s_ota_handle = 0;
    }
    s_update_partition = NULL;
}

/**
 * @brief Reset all push-OTA state back to IDLE.
 */
static void ota_push_reset_to_idle(void) {
    s_status.state         = OTA_STATE_IDLE;
    s_status.bytes_written = 0;
    s_status.total_bytes   = 0;
    s_status.error_msg[0]  = '\0';
}

/**
 * @brief Select update partition and open an OTA write handle.
 *
 * Combines ota_push_get_partition() + ota_push_open_handle() so that
 * ota_push_begin() stays within the return-count threshold.
 *
 * @param image_size Expected image size passed to esp_ota_begin().
 * @return ESP_OK on success.
 */
static esp_err_t ota_push_prepare(size_t image_size) {
    esp_err_t err = ota_push_get_partition();
    if (err != ESP_OK) return err;
    return ota_push_open_handle(image_size);
}

/**
 * @brief Verify the written image (esp_ota_end) then set the boot partition.
 *
 * Extracted so that ota_push_finish() stays within the return-count threshold.
 *
 * @return ESP_OK on success.
 */
static esp_err_t ota_push_verify_and_set_boot(void) {
    esp_err_t err = ota_push_end_handle();
    if (err != ESP_OK) return err;
    return ota_push_set_boot();
}

// =============================================================================
// Public: push OTA
// =============================================================================

esp_err_t ota_push_begin(size_t image_size) {
    if (s_status.state != OTA_STATE_IDLE) {
        set_error("OTA already in progress (state=%d)", (int)s_status.state);
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = ota_push_prepare(image_size);
    if (err != ESP_OK) { s_status.state = OTA_STATE_ERROR; return err; }
    ota_push_init_status(image_size);
    ESP_LOGI(TAG, "OTA push begin: partition=%s size=%zu",
             s_update_partition->label, image_size);
    return ESP_OK;
}

esp_err_t ota_push_write(const void* data, size_t len) {
    if (s_status.state != OTA_STATE_RECEIVING) {
        set_error("ota_push_write in wrong state: %d", (int)s_status.state);
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t err = esp_ota_write(s_ota_handle, data, len);
    if (err != ESP_OK) {
        set_error("esp_ota_write failed: %d", (int)err);
        s_status.state = OTA_STATE_ERROR;
        return err;
    }
    s_status.bytes_written += len;
    return ESP_OK;
}

esp_err_t ota_push_finish(void) {
    if (s_status.state != OTA_STATE_RECEIVING) {
        set_error("ota_push_finish in wrong state: %d", (int)s_status.state);
        return ESP_ERR_INVALID_STATE;
    }
    s_status.state = OTA_STATE_VALIDATING;
    ESP_LOGI(TAG, "Validating OTA image (%zu bytes written)", s_status.bytes_written);
    esp_err_t err = ota_push_verify_and_set_boot();
    if (err != ESP_OK) { s_status.state = OTA_STATE_ERROR; return err; }
    ESP_LOGI(TAG, "OTA image validated — call ota_push_commit() to reboot");
    return ESP_OK;
}

void ota_push_commit(void) {
    if (s_status.state != OTA_STATE_VALIDATING) {
        ESP_LOGW(TAG, "ota_push_commit called in wrong state %d — ignoring",
                 (int)s_status.state);
        return;
    }
    ota_push_reboot_internal();  // does not return
}

void ota_push_abort(void) {
    if (s_status.state == OTA_STATE_IDLE) return;
    if (s_status.state == OTA_STATE_VALIDATING ||
        s_status.state == OTA_STATE_REBOOTING) {
        ESP_LOGW(TAG, "Cannot abort OTA in state %d — ignoring", (int)s_status.state);
        return;
    }
    ESP_LOGW(TAG, "Aborting OTA push (state=%d, %zu/%zu bytes)",
             (int)s_status.state, s_status.bytes_written, s_status.total_bytes);
    ota_push_clear_handle();
    ota_push_reset_to_idle();
}

// =============================================================================
// Private: ota_mark_app_valid helpers
// =============================================================================

/**
 * @brief Mark the running OTA image as valid and cancel automatic rollback.
 *
 * @return ESP_OK on success.
 */
static esp_err_t ota_confirm_pending(void) {
    esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "OTA image marked as valid, rollback cancelled");
    } else {
        ESP_LOGE(TAG, "Failed to mark app valid: %d", (int)err);
    }
    return err;
}

// =============================================================================
// Public: ota_mark_app_valid
// =============================================================================

esp_err_t ota_mark_app_valid(void) {
    esp_ota_img_states_t ota_state;
    esp_err_t err = esp_ota_get_state_partition(s_running_partition, &ota_state);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to get OTA state: %d", (int)err);
        return err;
    }
    // Call cancel_rollback for both NEW and PENDING_VERIFY — both represent
    // an unconfirmed partition.  NEW is what esp_ota_set_boot_partition() leaves
    // after a push-OTA; PENDING_VERIFY is what the bootloader sets on first boot
    // when CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y.  If we only handle
    // PENDING_VERIFY, NEW partitions never get confirmed and the bootloader
    // eventually rolls back to the other slot.
    if (ota_state == ESP_OTA_IMG_PENDING_VERIFY || ota_state == ESP_OTA_IMG_NEW) {
        return ota_confirm_pending();
    }
    if (ota_state == ESP_OTA_IMG_VALID) {
        ESP_LOGI(TAG, "OTA image already marked as valid");
    } else {
        ESP_LOGW(TAG, "Unexpected OTA state: %d", ota_state);
        err = ESP_FAIL;
    }
    return err;
}

// =============================================================================
// Private: rollback helpers
// =============================================================================

/**
 * @brief Set a partition as the next boot target and restart the device.
 *
 * Only returns on failure; a successful set triggers esp_restart().
 *
 * @param partition Target partition for rollback.
 * @return Error code on failure.
 */
static esp_err_t ota_set_boot_and_reboot(const esp_partition_t* partition) {
    ESP_LOGI(TAG, "Rolling back to partition: %s", partition->label);
    esp_err_t err = esp_ota_set_boot_partition(partition);
    if (err != ESP_OK) {
        set_error("Failed to set boot partition: %d", (int)err);
        return err;
    }
    ESP_LOGI(TAG, "Rollback configured, rebooting...");
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
    return ESP_OK;  // unreachable
}

// =============================================================================
// Public: ota_rollback
// =============================================================================

esp_err_t ota_rollback(void) {
    ESP_LOGW(TAG, "Manual rollback requested");
    const esp_partition_t* partition = esp_ota_get_last_invalid_partition();
    if (partition == NULL) {
        set_error("No previous partition available for rollback");
        return ESP_FAIL;
    }
    return ota_set_boot_and_reboot(partition);
}
