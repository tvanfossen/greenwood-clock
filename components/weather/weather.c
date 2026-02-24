// components/weather/weather.c

#include "weather.h"
#include "http_client_utils.h"
#include "secrets.h"
#include "esp_log.h"
#include "esp_crt_bundle.h"
#include "cJSON.h"
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>

static const char *TAG = "weather";

#define HTTP_CONNECT_TIMEOUT_MS   5000
#define HTTP_READ_TIMEOUT_MS      10000
#define HTTP_MAX_RETRIES          3
#define HTTP_RETRY_DELAY_MS       2000
#define HTTP_RATE_LIMIT_DELAY_MS  60000

// ─── Private: weather_fetch helpers ──────────────────────────────────────────

/**
 * @brief Validate arguments and API key before a weather fetch.
 *
 * @param out_data  Output pointer to validate (must be non-NULL).
 * @return ESP_OK if preconditions are met.
 *         ESP_ERR_INVALID_ARG if out_data is NULL.
 *         ESP_ERR_INVALID_STATE if no API key is configured.
 */
static esp_err_t weather_check_preconditions(const weather_data_t *out_data)
{
    if (!out_data) {
        ESP_LOGE(TAG, "weather_check_preconditions: out_data is NULL");
        return ESP_ERR_INVALID_ARG;
    }
    if (!WEATHER_API_KEY[0]) {
        ESP_LOGE(TAG, "weather_check_preconditions: no API key configured in secrets.h");
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}

/**
 * @brief Build the Weatherbit current-conditions API URL.
 *
 * @param buf     Destination buffer.
 * @param buflen  Size of destination buffer.
 * @param lat     Latitude in decimal degrees.
 * @param lon     Longitude in decimal degrees.
 */
static void weather_build_url(char *buf, size_t buflen, float lat, float lon)
{
    snprintf(buf, buflen,
             "https://api.weatherbit.io/v2.0/current?lat=%.3f&lon=%.3f&key=%s",
             lat, lon, WEATHER_API_KEY);
}

/**
 * @brief Fetch the weather API URL with retry and exponential back-off.
 *
 * Handles HTTP 429 rate-limit delays. On success returns a heap buffer
 * containing the null-terminated JSON response body; caller must free.
 *
 * @param url  Fully-formed API URL.
 * @return Heap buffer on success, NULL if all attempts fail.
 */
static char *weather_http_fetch_json(const char *url)
{
    esp_http_client_config_t cfg = {
        .url               = url,
        .transport_type    = HTTP_TRANSPORT_OVER_SSL,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms        = HTTP_READ_TIMEOUT_MS,
    };

    for (int retry = 0; retry < HTTP_MAX_RETRIES; retry++) {
        if (retry > 0)
            ESP_LOGW(TAG, "weather_http_fetch_json: attempt %d/%d",
                     retry + 1, HTTP_MAX_RETRIES);

        size_t len = 0;
        http_attempt_result_t result;
        char *buf = http_request_execute(&cfg, &result, &len);

        if (result == HTTP_ATTEMPT_OK)         return buf;
        if (result == HTTP_ATTEMPT_ABORT)      break;
        if (result == HTTP_ATTEMPT_RATE_LIMIT) {
            ESP_LOGW(TAG, "weather_http_fetch_json: rate limited, waiting %dms",
                     HTTP_RATE_LIMIT_DELAY_MS);
            vTaskDelay(pdMS_TO_TICKS(HTTP_RATE_LIMIT_DELAY_MS));
        } else {
            vTaskDelay(pdMS_TO_TICKS(HTTP_RETRY_DELAY_MS * retry));
        }
    }

    ESP_LOGE(TAG, "weather_http_fetch_json: all %d attempts failed", HTTP_MAX_RETRIES);
    return NULL;
}

// ─── Private: JSON field parsers ─────────────────────────────────────────────

/**
 * @brief Copy a cJSON string value into a fixed C buffer.
 *
 * @param item    cJSON item (may be NULL or a non-string type).
 * @param dst     Destination buffer.
 * @param dstlen  Size of destination buffer.
 * @return true if a non-empty string was copied, false otherwise.
 */
static bool copy_json_string(cJSON *item, char *dst, size_t dstlen)
{
    if (cJSON_IsString(item) && item->valuestring) {
        strncpy(dst, item->valuestring, dstlen - 1);
        dst[dstlen - 1] = '\0';
        return true;
    }
    if (dstlen) dst[0] = '\0';
    return false;
}

/**
 * @brief Macro: assign a numeric cJSON field to a struct member with a type cast.
 *
 * Requires a local `cJSON *tmp` to be declared in the calling scope.
 */
#define PARSE_NUM(field, key, cast) \
    tmp = cJSON_GetObjectItemCaseSensitive(item, key); \
    out->field = cJSON_IsNumber(tmp) ? (cast)tmp->valuedouble : 0;

/**
 * @brief Parse atmospheric measurement fields: temperature, humidity, precipitation.
 *
 * Fills: temp_c, feels_like_c, dew_point_c, humidity, precip_mm, pressure_mb.
 *
 * @param item cJSON data object (index 0 of the "data" array).
 * @param out  Destination weather_data_t.
 */
static void weather_parse_atmospheric(cJSON *item, weather_data_t *out)
{
    cJSON *tmp;
    PARSE_NUM(temp_c,       "temp",     float)
    PARSE_NUM(feels_like_c, "app_temp", float)
    PARSE_NUM(dew_point_c,  "dewpt",    float)
    PARSE_NUM(humidity,     "rh",       int)
    PARSE_NUM(precip_mm,    "precip",   float)
    PARSE_NUM(pressure_mb,  "pres",     float)
}

/**
 * @brief Parse sky and visibility fields.
 *
 * Fills: clouds, visibility_km, uv_index, aqi.
 *
 * @param item cJSON data object.
 * @param out  Destination weather_data_t.
 */
static void weather_parse_sky(cJSON *item, weather_data_t *out)
{
    cJSON *tmp;
    PARSE_NUM(clouds,        "clouds", int)
    PARSE_NUM(visibility_km, "vis",    float)
    PARSE_NUM(uv_index,      "uv",     float)
    PARSE_NUM(aqi,           "aqi",    int)
}

/**
 * @brief Parse wind numeric fields.
 *
 * Fills: wind_spd_m_s, wind_gust_m_s, wind_dir_deg.
 *
 * @param item cJSON data object.
 * @param out  Destination weather_data_t.
 */
static void weather_parse_wind(cJSON *item, weather_data_t *out)
{
    cJSON *tmp;
    PARSE_NUM(wind_spd_m_s,  "wind_spd", float)
    PARSE_NUM(wind_gust_m_s, "gust",     float)
    PARSE_NUM(wind_dir_deg,  "wind_dir", int)
}

/**
 * @brief Parse solar radiation numeric fields.
 *
 * Fills: solar_ghi, solar_dhi, solar_dni, solar_rad, elev_angle, h_angle.
 *
 * @param item cJSON data object.
 * @param out  Destination weather_data_t.
 */
static void weather_parse_solar(cJSON *item, weather_data_t *out)
{
    cJSON *tmp;
    PARSE_NUM(solar_ghi,  "ghi",        float)
    PARSE_NUM(solar_dhi,  "dhi",        float)
    PARSE_NUM(solar_dni,  "dni",        float)
    PARSE_NUM(solar_rad,  "solar_rad",  float)
    PARSE_NUM(elev_angle, "elev_angle", float)
    PARSE_NUM(h_angle,    "h_angle",    float)
}

#undef PARSE_NUM

/**
 * @brief Parse location identity string fields.
 *
 * Fills: city_name, state_code, country_code, timezone.
 *
 * @param item cJSON data object.
 * @param out  Destination weather_data_t.
 */
static void weather_parse_place_strings(cJSON *item, weather_data_t *out)
{
    copy_json_string(cJSON_GetObjectItem(item, "city_name"),
                     out->city_name,    sizeof(out->city_name));
    copy_json_string(cJSON_GetObjectItem(item, "state_code"),
                     out->state_code,   sizeof(out->state_code));
    copy_json_string(cJSON_GetObjectItem(item, "country_code"),
                     out->country_code, sizeof(out->country_code));
    copy_json_string(cJSON_GetObjectItem(item, "timezone"),
                     out->timezone,     sizeof(out->timezone));
}

/**
 * @brief Parse primary observation time string fields.
 *
 * Fills: datetime, ob_time, station.
 *
 * @param item cJSON data object.
 * @param out  Destination weather_data_t.
 */
static void weather_parse_time_dates(cJSON *item, weather_data_t *out)
{
    copy_json_string(cJSON_GetObjectItem(item, "datetime"),
                     out->datetime, sizeof(out->datetime));
    copy_json_string(cJSON_GetObjectItem(item, "ob_time"),
                     out->ob_time,  sizeof(out->ob_time));
    copy_json_string(cJSON_GetObjectItem(item, "station"),
                     out->station,  sizeof(out->station));
}

/**
 * @brief Parse secondary time fields: sun events and Unix timestamp.
 *
 * Fills: sunrise, sunset, timestamp.
 *
 * @param item cJSON data object.
 * @param out  Destination weather_data_t.
 */
static void weather_parse_time_extras(cJSON *item, weather_data_t *out)
{
    copy_json_string(cJSON_GetObjectItem(item, "sunrise"),
                     out->sunrise, sizeof(out->sunrise));
    copy_json_string(cJSON_GetObjectItem(item, "sunset"),
                     out->sunset,  sizeof(out->sunset));
    cJSON *ts = cJSON_GetObjectItemCaseSensitive(item, "ts");
    out->timestamp = cJSON_IsNumber(ts) ? ts->valueint : 0;
}

/**
 * @brief Parse all observation time string fields and the Unix timestamp.
 *
 * Fills: datetime, ob_time, station, sunrise, sunset, timestamp.
 *
 * @param item cJSON data object.
 * @param out  Destination weather_data_t.
 */
static void weather_parse_time_strings(cJSON *item, weather_data_t *out)
{
    weather_parse_time_dates(item, out);
    weather_parse_time_extras(item, out);
}

/**
 * @brief Parse the nested "weather" object for description and icon URL.
 *
 * Fills: description, icon_url.
 *
 * @param item cJSON data object.
 * @param out  Destination weather_data_t.
 */
static void weather_parse_conditions(cJSON *item, weather_data_t *out)
{
    cJSON *w = cJSON_GetObjectItemCaseSensitive(item, "weather");
    if (!cJSON_IsObject(w)) {
        ESP_LOGW(TAG, "weather_parse_conditions: 'weather' object missing");
        out->description[0] = '\0';
        out->icon_url[0]    = '\0';
        return;
    }
    copy_json_string(cJSON_GetObjectItem(w, "description"),
                     out->description, sizeof(out->description));
    cJSON *icon = cJSON_GetObjectItemCaseSensitive(w, "icon");
    if (cJSON_IsString(icon) && icon->valuestring) {
        snprintf(out->icon_url, sizeof(out->icon_url),
                 "https://www.weatherbit.io/static/img/icons/%s.png",
                 icon->valuestring);
    } else {
        out->icon_url[0] = '\0';
    }
}

/**
 * @brief Populate all fields of a weather_data_t from a cJSON data item.
 *
 * Orchestrates all typed field parsers. Does not touch the cJSON tree after return.
 *
 * @param item cJSON data object (index 0 of the "data" array).
 * @param out  Destination weather_data_t.
 */
static void weather_populate_fields(cJSON *item, weather_data_t *out)
{
    weather_parse_atmospheric(item, out);
    weather_parse_sky(item, out);
    weather_parse_wind(item, out);
    weather_parse_solar(item, out);
    weather_parse_place_strings(item, out);
    weather_parse_time_strings(item, out);
    weather_parse_conditions(item, out);
}

/**
 * @brief Extract the first element of the "data" array from a Weatherbit JSON root.
 *
 * @param root  Parsed cJSON root object.
 * @return Pointer to the first data item, or NULL if the array is missing or empty.
 */
static cJSON *weather_extract_data_item(cJSON *root)
{
    cJSON *arr = cJSON_GetObjectItem(root, "data");
    if (!cJSON_IsArray(arr) || cJSON_GetArraySize(arr) < 1) {
        ESP_LOGE(TAG, "weather_extract_data_item: 'data' array missing or empty");
        return NULL;
    }
    return cJSON_GetArrayItem(arr, 0);
}

/**
 * @brief Parse a complete Weatherbit API JSON response into a weather_data_t.
 *
 * @param buf      Null-terminated JSON response body.
 * @param out_data Destination structure to populate.
 * @return ESP_OK on success.
 *         ESP_ERR_INVALID_ARG if JSON cannot be parsed or schema is unexpected.
 */
static esp_err_t weather_parse_response(const char *buf, weather_data_t *out_data)
{
    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        ESP_LOGE(TAG, "weather_parse_response: cJSON_Parse failed");
        return ESP_ERR_INVALID_ARG;
    }

    cJSON *item = weather_extract_data_item(root);
    if (!item) {
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }

    weather_populate_fields(item, out_data);
    cJSON_Delete(root);

    ESP_LOGI(TAG, "weather_parse_response: %s %.1f°C (feels %.1f°C) — %s",
             out_data->city_name, out_data->temp_c,
             out_data->feels_like_c, out_data->description);
    return ESP_OK;
}

