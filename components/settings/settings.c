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
    .background_image = "A:/backgrounds/splash.png",
    .text_color = 0xFFFFFF,  // Default to white (RGB888)
    .enable_weather = true,
    .enable_touch = true,
    .ota_server_url = "http://192.168.1.96:8000",
    // Display schedule defaults (seconds)
    .weather_show_s     = 30,
    .weather_cooldown_s = 1800,     // 30 min
    .radar_show_s       = 30,
    .radar_cooldown_s   = 1800,     // 30 min
    .astro_show_s       = 30,
    .astro_cooldown_s   = 43200,    // 12 hours (uint16 max = 65535 ≈ 18h, plenty)
    .photos_interval_s  = 1800,     // 30 min
    .photos_show_s      = 30,
    .ambient_interval_s = 2700,     // 45 min
    .ambient_show_s     = 30,
};

// ─── Internal helpers ─────────────────────────────────────────────────────────

/**
 * @brief Copy the compile-time defaults into the output struct.
 *
 * @param out  Destination settings struct.
 */
static void settings_use_defaults(clock_settings_t* out)
{
    memcpy(out, &default_settings, sizeof(clock_settings_t));
}

/**
 * @brief Check that a stored blob exists and has the expected size.
 *
 * @param handle    Open NVS handle.
 * @param size_out  Set to the stored blob size.
 * @return true if the blob is present and matches sizeof(clock_settings_t).
 */
static bool settings_nvs_readable(nvs_handle_t handle, size_t* size_out)
{
    esp_err_t err = nvs_get_blob(handle, SETTINGS_KEY, NULL, size_out);
    if (err != ESP_OK) return false;
    return (*size_out == sizeof(clock_settings_t));
}

/**
 * @brief Read the settings blob and validate the version field.
 *
 * @param handle  Open NVS handle.
 * @param out     Destination settings struct.
 * @return true if read succeeded and version matches SETTINGS_VERSION.
 */
static bool settings_nvs_read(nvs_handle_t handle, clock_settings_t* out)
{
    size_t size = sizeof(clock_settings_t);
    esp_err_t err = nvs_get_blob(handle, SETTINGS_KEY, out, &size);
    return (err == ESP_OK) && (out->version == SETTINGS_VERSION);
}

/**
 * @brief Log a summary of the loaded settings.
 *
 * Logs at INFO on first call, DEBUG thereafter (reduces noise from
 * repeated settings_load calls during state transitions).
 *
 * @param out  Loaded settings.
 */
static void settings_log_load_result(const clock_settings_t* out)
{
    static bool s_first_load = true;
    if (s_first_load) {
        ESP_LOGI(TAG, "Settings loaded — WiFi configured: %s",
                 out->wifi_configured ? "yes" : "no");
        if (out->wifi_configured) {
            ESP_LOGI(TAG, "  SSID: %s", out->wifi_ssid);
        }
        ESP_LOGI(TAG, "  Brightness: %d%%", out->brightness);
        ESP_LOGI(TAG, "  Location: %.3f, %.3f", out->latitude, out->longitude);
        s_first_load = false;
    } else {
        ESP_LOGD(TAG, "Settings loaded (WiFi=%s, brightness=%d%%)",
                 out->wifi_configured ? "yes" : "no", out->brightness);
    }
}

/**
 * @brief Attempt to load settings from an already-open NVS handle.
 *
 * Populates defaults on any error (size mismatch, read failure, version
 * mismatch). The handle must be closed by the caller.
 *
 * @param handle  Open NVS handle.
 * @param out     Destination settings struct.
 */
static void settings_load_from_open_handle(nvs_handle_t handle, clock_settings_t* out)
{
    size_t stored_size = 0;
    bool ok = settings_nvs_readable(handle, &stored_size) && settings_nvs_read(handle, out);
    if (!ok) {
        ESP_LOGW(TAG, "Settings unavailable, size mismatch, or version change — using defaults");
        settings_use_defaults(out);
    }
}

/**
 * @brief Write a settings blob and commit in a single NVS operation sequence.
 *
 * @param handle  Open NVS handle (NVS_READWRITE).
 * @param in      Settings to persist.
 * @return ESP_OK on success; error code on failure.
 */
static esp_err_t settings_write_and_commit(nvs_handle_t handle, const clock_settings_t* in)
{
    esp_err_t err = nvs_set_blob(handle, SETTINGS_KEY, in, sizeof(clock_settings_t));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save settings: %d", (int)err);
        return err;
    }
    err = nvs_commit(handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit settings: %d", (int)err);
    }
    return err;
}

/**
 * @brief Erase the settings key and commit the result.
 *
 * ESP_ERR_NVS_NOT_FOUND is treated as success (key already absent).
 *
 * @param handle  Open NVS handle (NVS_READWRITE).
 * @return ESP_OK on success; error code on failure.
 */
static esp_err_t settings_erase_and_commit(nvs_handle_t handle)
{
    esp_err_t err = nvs_erase_key(handle, SETTINGS_KEY);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGE(TAG, "Failed to erase settings: %d", (int)err);
        return err;
    }
    err = nvs_commit(handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit reset: %d", (int)err);
    }
    return err;
}

// ─── Public API ───────────────────────────────────────────────────────────────

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

/**
 * @brief Load settings from NVS into @p out.
 *
 * Falls back to compile-time defaults on any failure (namespace not found,
 * size mismatch, read error, version mismatch).  Always returns ESP_OK unless
 * @p out is NULL.
 *
 * @param out  Destination settings struct.
 * @return ESP_OK or ESP_ERR_INVALID_ARG.
 */
esp_err_t settings_load(clock_settings_t* out) {
    if (!out) return ESP_ERR_INVALID_ARG;
    nvs_handle_t handle;
    esp_err_t err = nvs_open(SETTINGS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Settings namespace not found, using defaults");
        settings_use_defaults(out);
        return ESP_OK;
    }
    settings_load_from_open_handle(handle, out);
    nvs_close(handle);
    settings_log_load_result(out);
    return ESP_OK;
}

/**
 * @brief Persist @p in to NVS.
 *
 * @param in  Settings to save.
 * @return ESP_OK on success; error code on failure.
 */
esp_err_t settings_save(const clock_settings_t* in) {
    if (!in) return ESP_ERR_INVALID_ARG;
    ESP_LOGI(TAG, "Saving settings to NVS");
    nvs_handle_t handle;
    esp_err_t err = nvs_open(SETTINGS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS for writing: %d", (int)err);
        return err;
    }
    err = settings_write_and_commit(handle, in);
    nvs_close(handle);
    if (err == ESP_OK) ESP_LOGI(TAG, "Settings saved successfully");
    return err;
}

/**
 * @brief Erase persisted settings from NVS (restores defaults on next load).
 *
 * @return ESP_OK on success; error code on commit failure.
 */
esp_err_t settings_reset(void) {
    ESP_LOGI(TAG, "Resetting settings to factory defaults");
    nvs_handle_t handle;
    esp_err_t err = nvs_open(SETTINGS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "No settings to erase");
        return ESP_OK;
    }
    err = settings_erase_and_commit(handle);
    nvs_close(handle);
    if (err == ESP_OK) ESP_LOGI(TAG, "Settings reset complete");
    return err;
}

const clock_settings_t* settings_get_defaults(void) {
    return &default_settings;
}
