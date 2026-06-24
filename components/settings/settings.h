// components/settings/settings.h

#ifndef SETTINGS_H
#define SETTINGS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#define SETTINGS_VERSION 6  // Bumped: device NVS now stores v6 blobs

typedef struct {
    uint32_t version;  // Settings schema version

    // Wi-Fi
    char wifi_ssid[32];
    char wifi_password[64];
    char hostname[32];
    bool wifi_configured;  // Has WiFi been set up?

    // Location
    float latitude;
    float longitude;
    char timezone[64];  // POSIX TZ string

    // Display
    uint8_t brightness;
    uint32_t clock_update_ms;
    uint32_t weather_update_ms;
    char background_image[128];  // Path to background image (A:/file.gif = /sdcard/file.gif, B:/file.png = /spiffs/file.png)
    uint32_t text_color;  // Text color as RGB888 (0xRRGGBB format) for main clock screen

    // Features
    bool enable_weather;
    bool enable_touch;

    // OTA
    char ota_server_url[128];

    // NWS Weather — cached grid/station from /points lookup
    char nws_office[8];       // e.g. "GRR"
    int  nws_grid_x;
    int  nws_grid_y;
    char nws_station[8];      // e.g. "KMKG"

    // Display schedule — per-state timing (seconds)
    uint16_t weather_show_s;      // how long to display weather (default 30)
    uint16_t weather_cooldown_s;  // debounce between triggers (default 1800)
    uint16_t radar_show_s;        // default 30
    uint16_t radar_cooldown_s;    // default 1800
    uint16_t astro_show_s;        // default 30
    uint16_t astro_cooldown_s;    // default 43200 (12h)
    uint16_t photos_interval_s;   // periodic trigger interval (default 1800)
    uint16_t photos_show_s;       // default 30
    uint16_t ambient_interval_s;  // periodic trigger interval (default 2700)
    uint16_t ambient_show_s;      // default 30

} clock_settings_t;

/**
 * @brief Initialize NVS and settings system
 * @return ESP_OK on success
 */
esp_err_t settings_init(void);

/**
 * @brief Load settings from NVS (or use defaults if not found)
 * @param out Pointer to settings structure to fill
 * @return ESP_OK on success
 */
esp_err_t settings_load(clock_settings_t* out);

/**
 * @brief Save settings to NVS
 * @param in Pointer to settings structure to save
 * @return ESP_OK on success
 */
esp_err_t settings_save(const clock_settings_t* in);

/**
 * @brief Reset settings to factory defaults
 * @return ESP_OK on success
 */
esp_err_t settings_reset(void);

/**
 * @brief Get pointer to default settings (read-only)
 * @return Pointer to default settings structure
 */
const clock_settings_t* settings_get_defaults(void);

#ifdef __cplusplus
}
#endif

#endif // SETTINGS_H