// ─── Public API ───────────────────────────────────────────────────────────────

/**
 * @brief Initialize the weather component.
 *
 * Validates compile-time API key configuration. The key is sourced from
 * secrets.h (WEATHER_API_KEY) and is never stored at runtime.
 *
 * @return ESP_OK always. Key absence is logged but not fatal at init time.
 */
esp_err_t weather_init(void)
{
    ESP_LOGI(TAG, "weather_init: API key %s",
             WEATHER_API_KEY[0] ? "present" : "NOT configured — fetches will fail");
    return ESP_OK;
}

/**
 * @brief Fetch current weather conditions for a given location.
 *
 * Performs an HTTPS GET to the Weatherbit v2.0 current-conditions endpoint.
 * Retries up to HTTP_MAX_RETRIES times with exponential back-off.
 * HTTP 429 triggers an extended rate-limit delay before the next attempt.
 *
 * @param latitude   Decimal degrees latitude.
 * @param longitude  Decimal degrees longitude.
 * @param out_data   Pointer to weather_data_t to populate on success.
 * @return ESP_OK on success.
 *         ESP_ERR_INVALID_ARG if out_data is NULL or JSON parse fails.
 *         ESP_ERR_INVALID_STATE if no API key is configured.
 *         ESP_FAIL if all HTTP attempts fail.
 */
