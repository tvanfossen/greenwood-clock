// components/weather/weather.h

#ifndef WEATHER_H
#define WEATHER_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    // Core
    float temp_c;                   // "temp"
    float feels_like_c;             // "app_temp"
    float dew_point_c;              // "dewpt"
    int   humidity;                 // "rh"
    float precip_mm;                // "precip"
    float pressure_mb;              // "pres"
    int   clouds;                   // "clouds"
    float visibility_km;            // "vis"
    float uv_index;                 // "uv"
    int   aqi;                      // "aqi"

    // Wind
    float wind_spd_m_s;             // "wind_spd"
    float wind_gust_m_s;            // "gust"
    int   wind_dir_deg;             // "wind_dir"
    char  wind_cdir[4];             // "wind_cdir" (e.g. "E")
    char  wind_cdir_full[16];       // "wind_cdir_full" ("east")

    // Location/time
    char  city_name[32];            // "city_name"
    char  state_code[4];            // "state_code"
    char  country_code[4];          // "country_code"
    char  timezone[32];             // "timezone"
    char  datetime[20];             // "datetime" (YYYY-MM-DD:HH)
    char  ob_time[20];              // "ob_time" (YYYY-MM-DD HH:MM)
    int64_t timestamp;              // "ts"

    // Solar
    float solar_ghi;                // "ghi"
    float solar_dhi;                // "dhi"
    float solar_dni;                // "dni"
    float solar_rad;                // "solar_rad"
    float elev_angle;               // "elev_angle"
    float h_angle;                  // "h_angle"

    // Sun
    char  sunrise[6];               // "sunrise" (HH:MM)
    char  sunset[6];                // "sunset"  (HH:MM)

    // Station & sources
    char  station[16];              // "station"
    // sources array omitted for brevity

    // Weather summary
    char  description[32];          // weather.description
    char  icon_url[64];             // constructed from weather.icon

} weather_data_t;

/**
 * Initialize the weather component.
 * API key is taken from secrets.h (WEATHER_API_KEY).
 */
esp_err_t weather_init(void);

/**
 * Fetch current weather for given latitude & longitude.
 * Fills out_data on success.
 */
esp_err_t weather_fetch(float latitude,
                        float longitude,
                        weather_data_t *out_data);

/**
 * Fetch the PNG icon for the given conditions.
 * On success: *out_buf=malloc’d PNG bytes, *out_len=length,
 * return ESP_OK. Caller must free(*out_buf).
 */
esp_err_t weather_fetch_icon(const weather_data_t *wd,
                             uint8_t **out_buf,
                             size_t  *out_len);

#ifdef __cplusplus
}
#endif

#endif // WEATHER_H