/**
 * @file nws_radar.c
 * @version 2
 * @brief Fetch radar imagery (by image id) and its image list from the NOAA ImageServer, and the
 *        Kp index from SWPC.
 */

#include "nws_internal.h"
#include "esp_heap_caps.h"
#include "nws_radar_frames.h"

static const char *TAG = "nws_radar";

// NOAA ArcGIS ImageServer (time-enabled) — base reflectivity. Each image is fetched by its id
// (lock-raster mosaic rule): without one the server returns a default view that lags behind,
// and a time request for the newest returns the previous image.
#define NWS_RADAR_BASE \
    "https://mapservices.weather.noaa.gov/eventdriven/rest/services/" \
    "radar/radar_base_reflectivity_time/ImageServer"
#define NWS_RADAR_URL_FMT \
    NWS_RADAR_BASE "/exportImage?bbox=%.4f,%.4f,%.4f,%.4f" \
    "&bboxSR=4326&size=1024,600&format=png32&transparent=true&f=image" \
    "&mosaicRule=%%7B%%22mosaicMethod%%22%%3A%%22esriMosaicLockRaster%%22" \
    "%%2C%%22lockRasterIds%%22%%3A%%5B%lld%%5D%%7D"
// The images covering the bbox, with their validity (epoch ms).
#define NWS_RADAR_QUERY_FMT \
    NWS_RADAR_BASE "/query?where=1%%3D1&geometry=%.4f,%.4f,%.4f,%.4f" \
    "&geometryType=esriGeometryEnvelope&inSR=4326&spatialRel=esriSpatialRelIntersects" \
    "&outFields=objectid,idp_validtime,idp_validendtime&returnGeometry=false&f=json"
#define RADAR_BBOX_HALF_DEG 2.0f   /* 4-degree box centred on the location */

#define NWS_KP_URL \
    "https://services.swpc.noaa.gov/products/noaa-planetary-k-index.json"

// Validate a fetched buffer as a PNG and copy it into a fresh SPIRAM buffer.
// Returns ESP_OK (out set), ESP_ERR_NO_MEM, or ESP_FAIL (not a PNG — give up).
/**
 * @brief Check the PNG signature of a fetched radar buffer and copy it into SPIRAM.
 * @version 1
 * @req REQ-RADAR-001
 * @return ESP_OK with @p out / @p out_len set, ESP_ERR_NO_MEM if the SPIRAM copy
 *         failed, or ESP_FAIL if the buffer is not a PNG.
 */
static esp_err_t accept_radar_png(const char *buf, size_t len,
                                  uint8_t **out, size_t *out_len)
{
    static const uint8_t png_sig[4] = {0x89, 0x50, 0x4E, 0x47};
    if (len < 8 || memcmp(buf, png_sig, 4) != 0) {
        ESP_LOGE(TAG, "radar response is not PNG (first bytes: %02x %02x %02x %02x, len=%zu)",
                 (uint8_t)buf[0], (uint8_t)buf[1], (uint8_t)buf[2], (uint8_t)buf[3], len);
        return ESP_FAIL;
    }
    uint8_t *spiram_buf = heap_caps_malloc(len, MALLOC_CAP_SPIRAM);
    if (!spiram_buf) {
        ESP_LOGE(TAG, "SPIRAM alloc failed for radar PNG (%zu bytes)", len);
        return ESP_ERR_NO_MEM;
    }
    memcpy(spiram_buf, buf, len);
    *out = spiram_buf;
    *out_len = len;
    ESP_LOGI(TAG, "radar: %zu bytes fetched", len);
    return ESP_OK;
}

/**
 * @brief GET @p url (a radar request) with up to NWS_MAX_RETRIES attempts, 15 s each. The URL
 *        is never logged: it carries the device's location.
 * @version 1
 * @dg_internal
 * @return malloc'd body (caller frees) with @p len set, or NULL.
 */
static char *radar_get(const char *url, size_t *len)
{
    esp_http_client_config_t cfg = {
        .url                   = url,
        .transport_type        = HTTP_TRANSPORT_OVER_SSL,
        .crt_bundle_attach     = esp_crt_bundle_attach,
        .timeout_ms            = 15000,  // radar images can be slow
        .user_agent            = NWS_USER_AGENT,
        .max_redirection_count = 3,
    };
    for (int attempt = 0; attempt < NWS_MAX_RETRIES; attempt++) {
        if (attempt > 0) {
            ESP_LOGW(TAG, "radar retry %d/%d", attempt + 1, NWS_MAX_RETRIES);
            vTaskDelay(pdMS_TO_TICKS(NWS_RETRY_DELAY_MS * attempt));
        }
        http_attempt_result_t result;
        char *buf = http_request_execute(&cfg, &result, len);
        if (result == HTTP_ATTEMPT_OK && buf && *len > 0) return buf;
        free(buf);
        if (result == HTTP_ATTEMPT_ABORT || result == HTTP_ATTEMPT_RATE_LIMIT) break;
    }
    ESP_LOGE(TAG, "radar request failed after %d attempts", NWS_MAX_RETRIES);
    return NULL;
}

