/**
 * @file nws_location.c
 * @version 1
 * @brief Resolve lat/lon to NWS grid coordinates and nearest observation station.
 *
 * Caches the result in NVS to avoid redundant /points calls on reboot.
 */

#include "nws_internal.h"
#include "settings.h"

static const char *TAG = "nws_loc";

#define NWS_POINTS_URL_FMT  "https://api.weather.gov/points/%.4f,%.4f"

// ---------------------------------------------------------------------------
// NVS cache helpers
// ---------------------------------------------------------------------------

/**
 * @brief Fill @p out from the NVS-cached NWS location if it matches @p lat / @p lon (~100 m).
 * @version 1
 * @req REQ-WX-001
 * @return true on a cache hit (out resolved), false if absent or the location moved.
 */
static bool location_cached(float lat, float lon, nws_location_t *out)
{
    clock_settings_t cfg;
    if (settings_load(&cfg) != ESP_OK) return false;

    // Check if we have cached NWS fields and lat/lon matches
    if (cfg.nws_office[0] == '\0' || cfg.nws_station[0] == '\0') return false;

    // Compare with ~100m precision
    float dlat = lat - cfg.latitude;
    float dlon = lon - cfg.longitude;
    if (dlat * dlat + dlon * dlon > 0.001f * 0.001f) {
        ESP_LOGI(TAG, "lat/lon changed — re-resolving");
        return false;
    }

    snprintf(out->office, sizeof(out->office), "%s", cfg.nws_office);
    out->grid_x = cfg.nws_grid_x;
    out->grid_y = cfg.nws_grid_y;
    snprintf(out->station, sizeof(out->station), "%s", cfg.nws_station);
    out->lat = lat;
    out->lon = lon;
    out->resolved = true;
    ESP_LOGI(TAG, "NVS cache hit: office=%s grid=%d,%d station=%s",
             out->office, out->grid_x, out->grid_y, out->station);
    return true;
}

/**
 * @brief Persist the resolved office, grid and station into the NVS settings blob.
 * @version 1
 * @req REQ-WX-001
 * @return ESP_OK, or the settings_load / settings_save error.
 */
static esp_err_t location_cache_save(const nws_location_t *loc)
{
    clock_settings_t cfg;
    esp_err_t err = settings_load(&cfg);
    if (err != ESP_OK) return err;

    snprintf(cfg.nws_office, sizeof(cfg.nws_office), "%s", loc->office);
    cfg.nws_grid_x = loc->grid_x;
    cfg.nws_grid_y = loc->grid_y;
    snprintf(cfg.nws_station, sizeof(cfg.nws_station), "%s", loc->station);

    return settings_save(&cfg);
}

// ---------------------------------------------------------------------------
// Parse /points response
// ---------------------------------------------------------------------------

/**
 * @brief Copy the first feature's stationIdentifier from a /stations response into @p out.
 * @version 1
 * @req REQ-WX-001
 * @return true if a station ID was found and copied.
 */
static bool station_id_from_features(const cJSON *st_root, nws_location_t *out)
{
    const cJSON *features = cJSON_GetObjectItemCaseSensitive(st_root, "features");
    if (!cJSON_IsArray(features) || cJSON_GetArraySize(features) == 0) {
        return false;
    }
    const cJSON *first = cJSON_GetArrayItem(features, 0);
    const cJSON *st_props = cJSON_GetObjectItemCaseSensitive(first, "properties");
    const cJSON *st_id = st_props
        ? cJSON_GetObjectItemCaseSensitive(st_props, "stationIdentifier") : NULL;
    bool found = cJSON_IsString(st_id) && st_id->valuestring;
    if (found) {
        snprintf(out->station, sizeof(out->station), "%s", st_id->valuestring);
    }
    return found;
}

/**
 * @brief Fetch the observation-stations list at @p url and take the nearest (first) station.
 * @version 1
 * @req REQ-WX-001
 */
static void fetch_nearest_station(const char *url, nws_location_t *out)
{
    char *stations_json = nws_http_fetch_json(TAG, url);
    if (!stations_json) {
        return;
    }
    cJSON *st_root = cJSON_Parse(stations_json);
    if (st_root && station_id_from_features(st_root, out)) {
        ESP_LOGI(TAG, "nearest station: %s", out->station);
    }
    cJSON_Delete(st_root);  // NULL-safe
    free(stations_json);
}

