// components/settings/settings.c

#include "settings.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <string.h>

static const char* TAG = "settings";

#define SETTINGS_NAMESPACE "clock_cfg"
#define SETTINGS_KEY "settings"

static const clock_settings_t default_settings = {
    .version = SETTINGS_VERSION,
    .wifi_ssid = "",
    .wifi_password = "",
    .hostname = "greenwood-clock",
    .wifi_configured = false,
    .latitude = 43.366f,
    .longitude = -85.851f,
    .timezone = "EST5EDT,M3.2.0/2,M11.1.0/2",
    .brightness = 50,
    .clock_update_ms = 60000,
    .weather_update_ms = 1800000,
    .background_image = "A:/splash.png",  // A: maps to /sdcard, so A:/splash.png = /sdcard/splash.png
    .text_color = 0xFFFFFF,  // Default to white (RGB888)
    .enable_weather = true,
    .enable_touch = true,
    .ota_server_url = "http://192.168.1.96:8000",
};

esp_err_t settings_init(void) {
    ESP_LOGI(TAG, "Initializing settings system");

    // Initialize NVS
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // NVS partition was truncated and needs to be erased
        ESP_LOGW(TAG, "NVS partition needs erase, erasing...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize NVS: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "NVS initialized successfully");
    return ESP_OK;
}

esp_err_t settings_load(clock_settings_t* out) {
    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;
    esp_err_t err;

    // Try to open NVS namespace
    err = nvs_open(SETTINGS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Settings namespace not found, using defaults");
        memcpy(out, &default_settings, sizeof(clock_settings_t));
        return ESP_OK;
    }

    // First check the size of stored settings
    size_t stored_size = 0;
    err = nvs_get_blob(handle, SETTINGS_KEY, NULL, &stored_size);

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Settings not found in NVS, using defaults");
        memcpy(out, &default_settings, sizeof(clock_settings_t));
        nvs_close(handle);
        return ESP_OK;
    }

    // Check if size matches - if not, settings are from old version
    if (stored_size != sizeof(clock_settings_t)) {
        ESP_LOGW(TAG, "Settings size mismatch (stored: %zu, expected: %zu), using defaults",
                 stored_size, sizeof(clock_settings_t));
        memcpy(out, &default_settings, sizeof(clock_settings_t));
        nvs_close(handle);
        return ESP_OK;
    }

    // Now safe to load settings blob
    size_t size = sizeof(clock_settings_t);
    err = nvs_get_blob(handle, SETTINGS_KEY, out, &size);
    nvs_close(handle);

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to read settings from NVS, using defaults");
        memcpy(out, &default_settings, sizeof(clock_settings_t));
        return ESP_OK;
    }

    // Validate version (redundant check, but keeps it for future)
    if (out->version != SETTINGS_VERSION) {
        ESP_LOGW(TAG, "Settings version mismatch (found %lu, expected %d), using defaults",
                 (unsigned long)out->version, SETTINGS_VERSION);
        memcpy(out, &default_settings, sizeof(clock_settings_t));
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Settings loaded from NVS");
    ESP_LOGI(TAG, "  WiFi configured: %s", out->wifi_configured ? "yes" : "no");
    if (out->wifi_configured) {
        ESP_LOGI(TAG, "  SSID: %s", out->wifi_ssid);
    }
    ESP_LOGI(TAG, "  Brightness: %d%%", out->brightness);
    ESP_LOGI(TAG, "  Location: %.3f, %.3f", out->latitude, out->longitude);

    return ESP_OK;
}

esp_err_t settings_save(const clock_settings_t* in) {
    if (in == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;
    esp_err_t err;

    ESP_LOGI(TAG, "Saving settings to NVS");

    // Open NVS namespace for writing
    err = nvs_open(SETTINGS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS for writing: %s", esp_err_to_name(err));
        return err;
    }

    // Save settings blob
    err = nvs_set_blob(handle, SETTINGS_KEY, in, sizeof(clock_settings_t));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save settings: %s", esp_err_to_name(err));
        nvs_close(handle);
        return err;
    }

    // Commit changes
    err = nvs_commit(handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit settings: %s", esp_err_to_name(err));
        nvs_close(handle);
        return err;
    }

    nvs_close(handle);
    ESP_LOGI(TAG, "Settings saved successfully");

    return ESP_OK;
}

esp_err_t settings_reset(void) {
    nvs_handle_t handle;
    esp_err_t err;

    ESP_LOGI(TAG, "Resetting settings to factory defaults");

    // Open NVS namespace
    err = nvs_open(SETTINGS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "No settings to erase");
        return ESP_OK;
    }

    // Erase the settings key
    err = nvs_erase_key(handle, SETTINGS_KEY);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGE(TAG, "Failed to erase settings: %s", esp_err_to_name(err));
        nvs_close(handle);
        return err;
    }

    // Commit changes
    err = nvs_commit(handle);
    nvs_close(handle);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit reset: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Settings reset complete");
    return ESP_OK;
}

const clock_settings_t* settings_get_defaults(void) {
    return &default_settings;
}
