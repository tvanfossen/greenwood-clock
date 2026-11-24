/**
 * @file nws_conditions.c
 * @version 2
 * @brief Fetch current weather conditions from the NWS observations endpoint, filling
 *        values the nearest station lacks from the next nearest ones.
 */

#include "nws_internal.h"
#include "nws_conditions_merge.h"

static const char *TAG = "nws_cond";

#define NWS_OBS_URL_FMT  "https://api.weather.gov/stations/%s/observations/latest"

// Wind direction degrees → cardinal string
/**
 * @brief Convert a wind direction in degrees to a 16-point cardinal ("N".."NNW").
 * @version 1
 * @utility
 */
static void degrees_to_cardinal(int deg, char *out, size_t out_size)
{
    static const char *dirs[] = {
        "N", "NNE", "NE", "ENE", "E", "ESE", "SE", "SSE",
        "S", "SSW", "SW", "WSW", "W", "WNW", "NW", "NNW"
    };
    int idx = ((deg + 11) / 22) % 16;
    strncpy(out, dirs[idx], out_size - 1);
    out[out_size - 1] = '\0';
}

/**
 * @brief Parse an observations/latest response into @p out.
 *
 * Reads temperature, wind chill (falls back to temperature), dew point, humidity,
 * wind, pressure (Pa -> hPa when > 10000), visibility (m -> km when > 1000),
 * text description, icon URL and station ID, then sets valid.
 * @version 2
 * @req REQ-WX-002
 * @return ESP_OK on success, ESP_ERR_INVALID_RESPONSE if the JSON does not parse
 *         or has no properties.
 * v2: missing values use the shared sentinels (nws_conditions_merge.h).
 */
static esp_err_t parse_conditions(const char *json_str, nws_conditions_t *out)
{
    cJSON *root = cJSON_Parse(json_str);
    if (!root) {
        ESP_LOGE(TAG, "JSON parse failed");
        return ESP_ERR_INVALID_RESPONSE;
    }

    cJSON *props = cJSON_GetObjectItemCaseSensitive(root, "properties");
    if (!props) {
        ESP_LOGE(TAG, "no 'properties' in observations response");
        cJSON_Delete(root);
        return ESP_ERR_INVALID_RESPONSE;
    }

    out->temp_c       = json_get_value_float(props, "temperature", NWS_TEMP_MISSING);
    out->feels_like_c = json_get_value_float(props, "windChill", out->temp_c);
    out->dew_point_c  = json_get_value_float(props, "dewpoint", NWS_TEMP_MISSING);
    out->humidity      = json_get_value_int(props, "relativeHumidity", NWS_HUMIDITY_MISSING);
    out->wind_speed_kmh = json_get_value_float(props, "windSpeed", 0.0f);
    out->wind_gust_kmh  = json_get_value_float(props, "windGust", 0.0f);
    out->wind_dir_deg   = json_get_value_int(props, "windDirection", 0);
    out->pressure_hpa   = json_get_value_float(props, "barometricPressure", 0.0f);
    out->visibility_km  = json_get_value_float(props, "visibility", 0.0f);

    // Convert pressure from Pa to hPa if it looks like Pa (>10000)
    if (out->pressure_hpa > 10000.0f) {
        out->pressure_hpa /= 100.0f;
    }

    // Convert visibility from m to km if it looks like meters (>1000)
    if (out->visibility_km > 1000.0f) {
        out->visibility_km /= 1000.0f;
    }

    degrees_to_cardinal(out->wind_dir_deg, out->wind_dir_cardinal,
                        sizeof(out->wind_dir_cardinal));

    json_get_str(props, "textDescription", out->description, sizeof(out->description));

    const cJSON *icon = cJSON_GetObjectItemCaseSensitive(props, "icon");
    if (cJSON_IsString(icon) && icon->valuestring) {
        strncpy(out->icon_url, icon->valuestring, sizeof(out->icon_url) - 1);
    }

    const cJSON *station = cJSON_GetObjectItemCaseSensitive(props, "station");
    if (cJSON_IsString(station) && station->valuestring) {
        // Station URL: ".../stations/KMDW" → extract last segment
        const char *last_slash = strrchr(station->valuestring, '/');
        if (last_slash) {
            strncpy(out->station_id, last_slash + 1, sizeof(out->station_id) - 1);
        }
    }

    out->valid = true;
    cJSON_Delete(root);

    ESP_LOGI(TAG, "conditions: %.1f°C \"%s\" wind %s %.0f km/h humidity %d%%",
             out->temp_c, out->description, out->wind_dir_cardinal,
             out->wind_speed_kmh, out->humidity);
    return ESP_OK;
}

#define FALLBACK_STATIONS 4   /* nearest stations asked for values the first one lacks */

static char s_nearby[FALLBACK_STATIONS][8];
static int  s_nearby_n = -1;     /* -1: not fetched yet (fetched once, on the first gap) */

/**
 * @brief Fetch and parse the latest observation of @p station.
 * @version 1
 * @utility
 * @return ESP_OK, ESP_ERR_HTTP_CONNECT or ESP_ERR_INVALID_RESPONSE.
 */
static esp_err_t fetch_station(const char *station, nws_conditions_t *out)
{
    memset(out, 0, sizeof(*out));
    char url[128];
    snprintf(url, sizeof(url), NWS_OBS_URL_FMT, station);
    char *json = nws_http_fetch_json(TAG, url);
    if (!json) return ESP_ERR_HTTP_CONNECT;
    esp_err_t err = parse_conditions(json, out);
    free(json);
    return err;
}

/**
 * @brief Fill the values @p out lacks from the next nearest stations of @p loc's grid cell
 *        (the list is fetched once), stopping once nothing is missing.
 * @version 1
 * @utility
 */
static void fill_from_nearby(const nws_location_t *loc, nws_conditions_t *out)
{
    if (s_nearby_n < 0) s_nearby_n = nws_nearby_stations(loc, s_nearby, FALLBACK_STATIONS);
    for (int i = 0; i < s_nearby_n && nws_conditions_incomplete(out); i++) {
        if (strcmp(s_nearby[i], loc->station) == 0) continue;
        nws_conditions_t other;
        if (fetch_station(s_nearby[i], &other) != ESP_OK) continue;
        int n = nws_conditions_fill(out, &other);
        if (n > 0) ESP_LOGI(TAG, "%d missing value(s) filled from station %s", n, s_nearby[i]);
    }
    if (nws_conditions_incomplete(out)) ESP_LOGW(TAG, "some values missing at every station");
}

/**
 * @brief Fetch and parse the latest observation for @p loc's station; values it lacks (NWS
 *        sends nulls) are filled from the next nearest stations.
 *
 * @p out is zeroed first, so on failure valid=false.
 * v2: fills missing values from nearby stations (nws_conditions_fill).
 * @version 2
 * @req REQ-WX-002
 * @return ESP_OK on success, ESP_ERR_HTTP_CONNECT if the fetch failed, or
 *         ESP_ERR_INVALID_RESPONSE on a malformed response.
 */
esp_err_t nws_fetch_conditions(const nws_location_t *loc, nws_conditions_t *out)
{
    esp_err_t err = fetch_station(loc->station, out);
    if (err == ESP_OK && nws_conditions_incomplete(out)) fill_from_nearby(loc, out);
    return err;
}
