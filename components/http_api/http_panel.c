/**
 * @file http_panel.c
 * v2: no global values (profiles set everything); POST /api/theme/save keeps a named theme.
 * @version 2
 * @brief The phone panel's API (see http_panel.h).
 */
#include "http_panel.h"
#include "http_auth.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "cJSON.h"
#include "esp_log.h"
#include "bg_schedule_model.h"
#include "theme_store.h"
#include "settings_lists.h"
#include "sched_store.h"
#include "display_fsm.h"

static const char *TAG = "http_panel";

#define LIST_MAX     32
#define SUN_DAYS     366
#define THEME_BODY_MAX 2048

/**
 * @brief "YYYY-MM-DD" of @p d into @p out.
 * @version 1
 * @utility
 */
static void fmt_date(sched_date_t d, char *out, size_t n)
{
    snprintf(out, n, "%04u-%02u-%02u", (unsigned)d.year, d.month, d.day);
}

/**
 * @brief "lists": backgrounds, themes, animations as [value, label] pairs.
 * @version 1
 * @utility
 */
static void add_lists(cJSON *root)
{
    static const char *const KEYS[] = { "backgrounds", "themes", "anims" };
    static settings_choice_t items[LIST_MAX];   // ~6 KB: off the httpd stack
    cJSON *lists = cJSON_AddObjectToObject(root, "lists");
    for (int w = 0; w < 3; w++) {
        int n = settings_lists_fill((settings_list_t)w, items, LIST_MAX);
        cJSON *arr = cJSON_AddArrayToObject(lists, KEYS[w]);
        for (int i = 0; i < n; i++) {
            cJSON *pair = cJSON_CreateArray();
            cJSON_AddItemToArray(pair, cJSON_CreateString(items[i].value));
            cJSON_AddItemToArray(pair, cJSON_CreateString(items[i].label));
            cJSON_AddItemToArray(arr, pair);
        }
    }
}

/**
 * @brief "schedule": the schedule file's object.
 * @version 1
 * @utility
 */
static void add_schedule(cJSON *root)
{
    static sched_rule_t r[SCHED_MAX_PROFILES];
    int n = sched_store_get(r, SCHED_MAX_PROFILES);
    char *json = sched_to_json(r, n);
    cJSON *obj = json ? cJSON_Parse(json) : NULL;
    free(json);
    cJSON_AddItemToObject(root, "schedule", obj ? obj : cJSON_CreateObject());
}

/**
 * @brief "now" (the clock's local date and minute) and "sun" (a year of [rise, set] from
 *        today, local minutes).
 * @version 1
 * @utility
 */
static void add_now_and_sun(cJSON *root)
{
    static sched_sun_t sun[SUN_DAYS];
    char date[16];
    time_t t = time(NULL);
    struct tm lt;
    localtime_r(&t, &lt);
    sched_date_t today = sched_store_today();
    fmt_date(today, date, sizeof(date));
    cJSON *now = cJSON_AddObjectToObject(root, "now");
    cJSON_AddStringToObject(now, "date", date);
    cJSON_AddNumberToObject(now, "min", lt.tm_hour * 60 + lt.tm_min);
    sched_store_sun_table(today, SUN_DAYS, sun);
    cJSON *days = cJSON_CreateArray();
    for (int i = 0; i < SUN_DAYS; i++) {
        cJSON *pair = cJSON_CreateArray();
        cJSON_AddItemToArray(pair, cJSON_CreateNumber(sun[i].rise_min));
        cJSON_AddItemToArray(pair, cJSON_CreateNumber(sun[i].set_min));
        cJSON_AddItemToArray(days, pair);
    }
    cJSON_AddItemToObject(root, "sun", days);
}

/**
 * @brief GET /api/panel.
 * v2: no "values" (the global settings are gone from the panel).
 * @version 2
 * @callback
 * @return ESP_OK.
 */
static esp_err_t panel_get_handler(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "version", "1.0.0");
    add_lists(root);
    add_schedule(root);
    add_now_and_sun(root);
    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json ? json : "{}", HTTPD_RESP_USE_STRLEN);
    free(json);
    return ESP_OK;
}

/**
 * @brief The request's whole body (NUL-terminated; free it), or NULL when larger than @p max
 *        or cut short.
 * @version 1
 * @utility
 * @return The body.
 */
static char *read_body(httpd_req_t *req, size_t max)
{
    char *body = req->content_len < max ? calloc(1, req->content_len + 1) : NULL;
    size_t got = 0;
    while (body && got < req->content_len) {
        int r = httpd_req_recv(req, body + got, req->content_len - got);
        if (r <= 0) break;
        got += (size_t)r;
    }
    if (body && got != req->content_len) {
        free(body);
        body = NULL;
    }
    return body;
}

/**
 * @brief POST /api/theme/save?stem=<stem>: keep the theme (the body, ui_theme JSON) as the
 *        theme file <stem>; a profile points at it through the schedule. Re-applies the theme.
 * @version 1
 * @callback
 * @req REQ-UI-004
 * @return ESP_OK (the error, if any, is in the response).
 */
static esp_err_t theme_save_post_handler(httpd_req_t *req)
{
    char query[64] = "", stem[BG_THEME_LEN] = "";
    httpd_req_get_url_query_str(req, query, sizeof(query));
    httpd_query_key_value(query, "stem", stem, sizeof(stem));
    char *body = bg_theme_name_valid(stem) ? read_body(req, THEME_BODY_MAX) : NULL;
    ui_theme_t t;
    bool ok = body && ui_theme_parse(body, &t) && theme_store_save(stem, &t);
    free(body);
    if (ok) display_fsm_refresh_theme();   // the FSM task rescans the themes and re-applies
    ESP_LOGI(TAG, "theme '%s' from the panel: %s", stem, ok ? "saved" : "refused");
    httpd_resp_set_type(req, "application/json");
    if (!ok) httpd_resp_set_status(req, "400 Bad Request");
    httpd_resp_send(req, ok ? "{\"status\":\"ok\"}" : "{\"error\":\"not a valid theme or name\"}",
                    HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

/**
 * @brief Register the panel endpoints.
 * v2: POST /api/theme/save (was /api/theme/custom).
 * @version 2
 * @req REQ-UI-005
 */
void http_panel_register(httpd_handle_t srv)
{
    static const httpd_uri_t get = {
        .uri = "/api/panel", .method = HTTP_GET, .handler = panel_get_handler};
    static const httpd_uri_t theme = {
        .uri = "/api/theme/save", .method = HTTP_POST, .handler = theme_save_post_handler};
    http_auth_register(srv, &get);
    http_auth_register(srv, &theme);
}
