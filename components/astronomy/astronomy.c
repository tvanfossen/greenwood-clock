#include "astronomy.h"
#include "secrets.h"
#include "esp_log.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "cJSON.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static const char *TAG = "astronomy";

static void astronomy_build_auth_header(char *out, size_t out_sz) {
    char raw[128];
    ESP_LOGI(TAG, "Building auth header with APP_ID='%s' and API_KEY='%s'", ASTRONOMY_APP_ID, ASTRONOMY_API_KEY);
    if (!out || out_sz < 16) {
        ESP_LOGE(TAG, "astronomy_build_auth_header: invalid output buffer");
        return;
    }
    snprintf(raw, sizeof(raw), "%s:%s", ASTRONOMY_APP_ID, ASTRONOMY_API_KEY);
    static const char b64_table[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    char encoded[192];
    size_t i, j;
    size_t srclen = strlen(raw);
    ESP_LOGI(TAG, "Base64 encoding %zu bytes for auth header", srclen);
    for (i = 0, j = 0; i < srclen && j + 4 < sizeof(encoded); i += 3) {
        unsigned v = raw[i] << 16;
        if (i + 1 < srclen) v |= raw[i + 1] << 8;
        if (i + 2 < srclen) v |= raw[i + 2];
        encoded[j++] = b64_table[(v >> 18) & 0x3F];
        encoded[j++] = b64_table[(v >> 12) & 0x3F];
        encoded[j++] = (i + 1 < srclen) ? b64_table[(v >> 6) & 0x3F] : '=';
        encoded[j++] = (i + 2 < srclen) ? b64_table[v & 0x3F] : '=';
    }
    encoded[j] = '\0';
    ESP_LOGI(TAG, "Auth header base64: %s", encoded);
    snprintf(out, out_sz, "Basic %s", encoded);
}

int astronomy_fetch_moon_phase(double lat, double lon, const struct tm* date, char* phase_name, size_t phase_name_sz, float* illumination, char* icon_url, size_t icon_url_sz) {
    ESP_LOGI(TAG, "astronomy_fetch_moon_phase: START lat=%.6f lon=%.6f", lat, lon);
    if (!date || !phase_name || !illumination || !icon_url) {
        ESP_LOGE(TAG, "astronomy_fetch_moon_phase: NULL argument(s)");
        if (phase_name) phase_name[0] = '\0';
        if (illumination) *illumination = -1.0f;
        if (icon_url) icon_url[0] = '\0';
        return ESP_ERR_INVALID_ARG;
    }
    if (phase_name_sz < 2 || icon_url_sz < 2) {
        ESP_LOGE(TAG, "astronomy_fetch_moon_phase: output buffer too small");
        phase_name[0] = '\0';
        icon_url[0] = '\0';
        *illumination = -1.0f;
        return ESP_ERR_INVALID_SIZE;
    }
    if (!ASTRONOMY_API_KEY[0] || !ASTRONOMY_APP_ID[0]) {
        ESP_LOGE(TAG, "No AstronomyAPI key or app id");
        phase_name[0] = '\0';
        icon_url[0] = '\0';
        *illumination = -1.0f;
        return ESP_ERR_INVALID_STATE;
    }
    char date_str[16];
    snprintf(date_str, sizeof(date_str), "%04d-%02d-%02d", date->tm_year + 1900, date->tm_mon + 1, date->tm_mday);
    ESP_LOGI(TAG, "Requesting moon phase for date %s", date_str);

    cJSON *root = cJSON_CreateObject();
    if (!root) {
        ESP_LOGE(TAG, "Failed to create cJSON root object");
        return -1;
    }
    cJSON_AddStringToObject(root, "format", "png");
    cJSON *style = cJSON_CreateObject();
    if (!style) {
        ESP_LOGE(TAG, "Failed to create cJSON style object");
        cJSON_Delete(root);
        return -1;
    }
    cJSON_AddStringToObject(style, "moonStyle", "sketch");
    cJSON_AddStringToObject(style, "backgroundStyle", "stars");
    cJSON_AddStringToObject(style, "backgroundColor", "black");
    cJSON_AddStringToObject(style, "headingColor", "white");
    cJSON_AddStringToObject(style, "textColor", "white");
    cJSON_AddItemToObject(root, "style", style);
    cJSON *observer = cJSON_CreateObject();
    if (!observer) {
        ESP_LOGE(TAG, "Failed to create cJSON observer object");
        cJSON_Delete(root);
        return -1;
    }
    cJSON_AddNumberToObject(observer, "latitude", lat);
    cJSON_AddNumberToObject(observer, "longitude", lon);
    cJSON_AddStringToObject(observer, "date", date_str);
    cJSON_AddItemToObject(root, "observer", observer);
    cJSON *view = cJSON_CreateObject();
    if (!view) {
        ESP_LOGE(TAG, "Failed to create cJSON view object");
        cJSON_Delete(root);
        return -1;
    }
    cJSON_AddStringToObject(view, "type", "portrait-simple");
    cJSON_AddStringToObject(view, "orientation", "south-up");
    cJSON_AddItemToObject(root, "view", view);
    char *body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!body) {
        ESP_LOGE(TAG, "Failed to print cJSON body");
        return -1;
    }
    ESP_LOGI(TAG, "Request JSON body: %s", body);

    char auth_hdr[256];
    astronomy_build_auth_header(auth_hdr, sizeof(auth_hdr));
    ESP_LOGI(TAG, "Auth header: %s", auth_hdr);

    esp_http_client_config_t cfg = {
        .url               = "https://api.astronomyapi.com/api/v2/studio/moon-phase",
        .transport_type    = HTTP_TRANSPORT_OVER_SSL,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    ESP_LOGI(TAG, "HTTP client initialized: %p", client);
    if (!client) {
        ESP_LOGE(TAG, "http_client_init failed");
        free(body);
        return -1;
    }
    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_header(client, "Authorization", auth_hdr);
    esp_http_client_set_post_field(client, body, strlen(body));

    ESP_LOGI(TAG, "Opening HTTP connection...");
    esp_err_t err = esp_http_client_open(client, 0);
    free(body);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "http_client_open: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        return -1;
    }
    int clen = esp_http_client_fetch_headers(client);
    ESP_LOGI(TAG, "Content length from headers: %d", clen);

    size_t cap   = (clen > 0 && clen < 32*1024) ? clen + 1 : 4096;
    size_t total = 0;
    char *buf    = malloc(cap);
    ESP_LOGI(TAG, "Allocated %zu bytes for response buffer at %p", cap, buf);
    if (!buf) {
        ESP_LOGE(TAG, "malloc(%zu) failed", cap);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        phase_name[0] = '\0';
        icon_url[0] = '\0';
        *illumination = -1.0f;
        return ESP_ERR_NO_MEM;
    }
    while (true) {
        if (total >= cap - 1) {
            // Prevent integer overflow
            if (cap > SIZE_MAX / 2) {
                ESP_LOGE(TAG, "Buffer size too large, aborting");
                break;
            }
            size_t newcap = cap * 2;
            char *tmp = realloc(buf, newcap);
            ESP_LOGI(TAG, "Reallocating buffer to %zu bytes at %p", newcap, tmp);
            if (!tmp) {
                ESP_LOGE(TAG, "realloc failed, using partial buffer");
                break;
            }
            buf = tmp;
            cap = newcap;
        }
        int r = esp_http_client_read(client, buf + total, cap - total - 1);
        ESP_LOGI(TAG, "Read %d bytes at offset %zu", r, total);
        if (r < 0) {
            ESP_LOGE(TAG, "esp_http_client_read error: %d", r);
            break;
        }
        if (r == 0) break;
        total += r;
    }
    buf[total] = '\0';
    ESP_LOGI(TAG, "Final response size: %zu bytes", total);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    ESP_LOGI(TAG, "Response JSON: %s", buf);

    cJSON *resp = cJSON_Parse(buf);
    free(buf);
    if (!resp) {
        ESP_LOGE(TAG, "JSON parse error");
        phase_name[0] = '\0';
        icon_url[0] = '\0';
        *illumination = -1.0f;
        return ESP_ERR_INVALID_RESPONSE;
    }
    cJSON *data = cJSON_GetObjectItem(resp, "data");
    if (!data) {
        ESP_LOGE(TAG, "No 'data' in response");
        cJSON_Delete(resp);
        phase_name[0] = '\0';
        icon_url[0] = '\0';
        *illumination = -1.0f;
        return ESP_ERR_INVALID_RESPONSE;
    }
    cJSON *phase = cJSON_GetObjectItem(data, "phase");
    if (cJSON_IsString(phase) && phase->valuestring) {
        ESP_LOGI(TAG, "Parsed phase name: %s", phase->valuestring);
        strncpy(phase_name, phase->valuestring, phase_name_sz - 1);
        phase_name[phase_name_sz - 1] = '\0';
    } else {
        ESP_LOGW(TAG, "No valid phase name in response");
        phase_name[0] = '\0';
    }
    cJSON *illum = cJSON_GetObjectItem(data, "illumination");
    if (cJSON_IsNumber(illum)) {
        ESP_LOGI(TAG, "Parsed illumination: %f", illum->valuedouble);
        *illumination = (float)illum->valuedouble;
    } else {
        ESP_LOGW(TAG, "No valid illumination in response");
        *illumination = -1.0f;
    }
    cJSON *img = cJSON_GetObjectItem(data, "imageUrl");
    if (cJSON_IsString(img) && img->valuestring) {
        ESP_LOGI(TAG, "Parsed imageUrl: %s", img->valuestring);
        strncpy(icon_url, img->valuestring, icon_url_sz - 1);
        icon_url[icon_url_sz - 1] = '\0';
    } else {
        ESP_LOGW(TAG, "No valid imageUrl in response");
        icon_url[0] = '\0';
    }
    cJSON_Delete(resp);
    ESP_LOGI(TAG, "astronomy_fetch_moon_phase: END phase='%s' illum=%.2f icon_url='%s'", phase_name, *illumination, icon_url);
    return 0;
}

