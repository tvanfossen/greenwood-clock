// components/ota/ota.c

#include "ota.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"
#include "esp_app_format.h"
#include "esp_system.h"
#include <string.h>

static const char* TAG = "ota";

// OTA configuration
#define OTA_FIRMWARE_PATH "/greenwood-clock.bin"
#define OTA_CONNECT_TIMEOUT_MS 10000
#define OTA_READ_TIMEOUT_MS 30000
#define OTA_BUFFER_SIZE 4096

// Static error message buffer
static char s_last_error[128] = {0};

// Running partition info
static const esp_partition_t* s_running_partition = NULL;
static esp_app_desc_t s_running_app_info;

esp_err_t ota_init(void) {
    // Get running partition
    s_running_partition = esp_ota_get_running_partition();
    if (s_running_partition == NULL) {
        ESP_LOGE(TAG, "Failed to get running partition");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Running partition: %s (type=%d, subtype=%d, offset=0x%lx, size=0x%lx)",
             s_running_partition->label,
             s_running_partition->type,
             s_running_partition->subtype,
             (unsigned long)s_running_partition->address,
             (unsigned long)s_running_partition->size);

    // Get app info
    esp_err_t err = esp_ota_get_partition_description(s_running_partition, &s_running_app_info);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to get app description: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "Current firmware version: %s", s_running_app_info.version);
        ESP_LOGI(TAG, "Compiled: %s %s", s_running_app_info.date, s_running_app_info.time);
    }

    // Check for pending verification (new OTA image)
    esp_ota_img_states_t ota_state;
    err = esp_ota_get_state_partition(s_running_partition, &ota_state);
    if (err == ESP_OK) {
        if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
            ESP_LOGW(TAG, "New OTA image pending verification!");
            ESP_LOGW(TAG, "Call ota_mark_app_valid() after verifying firmware");
        } else if (ota_state == ESP_OTA_IMG_VALID) {
            ESP_LOGI(TAG, "OTA image marked as valid");
        } else if (ota_state == ESP_OTA_IMG_INVALID) {
            ESP_LOGW(TAG, "OTA image marked as invalid");
        }
    }

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

static void set_error(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vsnprintf(s_last_error, sizeof(s_last_error), fmt, args);
    va_end(args);
    ESP_LOGE(TAG, "%s", s_last_error);
}

