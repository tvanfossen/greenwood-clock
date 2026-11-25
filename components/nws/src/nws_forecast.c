/**
 * @file nws_forecast.c
 * v2: each period's chance of precipitation.
 * @version 2
 * @brief Fetch the 7-day forecast from the NWS gridpoints endpoint.
 */

#include "nws_internal.h"

static const char *TAG = "nws_fc";

#define NWS_FORECAST_URL_FMT  "https://api.weather.gov/gridpoints/%s/%d,%d/forecast"

/**
 * @brief Copy one forecast period (name, detailed/short forecast, wind,
 *        temperature, unit defaulting to 'F', isDaytime, chance of precipitation) into @p out.
 * v2: probabilityOfPrecipitation.value (-1 when absent or null).
 * @version 2
 * @req REQ-WX-003
 */
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

    const cJSON *pop = cJSON_GetObjectItemCaseSensitive(period_json, "probabilityOfPrecipitation");
    const cJSON *pop_v = pop ? cJSON_GetObjectItemCaseSensitive(pop, "value") : NULL;
    out->precip_pct = cJSON_IsNumber(pop_v) ? pop_v->valueint : -1;   // null: none given
}

/**
 * @brief Parse a gridpoints forecast response into @p out, keeping at most 14 periods.
 * @version 1
 * @req REQ-WX-003
 * @return ESP_OK on success, ESP_ERR_INVALID_RESPONSE if the JSON does not parse
 *         or lacks properties / a periods array.
 */
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

/**
 * @brief Fetch and parse the forecast for @p loc's office and grid point.
 *
 * @p out is zeroed first, so on failure valid=false.
 * @version 1
 * @req REQ-WX-003
 * @return ESP_OK on success, ESP_ERR_HTTP_CONNECT if the fetch failed, or
 *         ESP_ERR_INVALID_RESPONSE on a malformed response.
 */
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
