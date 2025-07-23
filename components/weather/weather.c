// components/weather/weather.c

#include "weather.h"
#include "secrets.h"            // defines WEATHER_API_KEY
#include "esp_log.h"
#include "esp_crt_bundle.h"     // for esp_crt_bundle_attach()
#include "esp_http_client.h"
#include "cJSON.h"
#include <string.h>
#include <stdlib.h>
#include <inttypes.h>

static const char *TAG = "weather";

esp_err_t weather_init(void)
{
    ESP_LOGI(TAG, "weather_init: API key = %s…", WEATHER_API_KEY[0] ? "[redacted]" : "[none]");
    return ESP_OK;
}

static bool copy_json_string(cJSON *item, char *dst, size_t dstlen) {
    if (cJSON_IsString(item) && item->valuestring) {
        strncpy(dst, item->valuestring, dstlen - 1);
        dst[dstlen - 1] = '\0';
        return true;
    }
    if (dstlen) dst[0] = '\0';
    return false;
}

esp_err_t weather_fetch(float latitude,
                        float longitude,
                        weather_data_t *out_data)
{
    if (!out_data) return ESP_ERR_INVALID_ARG;
    if (!WEATHER_API_KEY[0]) {
        ESP_LOGE(TAG, "weather_fetch: no API key");
        return ESP_ERR_INVALID_STATE;
    }

    // 1) Build URL
    char url[128];
    snprintf(url, sizeof(url),
            "https://api.weatherbit.io/v2.0/current?"
            "lat=%.3f&lon=%.3f&key=%s",
            latitude, longitude,
            WEATHER_API_KEY);
    ESP_LOGI(TAG, "weather_fetch: URL=%s", url);

    // 2) HTTPS client config
    esp_http_client_config_t cfg = {
        .url               = url,
        .transport_type    = HTTP_TRANSPORT_OVER_SSL,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        ESP_LOGE(TAG, "http_client_init failed");
        return ESP_FAIL;
    }

    // 3) Open + fetch headers
    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "http_client_open: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return err;
    }
    int clen = esp_http_client_fetch_headers(client);
    ESP_LOGI(TAG, "weather_fetch: content_length=%d", clen);

    // 4) Read body into dynamically growing buffer
    size_t cap   = (clen > 0 && clen < 32*1024) ? clen + 1 : 4096;
    size_t total = 0;
    char *buf    = malloc(cap);
    if (!buf) {
        ESP_LOGE(TAG, "malloc(%d) failed", cap);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_ERR_NO_MEM;
    }
    while (true) {
        int r = esp_http_client_read(client, buf + total, cap - total - 1);
        if (r <= 0) break;
        total += r;
        if (total >= cap - 1) {
            size_t newcap = cap * 2;
            char *tmp = realloc(buf, newcap);
            if (!tmp) break;
            buf = tmp;
            cap = newcap;
        }
    }
    buf[total] = '\0';
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    ESP_LOGI(TAG, "weather_fetch: received %u bytes JSON", total);
    ESP_LOGI(TAG, "weather_fetch: payload:\n%s", buf);

    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        ESP_LOGE(TAG, "JSON parse error");
        free(buf);
        return ESP_ERR_INVALID_ARG;
    }

    // 6) Drill into data[0]
    cJSON *data_arr = cJSON_GetObjectItem(root, "data");
    if (!cJSON_IsArray(data_arr) || cJSON_GetArraySize(data_arr) < 1) {
        ESP_LOGE(TAG, "Missing 'data' array");
        cJSON_Delete(root);
        return ESP_ERR_INVALID_ARG;
    }
    cJSON *item = cJSON_GetArrayItem(data_arr, 0);

    // Numeric fields
    cJSON *tmp;
    #define PARSE_NUM(field, ckey, cast) \
        tmp = cJSON_GetObjectItemCaseSensitive(item, ckey); \
        out_data->field = cJSON_IsNumber(tmp) ? (cast)tmp->valuedouble : 0;

    PARSE_NUM(temp_c,     "temp",      float)
    PARSE_NUM(feels_like_c,"app_temp",  float)
    PARSE_NUM(dew_point_c,"dewpt",     float)
    PARSE_NUM(humidity,   "rh",        int)
    PARSE_NUM(precip_mm,  "precip",    float)
    PARSE_NUM(pressure_mb,"pres",      float)
    PARSE_NUM(clouds,     "clouds",    int)
    PARSE_NUM(visibility_km,"vis",     float)
    PARSE_NUM(uv_index,   "uv",        float)
    PARSE_NUM(aqi,        "aqi",       int)
    PARSE_NUM(wind_spd_m_s,"wind_spd",  float)
    PARSE_NUM(wind_gust_m_s,"gust",     float)
    PARSE_NUM(wind_dir_deg,"wind_dir",  int)
    PARSE_NUM(solar_ghi,  "ghi",       float)
    PARSE_NUM(solar_dhi,  "dhi",       float)
    PARSE_NUM(solar_dni,  "dni",       float)
    PARSE_NUM(solar_rad,  "solar_rad", float)
    PARSE_NUM(elev_angle, "elev_angle",float)
    PARSE_NUM(h_angle,    "h_angle",   float)

    // String fields
    copy_json_string(cJSON_GetObjectItem(item, "city_name"),
                     out_data->city_name, sizeof(out_data->city_name));
    copy_json_string(cJSON_GetObjectItem(item, "state_code"),
                     out_data->state_code, sizeof(out_data->state_code));
    copy_json_string(cJSON_GetObjectItem(item, "country_code"),
                     out_data->country_code, sizeof(out_data->country_code));
    copy_json_string(cJSON_GetObjectItem(item, "timezone"),
                     out_data->timezone, sizeof(out_data->timezone));
    copy_json_string(cJSON_GetObjectItem(item, "datetime"),
                     out_data->datetime, sizeof(out_data->datetime));
    copy_json_string(cJSON_GetObjectItem(item, "ob_time"),
                     out_data->ob_time, sizeof(out_data->ob_time));
    copy_json_string(cJSON_GetObjectItem(item, "station"),
                     out_data->station, sizeof(out_data->station));
    copy_json_string(cJSON_GetObjectItem(item, "sunrise"),
                     out_data->sunrise, sizeof(out_data->sunrise));
    copy_json_string(cJSON_GetObjectItem(item, "sunset"),
                     out_data->sunset, sizeof(out_data->sunset));

    // Unix timestamp
    tmp = cJSON_GetObjectItemCaseSensitive(item, "ts");
    out_data->timestamp = cJSON_IsNumber(tmp) ? tmp->valueint : 0;

    // Weather nested object
    cJSON *w = cJSON_GetObjectItemCaseSensitive(item, "weather");
    copy_json_string(cJSON_GetObjectItem(w, "description"),
                     out_data->description, sizeof(out_data->description));
    if (cJSON_IsObject(w)) {
        cJSON *icon = cJSON_GetObjectItemCaseSensitive(w, "icon");
        if (cJSON_IsString(icon) && icon->valuestring) {
            snprintf(out_data->icon_url,
                     sizeof(out_data->icon_url),
                     "https://www.weatherbit.io/static/img/icons/%s.png",
                     icon->valuestring);
        } else {
            out_data->icon_url[0] = '\0';
        }
    }

    cJSON_Delete(root);
    ESP_LOGI(TAG, "weather_fetch: parsed city=%s temp=%.1f°C feels=%.1f°C desc=\"%s\"",
             out_data->city_name,
             out_data->temp_c,
             out_data->feels_like_c,
             out_data->description);

    return ESP_OK;
}