esp_err_t astronomy_fetch_icon(const char* image_url, uint8_t **out_buf, size_t *out_len) {
    ESP_LOGI(TAG, "astronomy_fetch_icon: START url=%s", image_url);
    if (!image_url || !image_url[0] || !out_buf || !out_len) {
        ESP_LOGE(TAG, "Invalid arguments to astronomy_fetch_icon");
        if (out_buf) *out_buf = NULL;
        if (out_len) *out_len = 0;
        return ESP_ERR_INVALID_ARG;
    }
    *out_buf = NULL;
    *out_len = 0;
    esp_http_client_config_t cfg = {
        .url               = image_url,
        .transport_type    = HTTP_TRANSPORT_OVER_SSL,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    ESP_LOGI(TAG, "HTTP client initialized: %p", client);
    if (!client) return ESP_FAIL;

    esp_err_t err = esp_http_client_open(client, 0);
    ESP_LOGI(TAG, "HTTP client open result: %d", err);
    if (err != ESP_OK) {
        esp_http_client_cleanup(client);
        return err;
    }
    esp_http_client_fetch_headers(client);

    size_t cap   = 4096;
    size_t total = 0;
    uint8_t *buf = malloc(cap);
    ESP_LOGI(TAG, "Allocated %zu bytes for icon buffer at %p", cap, buf);
    if (!buf) {
        ESP_LOGE(TAG, "malloc(%zu) failed", cap);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        *out_buf = NULL;
        *out_len = 0;
        return ESP_ERR_NO_MEM;
    }
    int r;
    while (1) {
        if (total >= cap) {
            if (cap > SIZE_MAX / 2) {
                ESP_LOGE(TAG, "Buffer size too large, aborting");
                break;
            }
            size_t newcap = cap * 2;
            uint8_t *tmp = realloc(buf, newcap);
            ESP_LOGI(TAG, "Reallocating icon buffer to %zu bytes at %p", newcap, tmp);
            if (!tmp) {
                ESP_LOGE(TAG, "realloc failed, using partial buffer");
                break;
            }
            buf = tmp;
            cap = newcap;
        }
        r = esp_http_client_read(client, (char*)buf + total, cap - total);
        ESP_LOGI(TAG, "Read %d bytes at offset %zu", r, total);
        if (r < 0) {
            ESP_LOGE(TAG, "esp_http_client_read error: %d", r);
            break;
        }
        if (r == 0) break;
        total += r;
    }
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    *out_buf = buf;
    *out_len = total;
    ESP_LOGI(TAG, "astronomy_fetch_icon: END downloaded %u bytes", (unsigned)total);
    return ESP_OK;
}