esp_err_t weather_fetch(float latitude, float longitude, weather_data_t *out_data)
{
    esp_err_t err = weather_check_preconditions(out_data);
    if (err != ESP_OK) return err;

    char url[128];
    weather_build_url(url, sizeof(url), latitude, longitude);
    ESP_LOGI(TAG, "weather_fetch: starting fetch");

    char *buf = weather_http_fetch_json(url);
    if (!buf) {
        ESP_LOGE(TAG, "weather_fetch: all HTTP attempts failed");
        return ESP_FAIL;
    }

    err = weather_parse_response(buf, out_data);
    free(buf);
    return err;
}

/**
 * @brief Execute one icon download attempt and store the result on success.
 *
 * On HTTP_ATTEMPT_OK the heap buffer is transferred to *out_buf / *out_len.
 * All other results leave *out_buf / *out_len unchanged.
 *
 * @param cfg      HTTP client configuration for this attempt.
 * @param out_buf  Set to the raw PNG heap buffer on success.
 * @param out_len  Set to the PNG byte count on success.
 * @return Attempt disposition (HTTP_ATTEMPT_OK, HTTP_ATTEMPT_RETRY, etc.).
 */
static http_attempt_result_t icon_attempt_one(const esp_http_client_config_t *cfg,
                                               uint8_t **out_buf,
                                               size_t   *out_len)
{
    size_t len = 0;
    http_attempt_result_t result;
    char *buf = http_request_execute(cfg, &result, &len);
    if (result == HTTP_ATTEMPT_OK) {
        ESP_LOGI(TAG, "weather_fetch_icon: downloaded %zu bytes", len);
        *out_buf = (uint8_t *)buf;
        *out_len = len;
    }
    return result;
}

