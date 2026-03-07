// components/nws/src/nws_forecast.c
//
// Fetch 7-day forecast from NWS gridpoints endpoint.

#include "nws_internal.h"

static const char *TAG = "nws_fc";

#define NWS_FORECAST_URL_FMT  "https://api.weather.gov/gridpoints/%s/%d,%d/forecast"

static void parse_period(const cJSON *period_json, nws_forecast_period_t *out)
{
    json_get_str(period_json, "name", out->name, sizeof(out->name));
    json_get_str(period_json, "detailedForecast", out->detail_forecast,
                 sizeof(out->detail_forecast));
    json_get_str(period_json, "shortForecast", out->short_forecast,
                 sizeof(out->short_forecast));
    json_get_str(period_json, "windSpeed", out->wind_speed, sizeof(out->wind_speed));
    json_get_str(period_json, "windDirection", out->wind_direction,
                 sizeof(out->wind_direction));

    const cJSON *temp = cJSON_GetObjectItemCaseSensitive(period_json, "temperature");
    if (cJSON_IsNumber(temp)) out->temperature = temp->valueint;

    const cJSON *unit = cJSON_GetObjectItemCaseSensitive(period_json, "temperatureUnit");
    if (cJSON_IsString(unit) && unit->valuestring && unit->valuestring[0]) {
        out->temp_unit = unit->valuestring[0];
    } else {
        out->temp_unit = 'F';
    }

    const cJSON *daytime = cJSON_GetObjectItemCaseSensitive(period_json, "isDaytime");
    out->is_daytime = cJSON_IsTrue(daytime);
}

static esp_err_t parse_forecast(const char *json_str, nws_forecast_t *out)
{
    cJSON *root = cJSON_Parse(json_str);
    if (!root) {
        ESP_LOGE(TAG, "JSON parse failed");
        return ESP_ERR_INVALID_RESPONSE;
    }

    cJSON *props = cJSON_GetObjectItemCaseSensitive(root, "properties");
    if (!props) {
        ESP_LOGE(TAG, "no 'properties' in forecast response");
        cJSON_Delete(root);
        return ESP_ERR_INVALID_RESPONSE;
    }

    const cJSON *periods = cJSON_GetObjectItemCaseSensitive(props, "periods");
    if (!cJSON_IsArray(periods)) {
        ESP_LOGE(TAG, "no 'periods' array in forecast");
        cJSON_Delete(root);
        return ESP_ERR_INVALID_RESPONSE;
    }

    int count = cJSON_GetArraySize(periods);
    if (count > 14) count = 14;
    out->period_count = count;

    for (int i = 0; i < count; i++) {
        const cJSON *p = cJSON_GetArrayItem(periods, i);
        parse_period(p, &out->periods[i]);
    }

    out->valid = true;
    cJSON_Delete(root);

    ESP_LOGI(TAG, "forecast: %d periods, first=%s %d°%c \"%s\"",
             count, out->periods[0].name, out->periods[0].temperature,
             out->periods[0].temp_unit, out->periods[0].short_forecast);
    return ESP_OK;
}

esp_err_t nws_fetch_forecast(const nws_location_t *loc, nws_forecast_t *out)
{
    memset(out, 0, sizeof(*out));

    char url[128];
    snprintf(url, sizeof(url), NWS_FORECAST_URL_FMT,
             loc->office, loc->grid_x, loc->grid_y);

    char *json = nws_http_fetch_json(TAG, url);
    if (!json) return ESP_ERR_HTTP_CONNECT;

    esp_err_t err = parse_forecast(json, out);
    free(json);
    return err;
}
