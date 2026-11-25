/**
 * @file http_frame_capture.c
 * @version 1
 * @brief POST /api/debug/capture?frames=N&trigger=transition|now arms the frame recorder;
 *        GET /api/debug/capture returns 202 while recording, else the recording:
 *        an ASCII line "count w h\n", count little-endian int64 render times (us), then
 *        count w*h RGB565 frames. A download frees the recording.
 */
#include "http_auth.h"
#include "http_frame_capture.h"

#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "frame_capture.h"

static const char *TAG = "http_capture";

/**
 * @brief Read an integer query parameter.
 * @version 1
 * @utility
 * @return Its value, or @p def if absent.
 */
static int query_int(const char *query, const char *key, int def)
{
    char v[12];
    bool ok = query && httpd_query_key_value(query, key, v, sizeof(v)) == ESP_OK;
    return ok ? atoi(v) : def;
}

/**
 * @brief POST /api/debug/capture: arm a recording.
 * @version 1
 * @callback
 * @return ESP_OK, or ESP_FAIL after sending an error.
 */
static esp_err_t capture_post_handler(httpd_req_t *req)
{
    char query[64] = "";
    httpd_req_get_url_query_str(req, query, sizeof(query));
    char trig[16] = "transition";
    httpd_query_key_value(query, "trigger", trig, sizeof(trig));
    esp_err_t err = frame_capture_arm(query_int(query, "frames", 16), strcmp(trig, "now") != 0);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "arm failed: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, esp_err_to_name(err));
        return ESP_FAIL;
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"status\":\"armed\"}\n", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

/**
 * @brief Stream a finished recording.
 * @version 1
 * @utility
 * @return The first send error, or ESP_OK.
 */
static esp_err_t send_result(httpd_req_t *req, const frame_capture_result_t *r)
{
    char hdr[48];
    int n = snprintf(hdr, sizeof(hdr), "%u %u %u\n", r->count, r->w, r->h);
    httpd_resp_set_type(req, "application/octet-stream");
    esp_err_t err = httpd_resp_send_chunk(req, hdr, n);
    if (err == ESP_OK) err = httpd_resp_send_chunk(req, (const char *)r->t_us, r->count * sizeof(int64_t));
    size_t size = (size_t)r->count * r->w * r->h * 2;
    for (size_t off = 0; off < size && err == ESP_OK; off += 8192) {
        size_t chunk = size - off > 8192 ? 8192 : size - off;
        err = httpd_resp_send_chunk(req, (const char *)r->pixels + off, chunk);
    }
    httpd_resp_send_chunk(req, NULL, 0);
    return err;
}

/**
 * @brief GET /api/debug/capture: 202 until the recording is done, then the frames.
 * @version 1
 * @callback
 * @return ESP_OK, or the send error.
 */
static esp_err_t capture_get_handler(httpd_req_t *req)
{
    frame_capture_result_t r;
    lvgl_port_lock(0);
    bool done = frame_capture_result(&r);
    lvgl_port_unlock();
    if (!done) {
        httpd_resp_set_status(req, "202 Accepted");
        httpd_resp_send(req, "recording or idle\n", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }
    esp_err_t err = send_result(req, &r);
    if (err != ESP_OK) ESP_LOGW(TAG, "download failed: %s", esp_err_to_name(err));
    frame_capture_release();
    return err;
}

/**
 * @brief Register the frame-capture endpoints (before any wildcard handler).
 * v2: registered through http_auth_register (the gate).
 * @version 2
 * @req REQ-API-003
 */
void http_frame_capture_register(httpd_handle_t srv)
{
    static const httpd_uri_t post = {
        .uri = "/api/debug/capture", .method = HTTP_POST, .handler = capture_post_handler};
    static const httpd_uri_t get = {
        .uri = "/api/debug/capture", .method = HTTP_GET, .handler = capture_get_handler};
    http_auth_register(srv, &post);
    http_auth_register(srv, &get);
}