/**
 * @brief Fetch radar image @p id (from nws_radar_rasters) as a 1024x600 transparent
 *        base-reflectivity PNG for a 4-degree box centred on @p lat / @p lon.
 * v2: by image id (the no-time default view lagged behind the newest image); retries shared
 *     with the image list query (radar_get).
 * @version 2
 * @req REQ-RADAR-001
 * @req REQ-RADAR-003
 * @return ESP_OK with a SPIRAM buffer in @p png_buf (caller frees), ESP_ERR_NO_MEM,
 *         ESP_FAIL if the response is not a PNG, or ESP_ERR_HTTP_CONNECT.
 */
esp_err_t nws_fetch_radar(float lat, float lon, int64_t id, uint8_t **png_buf,
                          size_t *png_len)
{
    *png_buf = NULL;
    *png_len = 0;
    char url[512];
    snprintf(url, sizeof(url), NWS_RADAR_URL_FMT, lon - RADAR_BBOX_HALF_DEG,
             lat - RADAR_BBOX_HALF_DEG, lon + RADAR_BBOX_HALF_DEG, lat + RADAR_BBOX_HALF_DEG,
             (long long)id);
    size_t len = 0;
    char *buf = radar_get(url, &len);
    if (!buf) return ESP_ERR_HTTP_CONNECT;
    esp_err_t r = accept_radar_png(buf, len, png_buf, png_len);
    free(buf);
    return r;
}

/**
 * @brief Read one query feature's id and validity into @p out (an open image has a null end).
 * @version 1
 * @dg_internal
 * @return Whether the feature had an id and a start time.
 */
static bool parse_raster(const cJSON *feature, nws_radar_raster_t *out)
{
    const cJSON *attr = cJSON_GetObjectItemCaseSensitive(feature, "attributes");
    const cJSON *id = cJSON_GetObjectItemCaseSensitive(attr, "objectid");
    const cJSON *start = cJSON_GetObjectItemCaseSensitive(attr, "idp_validtime");
    const cJSON *end = cJSON_GetObjectItemCaseSensitive(attr, "idp_validendtime");
    if (!cJSON_IsNumber(start) || !cJSON_IsNumber(id)) return false;
    out->id = (int64_t)id->valuedouble;
    out->start_ms = (int64_t)start->valuedouble;
    out->end_ms = cJSON_IsNumber(end) ? (int64_t)end->valuedouble : out->start_ms;
    return true;
}

/**
 * @brief List the radar images the server holds for a 4-degree box centred on @p lat /
 *        @p lon, with their validity, into @p out (at most @p max, server order).
 * @version 1
 * @req REQ-RADAR-003
 * @return Number listed, or -1 if the query failed.
 */
int nws_radar_rasters(float lat, float lon, nws_radar_raster_t *out, int max)
{
    char url[512];
    snprintf(url, sizeof(url), NWS_RADAR_QUERY_FMT, lon - RADAR_BBOX_HALF_DEG,
             lat - RADAR_BBOX_HALF_DEG, lon + RADAR_BBOX_HALF_DEG, lat + RADAR_BBOX_HALF_DEG);
    size_t len = 0;
    char *json = radar_get(url, &len);
    cJSON *root = json ? cJSON_Parse(json) : NULL;
    free(json);
    const cJSON *features = cJSON_GetObjectItemCaseSensitive(root, "features");
    int n = cJSON_IsArray(features) ? 0 : -1;
    const cJSON *f;
    cJSON_ArrayForEach(f, features) {
        if (n >= max) break;
        if (parse_raster(f, &out[n])) n++;
    }
    cJSON_Delete(root);
    if (n < 0) ESP_LOGE(TAG, "radar image list: no features in the response");
    else ESP_LOGI(TAG, "radar image list: %d image(s)", n);
    return n;
}

/**
 * @brief Fetch the SWPC planetary K-index table and take the Kp of the last
 *        (most recent) row.
 *
 * @p kp_out is set to 0.0 first and stays 0.0 on failure.
 * @version 1
 * @req REQ-ASTRO-001
 * @return ESP_OK on success, ESP_ERR_HTTP_CONNECT if the fetch failed, or
 *         ESP_ERR_INVALID_RESPONSE if the array is missing, too short or malformed.
 */
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
