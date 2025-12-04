// components/settings/settings.h

#ifndef SETTINGS_H
#define SETTINGS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#define SETTINGS_VERSION 2  // Incremented: added background_image field

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
    char background_image[128];  // Path to background image (e.g., "A:/sdcard/splash.png")

    // Features
    bool enable_weather;
    bool enable_touch;

    // OTA
    char ota_server_url[128];

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