/**
 * @brief Retry loop for icon download: attempt up to HTTP_MAX_RETRIES times.
 *
 * Applies standard back-off delay between retries. Aborts immediately on a
 * permanent client error (HTTP 4xx). Returns ESP_OK only when icon data was
 * successfully received and stored in *out_buf / *out_len.
 *
 * @param cfg      HTTP client configuration shared across all attempts.
 * @param out_buf  Set to the raw PNG heap buffer on success.
 * @param out_len  Set to the PNG byte count on success.
 * @return ESP_OK on success; ESP_FAIL if all attempts fail or a client error occurs.
 */
static esp_err_t weather_fetch_icon_loop(const esp_http_client_config_t *cfg,
                                          uint8_t **out_buf,
                                          size_t   *out_len)
{
    for (int retry = 0; retry < HTTP_MAX_RETRIES; retry++) {
        if (retry > 0) {
            ESP_LOGW(TAG, "weather_fetch_icon: attempt %d/%d", retry + 1, HTTP_MAX_RETRIES);
            vTaskDelay(pdMS_TO_TICKS(HTTP_RETRY_DELAY_MS * retry));
        }
        http_attempt_result_t result = icon_attempt_one(cfg, out_buf, out_len);
        if (result == HTTP_ATTEMPT_OK)    return ESP_OK;
        if (result == HTTP_ATTEMPT_ABORT) {
            ESP_LOGE(TAG, "weather_fetch_icon: client error, not retrying");
            return ESP_FAIL;
        }
    }
    ESP_LOGE(TAG, "weather_fetch_icon: all %d attempts failed", HTTP_MAX_RETRIES);
    return ESP_FAIL;
}