esp_err_t weather_fetch_icon(const weather_data_t *wd,
                             uint8_t **out_buf,
                             size_t  *out_len)
{
    if (!wd || !wd->icon_url[0] || !out_buf || !out_len) {
        return ESP_ERR_INVALID_ARG;
    }
    ESP_LOGI(TAG, "weather icon url: %s", wd->icon_url);
    esp_http_client_config_t cfg = {
        .url               = wd->icon_url,
        .transport_type    = HTTP_TRANSPORT_OVER_SSL,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) return ESP_FAIL;

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        esp_http_client_cleanup(client);
        return err;
    }
    esp_http_client_fetch_headers(client);

    // Start with a reasonable chunk; grow as needed
    size_t cap   = 4096;
    size_t total = 0;
    uint8_t *buf = malloc(cap);
    if (!buf) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return ESP_ERR_NO_MEM;
    }

    // Read until EOF
    int r;
    while ((r = esp_http_client_read(client, (char*)buf + total, cap - total)) > 0) {
        total += r;
        if (total >= cap) {
            size_t newcap = cap * 2;
            uint8_t *tmp = realloc(buf, newcap);
            if (!tmp) break;   // out of memory: we'll use what we have
            buf = tmp;
            cap = newcap;
        }
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    *out_buf = buf;
    *out_len = total;
    ESP_LOGI("weather", "weather_fetch_icon: downloaded %u bytes", (unsigned)total);
    return ESP_OK;
}