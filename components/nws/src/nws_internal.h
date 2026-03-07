// components/nws/src/nws_internal.h
//
// Shared internals for NWS component source files.

#ifndef NWS_INTERNAL_H
#define NWS_INTERNAL_H

#include "nws.h"
#include "esp_http_client.h"
#include "http_client_utils.h"
#include "esp_log.h"
#include "esp_crt_bundle.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdlib.h>

// NWS API requires a User-Agent header identifying the application.
#define NWS_USER_AGENT  "(greenwood-clock, vanfosst@gmail.com)"

#define NWS_CONNECT_TIMEOUT_MS  5000
#define NWS_READ_TIMEOUT_MS     10000
#define NWS_MAX_RETRIES         3
#define NWS_RETRY_DELAY_MS      2000

/**
 * @brief Fetch JSON from a URL with NWS-standard headers and retry logic.
 *
 * Returns a malloc'd null-terminated buffer on success (caller must free).
 * Returns NULL on failure after all retries exhausted.
 */
static inline char *nws_http_fetch_json(const char *tag, const char *url)
{
    esp_http_client_config_t cfg = {
        .url               = url,
        .transport_type    = HTTP_TRANSPORT_OVER_SSL,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms        = NWS_READ_TIMEOUT_MS,
        .user_agent        = NWS_USER_AGENT,
    };

    for (int attempt = 0; attempt < NWS_MAX_RETRIES; attempt++) {
        if (attempt > 0) {
            ESP_LOGW(tag, "retry %d/%d for %s", attempt + 1, NWS_MAX_RETRIES, url);
            vTaskDelay(pdMS_TO_TICKS(NWS_RETRY_DELAY_MS * attempt));
        }

        size_t len = 0;
        http_attempt_result_t result;
        char *buf = http_request_execute(&cfg, &result, &len);

        if (result == HTTP_ATTEMPT_OK) return buf;
        if (result == HTTP_ATTEMPT_ABORT) break;
        if (result == HTTP_ATTEMPT_RATE_LIMIT) break;
        free(buf);  // may be NULL, free(NULL) is safe
    }

    ESP_LOGE(tag, "all attempts failed for %s", url);
    return NULL;
}

/**
 * @brief Safely extract a string from a cJSON object.
 * Copies into dst, ensuring null termination.  No-op if key missing or null.
 */
static inline void json_get_str(const cJSON *obj, const char *key,
                                 char *dst, size_t dst_size)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (cJSON_IsString(item) && item->valuestring) {
        strncpy(dst, item->valuestring, dst_size - 1);
        dst[dst_size - 1] = '\0';
    }
}

/**
 * @brief Safely extract a float from a nested {value: N} object.
 * NWS wraps numeric values in {"value": 12.3, "unitCode": "..."}.
 * Returns fallback if missing or null.
 */
static inline float json_get_value_float(const cJSON *obj, const char *key,
                                          float fallback)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (!item) return fallback;
    const cJSON *val = cJSON_GetObjectItemCaseSensitive(item, "value");
    if (cJSON_IsNumber(val)) return (float)val->valuedouble;
    return fallback;
}

/**
 * @brief Safely extract an int from a nested {value: N} object.
 */
static inline int json_get_value_int(const cJSON *obj, const char *key,
                                      int fallback)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
    if (!item) return fallback;
    const cJSON *val = cJSON_GetObjectItemCaseSensitive(item, "value");
    if (cJSON_IsNumber(val)) return val->valueint;
    return fallback;
}

#endif // NWS_INTERNAL_H