/**
 * @brief Download the PNG weather condition icon for the given weather data.
 *
 * The icon URL is populated by weather_fetch() via the Weatherbit icon field.
 * Retries up to HTTP_MAX_RETRIES times with exponential back-off.
 * The caller is responsible for freeing *out_buf.
 *
 * @param wd       Populated weather_data_t containing a non-empty icon_url.
 * @param out_buf  Set to a heap-allocated buffer containing the raw PNG bytes.
 * @param out_len  Set to the byte count of *out_buf.
 * @return ESP_OK on success.
 *         ESP_ERR_INVALID_ARG if any argument is invalid.
 *         ESP_FAIL if all HTTP attempts fail.
 */
esp_err_t weather_fetch_icon(const weather_data_t *wd,
                             uint8_t **out_buf,
                             size_t   *out_len)
{
    if (!wd || !wd->icon_url[0] || !out_buf || !out_len) {
        ESP_LOGE(TAG, "weather_fetch_icon: invalid arguments");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "weather_fetch_icon: url=%s", wd->icon_url);

    esp_http_client_config_t cfg = {
        .url               = wd->icon_url,
        .transport_type    = HTTP_TRANSPORT_OVER_SSL,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms        = HTTP_READ_TIMEOUT_MS,
    };

    return weather_fetch_icon_loop(&cfg, out_buf, out_len);
}
