/**
 * @file http_schedule.c
 * @version 1
 * @brief GET/POST /api/schedule (see http_schedule.h).
 *
 * GET  /api/schedule → the schedule file's JSON
 * POST /api/schedule ← a whole schedule; 400 when malformed, 409 {"error": "Overlaps ..."}
 *                      when two profiles of a kind overlap, else stored and applied.
 */
#include "http_auth.h"
#include "http_schedule.h"

#include <stdio.h>
#include <stdlib.h>
#include "sched_store.h"
#include "cJSON.h"
#include "display_events.h"
#include "display_fsm.h"
#include "esp_log.h"

static const char *TAG = "http_sched";

#define SCHED_BODY_MAX 16384

/**
 * @brief GET /api/schedule.
 * @version 1
 * @callback
 * @return ESP_OK.
 */
static esp_err_t schedule_get_handler(httpd_req_t *req)
{
    static sched_rule_t r[SCHED_MAX_PROFILES];   // ~5 KB: off the httpd stack
    int n = sched_store_get(r, SCHED_MAX_PROFILES);
    char *json = sched_to_json(r, n);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json ? json : "{}", HTTPD_RESP_USE_STRLEN);
    free(json);
    return ESP_OK;
}

/**
 * @brief Receive the whole request body into a heap buffer (NUL-terminated).
 * @version 1
 * @utility
 * @return The body (caller frees), or NULL if empty, too large or interrupted.
 */
static char *recv_body(httpd_req_t *req)
{
    size_t len = req->content_len;
    char *buf = (len > 0 && len < SCHED_BODY_MAX) ? malloc(len + 1) : NULL;
    size_t got = 0;
    while (buf && got < len) {
        int r = httpd_req_recv(req, buf + got, len - got);
        if (r <= 0) {
            free(buf);
            buf = NULL;
        } else {
            got += (size_t)r;
        }
    }
    if (buf) buf[len] = '\0';
    return buf;
}

/**
 * @brief Send a JSON error with HTTP status @p status ("409 Conflict").
 * @version 1
 * @utility
 */
static void send_error(httpd_req_t *req, const char *status, const char *why)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "error", why);
    char *json = cJSON_PrintUnformatted(o);
    cJSON_Delete(o);
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json ? json : "{}", HTTPD_RESP_USE_STRLEN);
    free(json);
}

/**
 * @brief POST /api/schedule: parse, check, store, and make the display re-apply.
 * @version 1
 * @callback
 * @return ESP_OK (the error, if any, is in the response).
 */
static esp_err_t schedule_post_handler(httpd_req_t *req)
{
    static sched_rule_t r[SCHED_MAX_PROFILES];
    char why[128] = "Failed to save the schedule";
    char *body = recv_body(req);
    int n = body ? sched_from_json(body, r, SCHED_MAX_PROFILES) : -1;
    free(body);
    esp_err_t err = n < 0 ? ESP_ERR_INVALID_SIZE : sched_store_set(r, n, why, sizeof(why));
    if (err == ESP_ERR_INVALID_SIZE) {
        ESP_LOGW(TAG, "POST: not a valid schedule");
        send_error(req, "400 Bad Request", "Not a valid schedule");
    } else if (err != ESP_OK) {
        ESP_LOGW(TAG, "POST refused: %s", why);
        send_error(req, err == ESP_ERR_INVALID_ARG ? "409 Conflict" : "500 Internal Server Error", why);
    } else {
        display_event_t evt = {.type = DISPLAY_EVT_REFRESH_BG};
        display_fsm_send_event(&evt);   // re-apply the showing profile now
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"status\":\"ok\"}\n", HTTPD_RESP_USE_STRLEN);
    }
    return ESP_OK;
}

/**
 * @brief Register the schedule endpoints.
 * v2: registered through http_auth_register (the gate).
 * @version 2
 * @req REQ-BG-004
 */
void http_schedule_register(httpd_handle_t srv)
{
    static const httpd_uri_t get = {
        .uri = "/api/schedule", .method = HTTP_GET, .handler = schedule_get_handler};
    static const httpd_uri_t post = {
        .uri = "/api/schedule", .method = HTTP_POST, .handler = schedule_post_handler};
    http_auth_register(srv, &get);
    http_auth_register(srv, &post);
}