esp_err_t ota_check_update(const char* server_url) {
    if (server_url == NULL) {
        set_error("Server URL is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    // Build firmware URL
    char url[256];
    snprintf(url, sizeof(url), "%s%s", server_url, OTA_FIRMWARE_PATH);

    ESP_LOGI(TAG, "Checking for update at: %s", url);

    // Create HTTP client to check if firmware exists
    esp_http_client_config_t config = {
        .url = url,
        .timeout_ms = OTA_CONNECT_TIMEOUT_MS,
        .method = HTTP_METHOD_HEAD,  // Just check if file exists
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        set_error("Failed to initialize HTTP client");
        return ESP_FAIL;
    }

    esp_err_t err = esp_http_client_perform(client);
    int status_code = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK) {
        set_error("HTTP request failed: %s", esp_err_to_name(err));
        return err;
    }

    if (status_code == 200) {
        ESP_LOGI(TAG, "Firmware available on server");
        return ESP_OK;
    } else {
        set_error("Server returned HTTP %d", status_code);
        return ESP_ERR_NOT_FOUND;
    }
}

esp_err_t ota_perform_update(const char* server_url,
                              ota_progress_cb_t progress_cb,
                              void* user_data) {
    if (server_url == NULL) {
        set_error("Server URL is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    // Build firmware URL
    char url[256];
    snprintf(url, sizeof(url), "%s%s", server_url, OTA_FIRMWARE_PATH);

    ESP_LOGI(TAG, "Starting OTA update from: %s", url);

    // Report checking state
    if (progress_cb) {
        ota_status_t status = {
            .state = OTA_STATE_CHECKING,
            .progress_percent = 0,
            .downloaded_bytes = 0,
            .total_bytes = 0,
        };
        progress_cb(&status, user_data);
    }

    // Configure HTTP client (for plain HTTP, not HTTPS)
    esp_http_client_config_t http_config = {
        .url = url,
        .timeout_ms = OTA_READ_TIMEOUT_MS,
        .keep_alive_enable = true,
        .buffer_size = OTA_BUFFER_SIZE,
        .skip_cert_common_name_check = true,
        .use_global_ca_store = false,  // Disable CA store for HTTP
    };

    // Configure OTA
    esp_https_ota_config_t ota_config = {
        .http_config = &http_config,
    };

    esp_https_ota_handle_t ota_handle = NULL;
    esp_err_t err = esp_https_ota_begin(&ota_config, &ota_handle);
    if (err != ESP_OK) {
        set_error("OTA begin failed: %s", esp_err_to_name(err));
        return err;
    }

    // Get image size
    int image_size = esp_https_ota_get_image_size(ota_handle);
    ESP_LOGI(TAG, "OTA image size: %d bytes", image_size);

    // Report downloading state
    if (progress_cb) {
        ota_status_t status = {
            .state = OTA_STATE_DOWNLOADING,
            .progress_percent = 0,
            .downloaded_bytes = 0,
            .total_bytes = (size_t)image_size,
        };
        progress_cb(&status, user_data);
    }

    // Download and flash in chunks
    while (1) {
        err = esp_https_ota_perform(ota_handle);

        if (err != ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
            break;
        }

        // Report progress
        if (progress_cb && image_size > 0) {
            int downloaded = esp_https_ota_get_image_len_read(ota_handle);
            ota_status_t status = {
                .state = OTA_STATE_DOWNLOADING,
                .progress_percent = (downloaded * 100) / image_size,
                .downloaded_bytes = (size_t)downloaded,
                .total_bytes = (size_t)image_size,
            };
            progress_cb(&status, user_data);
        }
    }

    // Check final result
    if (err == ESP_OK) {
        // Verify image
        if (progress_cb) {
            ota_status_t status = {
                .state = OTA_STATE_VERIFYING,
                .progress_percent = 100,
                .downloaded_bytes = (size_t)image_size,
                .total_bytes = (size_t)image_size,
            };
            progress_cb(&status, user_data);
        }

        err = esp_https_ota_finish(ota_handle);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "OTA update successful!");

            if (progress_cb) {
                ota_status_t status = {
                    .state = OTA_STATE_SUCCESS,
                    .progress_percent = 100,
                    .downloaded_bytes = (size_t)image_size,
                    .total_bytes = (size_t)image_size,
                };
                snprintf(status.error_msg, sizeof(status.error_msg), "Update successful, rebooting...");
                progress_cb(&status, user_data);
            }

            ESP_LOGI(TAG, "Rebooting in 2 seconds...");
            vTaskDelay(pdMS_TO_TICKS(2000));
            esp_restart();
        } else {
            set_error("OTA finish failed: %s", esp_err_to_name(err));
        }
    } else {
        set_error("OTA download failed: %s", esp_err_to_name(err));
        esp_https_ota_abort(ota_handle);
    }

    // Report error state
    if (err != ESP_OK && progress_cb) {
        ota_status_t status = {
            .state = OTA_STATE_ERROR,
            .progress_percent = 0,
            .downloaded_bytes = 0,
            .total_bytes = 0,
        };
        snprintf(status.error_msg, sizeof(status.error_msg), "%s", s_last_error);
        progress_cb(&status, user_data);
    }

    return err;
}

esp_err_t ota_mark_app_valid(void) {
    esp_ota_img_states_t ota_state;
    esp_err_t err = esp_ota_get_state_partition(s_running_partition, &ota_state);

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to get OTA state: %s", esp_err_to_name(err));
        return err;
    }

    if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
        err = esp_ota_mark_app_valid_cancel_rollback();
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "OTA image marked as valid, rollback cancelled");
        } else {
            ESP_LOGE(TAG, "Failed to mark app valid: %s", esp_err_to_name(err));
        }
        return err;
    } else if (ota_state == ESP_OTA_IMG_VALID) {
        ESP_LOGI(TAG, "OTA image already marked as valid");
        return ESP_OK;
    } else {
        ESP_LOGW(TAG, "Unexpected OTA state: %d", ota_state);
        return ESP_FAIL;
    }
}

esp_err_t ota_rollback(void) {
    ESP_LOGW(TAG, "Manual rollback requested");

    const esp_partition_t* last_invalid_partition = esp_ota_get_last_invalid_partition();
    if (last_invalid_partition != NULL) {
        ESP_LOGI(TAG, "Rolling back to partition: %s", last_invalid_partition->label);

        esp_err_t err = esp_ota_set_boot_partition(last_invalid_partition);
        if (err != ESP_OK) {
            set_error("Failed to set boot partition: %s", esp_err_to_name(err));
            return err;
        }

        ESP_LOGI(TAG, "Rollback configured, rebooting...");
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_restart();
    } else {
        set_error("No previous partition available for rollback");
        return ESP_FAIL;
    }

    return ESP_OK;  // Never reached (reboots)
}