/**
 * @brief Extract office (cwa), grid X/Y and the observationStations URL from /points properties.
 * @version 1
 * @req REQ-WX-001
 */
static void parse_points_props(const cJSON *props, nws_location_t *out,
                               char *station_list_url, size_t url_len)
{
    // Extract grid office (e.g. "https://api.weather.gov/offices/GRR" → "GRR")
    const cJSON *forecast_office = cJSON_GetObjectItemCaseSensitive(props, "cwa");
    if (cJSON_IsString(forecast_office) && forecast_office->valuestring) {
        snprintf(out->office, sizeof(out->office), "%s", forecast_office->valuestring);
    }

    // Grid coordinates
    const cJSON *gx = cJSON_GetObjectItemCaseSensitive(props, "gridX");
    const cJSON *gy = cJSON_GetObjectItemCaseSensitive(props, "gridY");
    if (cJSON_IsNumber(gx)) out->grid_x = gx->valueint;
    if (cJSON_IsNumber(gy)) out->grid_y = gy->valueint;

    // Observation stations URL — we need to follow this to get station ID
    const cJSON *stations_url = cJSON_GetObjectItemCaseSensitive(props, "observationStations");
    if (cJSON_IsString(stations_url) && stations_url->valuestring) {
        snprintf(station_list_url, url_len, "%s", stations_url->valuestring);
    }
}

/**
 * @brief Parse a /points response into @p out, then resolve the nearest observation station.
 * @version 2
 * @req REQ-WX-001
 * @return ESP_OK when office and grid were found; ESP_ERR_INVALID_RESPONSE otherwise.
 */
static esp_err_t parse_points_response(const char *json_str, nws_location_t *out)
{
    cJSON *root = cJSON_Parse(json_str);
    if (!root) {
        ESP_LOGE(TAG, "JSON parse failed");
        return ESP_ERR_INVALID_RESPONSE;
    }

    cJSON *props = cJSON_GetObjectItemCaseSensitive(root, "properties");
    if (!props) {
        ESP_LOGE(TAG, "no 'properties' in response");
        cJSON_Delete(root);
        return ESP_ERR_INVALID_RESPONSE;
    }

    char station_list_url[256] = "";
    parse_points_props(props, out, station_list_url, sizeof(station_list_url));
    cJSON_Delete(root);

    if (out->office[0] == '\0' || out->grid_x == 0) {
        ESP_LOGE(TAG, "incomplete /points response");
        return ESP_ERR_INVALID_RESPONSE;
    }

    ESP_LOGI(TAG, "/points: office=%s grid=%d,%d", out->office, out->grid_x, out->grid_y);

    // Fetch observation stations list to get nearest station
    if (station_list_url[0] != '\0') {
        fetch_nearest_station(station_list_url, out);
    }

    // Fallback if station fetch failed
    if (out->station[0] == '\0') {
        ESP_LOGW(TAG, "could not determine station — using office as fallback");
        snprintf(out->station, sizeof(out->station), "K%s", out->office);
    }

    out->resolved = true;
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

/**
 * @brief Resolve lat/lon to NWS office, grid and nearest station (NVS cache first, then /points).
 * @version 1
 * @req REQ-WX-001
 * @return ESP_OK when resolved; ESP_ERR_HTTP_CONNECT or ESP_ERR_INVALID_RESPONSE on failure.
 */
esp_err_t nws_resolve_location(float lat, float lon, nws_location_t *out)
{
    memset(out, 0, sizeof(*out));
    out->lat = lat;
    out->lon = lon;

    // Check NVS cache first
    if (location_cached(lat, lon, out)) {
        return ESP_OK;
    }

    // Fetch from NWS API
    char url[128];
    snprintf(url, sizeof(url), NWS_POINTS_URL_FMT, lat, lon);
    ESP_LOGI(TAG, "resolving: %s", url);

    char *json = nws_http_fetch_json(TAG, url);
    if (!json) {
        ESP_LOGE(TAG, "failed to fetch /points");
        return ESP_ERR_HTTP_CONNECT;
    }

    esp_err_t err = parse_points_response(json, out);
    free(json);

    if (err == ESP_OK) {
        err = location_cache_save(out);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "NVS cache save failed: %s (non-fatal)", esp_err_to_name(err));
            err = ESP_OK;  // location is resolved, cache failure is non-fatal
        }
    }

    return err;
}
