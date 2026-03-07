// components/nws/include/nws.h
//
// NWS Weather Service Integration — public API and data structures.
// Free, unlimited, US-only. No API key required (User-Agent only).

#ifndef NWS_H
#define NWS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "display_events.h"

// Callback type for delivering display events from NWS task.
// Decouples nws from display_fsm — caller provides the bridge function.
typedef bool (*nws_event_cb_t)(const display_event_t *evt);

// ============================================================================
// Data structures
// ============================================================================

typedef struct nws_conditions_t {
    float temp_c;
    float feels_like_c;
    float dew_point_c;
    int   humidity;
    float wind_speed_kmh;
    float wind_gust_kmh;
    int   wind_dir_deg;
    char  wind_dir_cardinal[4];     // "NW", "SSE", etc.
    float pressure_hpa;
    float visibility_km;
    char  description[64];          // "Mostly Cloudy", "Light Rain"
    char  icon_url[128];            // NWS icon URL (unused for now)
    char  station_id[8];
    int64_t timestamp;
    bool  valid;                    // true if data was successfully parsed
} nws_conditions_t;

typedef struct nws_forecast_period_t {
    char  name[32];                 // "Tonight", "Tuesday"
    char  detail_forecast[512];     // Full text
    char  short_forecast[64];       // "Partly Cloudy"
    int   temperature;
    char  temp_unit;                // 'F' or 'C'
    char  wind_speed[16];           // "10 to 15 mph"
    char  wind_direction[4];        // "NW"
    bool  is_daytime;
} nws_forecast_period_t;

typedef struct nws_forecast_t {
    nws_forecast_period_t periods[14];  // 7 days x 2 (day + night)
    int period_count;
    bool valid;
} nws_forecast_t;

typedef struct nws_alert_t {
    char  event[64];                // "Tornado Warning"
    char  severity[16];             // "Extreme", "Severe", "Moderate", "Minor"
    char  urgency[16];              // "Immediate", "Expected", "Future"
    char  certainty[16];            // "Observed", "Likely", "Possible"
    char  headline[256];
    char  description[1024];
    char  instruction[512];
    char  area_desc[256];
    int64_t onset;
    int64_t expires;
} nws_alert_t;

typedef struct nws_alerts_t {
    nws_alert_t alerts[8];          // Max 8 concurrent alerts
    int alert_count;
    bool valid;
} nws_alerts_t;

// NWS grid location (cached in NVS after /points lookup)
typedef struct nws_location_t {
    char  office[8];                // e.g. "GRR"
    int   grid_x;
    int   grid_y;
    char  station[8];               // nearest observation station
    float lat;
    float lon;
    bool  resolved;
} nws_location_t;

// ============================================================================
// Location resolver
// ============================================================================

/**
 * @brief Resolve lat/lon to NWS grid coordinates and observation station.
 *
 * On first call, fetches from api.weather.gov/points/{lat},{lon} and caches
 * in NVS.  Subsequent calls return cached values unless lat/lon changed.
 *
 * @param lat   Latitude
 * @param lon   Longitude
 * @param out   Resolved location info
 * @return ESP_OK on success
 */
esp_err_t nws_resolve_location(float lat, float lon, nws_location_t *out);

// ============================================================================
// Data fetch functions (each returns cached data on failure)
// ============================================================================

esp_err_t nws_fetch_conditions(const nws_location_t *loc, nws_conditions_t *out);
esp_err_t nws_fetch_forecast(const nws_location_t *loc, nws_forecast_t *out);
esp_err_t nws_fetch_alerts(float lat, float lon, nws_alerts_t *out);

/**
 * @brief Fetch radar image from NOAA ImageServer.
 *
 * Returns a transparent RGBA PNG in a SPIRAM-allocated buffer.
 * Caller must free() the buffer.
 *
 * @param lat       Center latitude
 * @param lon       Center longitude
 * @param png_buf   Output: SPIRAM-allocated PNG data
 * @param png_len   Output: length of PNG data
 * @return ESP_OK on success
 */
esp_err_t nws_fetch_radar(float lat, float lon, uint8_t **png_buf, size_t *png_len);

/**
 * @brief Fetch current Kp index from NOAA SWPC.
 * @param kp_out  Most recent Kp value (0.0-9.0)
 * @return ESP_OK on success
 */
esp_err_t nws_fetch_kp_index(float *kp_out);

// ============================================================================
// Condition-to-Lottie mapping
// ============================================================================

/**
 * @brief Map NWS short_forecast string to a Lottie animation file path.
 *
 * Uses substring matching against known NWS condition patterns.
 * Falls back to "mostly_cloudy" if no pattern matches.
 *
 * @param short_forecast  NWS short forecast text (e.g., "Partly Cloudy")
 * @param is_daytime      true for day variant, false for night
 * @param path_out        Output buffer for LVGL file path (e.g., "A:/lottie/weather/day/clear.json")
 * @param max_len         Size of path_out buffer
 */
void nws_condition_to_lottie_path(const char *short_forecast, bool is_daytime,
                                   char *path_out, size_t max_len);

// ============================================================================
// Cached data accessors (thread-safe reads of last successful fetch)
// ============================================================================

const nws_conditions_t *nws_get_conditions(void);
const nws_forecast_t   *nws_get_forecast(void);
const nws_alerts_t     *nws_get_alerts(void);
float                   nws_get_kp_index(void);

/**
 * @brief Get cached radar PNG data (last successful fetch).
 * @param len_out  Output: length of PNG data in bytes
 * @return Pointer to SPIRAM-allocated PNG data, or NULL if no radar cached.
 *         Pointer is valid until next radar fetch. Caller must NOT free.
 */
const uint8_t *nws_get_radar_png(size_t *len_out);

/**
 * @brief Check if cached radar data shows precipitation.
 * Uses PNG size heuristic (>20KB = precipitation present).
 * @return true if radar data exists and indicates precipitation.
 */
bool nws_radar_has_precipitation(void);

// ============================================================================
// Task management
// ============================================================================

/**
 * @brief Initialize NWS subsystem and start the polling task.
 * @param lat       Device latitude
 * @param lon       Device longitude
 * @param event_cb  Callback for delivering display events (e.g. display_fsm_send_event).
 *                  If NULL, events are logged but not delivered.
 * @return ESP_OK on success
 */
esp_err_t nws_init(float lat, float lon, nws_event_cb_t event_cb);

#ifdef __cplusplus
}
#endif

#endif // NWS_H
