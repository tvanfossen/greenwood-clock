// components/nws/src/nws_radar.c
//
// Fetch radar imagery from NOAA ImageServer and Kp index from SWPC.

#include "nws_internal.h"
#include "esp_heap_caps.h"

static const char *TAG = "nws_radar";

// NOAA ArcGIS ImageServer — base reflectivity, transparent PNG
#define NWS_RADAR_URL_FMT \
    "https://mapservices.weather.noaa.gov/eventdriven/rest/services/" \
    "radar/radar_base_reflectivity_time/ImageServer/exportImage" \
    "?bbox=%.4f,%.4f,%.4f,%.4f" \
    "&bboxSR=4326&size=1024,600&format=png32&f=image"

#define NWS_KP_URL \
    "https://services.swpc.noaa.gov/products/noaa-planetary-k-index.json"

esp_err_t nws_fetch_radar(float lat, float lon, uint8_t **png_buf, size_t *png_len)
{
    *png_buf = NULL;
    *png_len = 0;

    // 4-degree bbox centered on location
    float lon_min = lon - 2.0f;
    float lat_min = lat - 2.0f;
    float lon_max = lon + 2.0f;
    float lat_max = lat + 2.0f;

    char url[512];
    snprintf(url, sizeof(url), NWS_RADAR_URL_FMT,
             lon_min, lat_min, lon_max, lat_max);

    esp_http_client_config_t cfg = {
        .url               = url,
        .transport_type    = HTTP_TRANSPORT_OVER_SSL,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms        = 15000,  // radar images can be slow
        .user_agent        = NWS_USER_AGENT,
    };

    for (int attempt = 0; attempt < NWS_MAX_RETRIES; attempt++) {
        if (attempt > 0) {
            ESP_LOGW(TAG, "radar retry %d/%d", attempt + 1, NWS_MAX_RETRIES);
            vTaskDelay(pdMS_TO_TICKS(NWS_RETRY_DELAY_MS * attempt));
        }

        size_t len = 0;
        http_attempt_result_t result;
        char *buf = http_request_execute(&cfg, &result, &len);

        if (result == HTTP_ATTEMPT_OK && buf && len > 0) {
            // Copy to SPIRAM so the original heap buffer can be freed
            uint8_t *spiram_buf = heap_caps_malloc(len, MALLOC_CAP_SPIRAM);
            if (!spiram_buf) {
                ESP_LOGE(TAG, "SPIRAM alloc failed for radar PNG (%zu bytes)", len);
                free(buf);
                return ESP_ERR_NO_MEM;
            }
            memcpy(spiram_buf, buf, len);
            free(buf);

            *png_buf = spiram_buf;
            *png_len = len;

            ESP_LOGI(TAG, "radar: %zu bytes fetched", len);
            return ESP_OK;
        }

        if (result == HTTP_ATTEMPT_ABORT || result == HTTP_ATTEMPT_RATE_LIMIT) {
            free(buf);
            break;
        }
        free(buf);
    }

    ESP_LOGE(TAG, "radar fetch failed after %d attempts", NWS_MAX_RETRIES);
    return ESP_ERR_HTTP_CONNECT;
}

esp_err_t nws_fetch_kp_index(float *kp_out)
{
    *kp_out = 0.0f;

    char *json = nws_http_fetch_json(TAG, NWS_KP_URL);
    if (!json) return ESP_ERR_HTTP_CONNECT;

    // Response is a JSON array of arrays:
    // [["time_tag","Kp","Kp_fraction","a_running","station_count"],
    //  ["2026-03-04 00:00:00.000","2","2.00","6","8"], ...]
    // Last entry is most recent.
    cJSON *root = cJSON_Parse(json);
    free(json);

    if (!root || !cJSON_IsArray(root)) {
        ESP_LOGE(TAG, "Kp JSON parse failed");
        cJSON_Delete(root);
        return ESP_ERR_INVALID_RESPONSE;
    }

    int count = cJSON_GetArraySize(root);
    if (count < 2) {
        // Need at least header row + one data row
        ESP_LOGE(TAG, "Kp data too short: %d entries", count);
        cJSON_Delete(root);
        return ESP_ERR_INVALID_RESPONSE;
    }

    // Last entry in array is most recent Kp reading
    const cJSON *last = cJSON_GetArrayItem(root, count - 1);
    if (!cJSON_IsArray(last) || cJSON_GetArraySize(last) < 3) {
        ESP_LOGE(TAG, "Kp last entry malformed");
        cJSON_Delete(root);
        return ESP_ERR_INVALID_RESPONSE;
    }

    // Index 1 = Kp value as string
    const cJSON *kp_item = cJSON_GetArrayItem(last, 1);
    if (cJSON_IsString(kp_item) && kp_item->valuestring) {
        *kp_out = strtof(kp_item->valuestring, NULL);
    } else if (cJSON_IsNumber(kp_item)) {
        *kp_out = (float)kp_item->valuedouble;
    }

    ESP_LOGI(TAG, "Kp index: %.1f", *kp_out);
    cJSON_Delete(root);
    return ESP_OK;
}
