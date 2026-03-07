// components/nws/src/nws_conditions.c
//
// Fetch current weather conditions from NWS observations endpoint.

#include "nws_internal.h"

static const char *TAG = "nws_cond";

#define NWS_OBS_URL_FMT  "https://api.weather.gov/stations/%s/observations/latest"

// Wind direction degrees → cardinal string
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

    out->temp_c       = json_get_value_float(props, "temperature", -999.0f);
    out->feels_like_c = json_get_value_float(props, "windChill", out->temp_c);
    out->dew_point_c  = json_get_value_float(props, "dewpoint", -999.0f);
    out->humidity      = json_get_value_int(props, "relativeHumidity", -1);
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
        // Station URL: ".../stations/KMKG" → extract last segment
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

esp_err_t nws_fetch_conditions(const nws_location_t *loc, nws_conditions_t *out)
{
    memset(out, 0, sizeof(*out));

    char url[128];
    snprintf(url, sizeof(url), NWS_OBS_URL_FMT, loc->station);

    char *json = nws_http_fetch_json(TAG, url);
    if (!json) return ESP_ERR_HTTP_CONNECT;

    esp_err_t err = parse_conditions(json, out);
    free(json);
    return err;
}
