/**
 * @file http_panel.c
 * v2: no global values (profiles set everything); POST /api/theme/save keeps a named theme.
 * @version 2
 * @brief The phone panel's API (see http_panel.h).
 */
#include "http_panel.h"
#include "http_json.h"
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
#include "http_api.h"
#include "location.h"

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
 * @brief "location": where the clock is (its label, coordinates and zone).
 * @version 1
 * @utility
 */
static void add_location(cJSON *root)
{
    location_t loc;
    location_get(&loc);
    cJSON *o = cJSON_AddObjectToObject(root, "location");
    cJSON_AddStringToObject(o, "label", loc.place.label);
    cJSON_AddNumberToObject(o, "lat", loc.place.lat);
    cJSON_AddNumberToObject(o, "lon", loc.place.lon);
    cJSON_AddStringToObject(o, "tz", loc.tz);
}

/**
 * @brief GET /api/panel.
 * v2: no "values" (the global settings are gone from the panel).
 * v3: "location".
 * @version 3
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
    add_location(root);
    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json ? json : "{}", HTTPD_RESP_USE_STRLEN);
    free(json);
    return ESP_OK;
}

/**
 * @brief POST /api/theme/save?stem=<stem>: keep the theme (the body, ui_theme JSON) as the
 *        theme file <stem>; a profile points at it through the schedule. Re-applies the theme.
 * v2: the shared JSON helpers (http_json).
 * @version 2
 * @callback
 * @req REQ-UI-004
 * @return ESP_OK (the error, if any, is in the response).
 */
static esp_err_t theme_save_post_handler(httpd_req_t *req)
{
    char query[64] = "", stem[BG_THEME_LEN] = "";
    httpd_req_get_url_query_str(req, query, sizeof(query));
    httpd_query_key_value(query, "stem", stem, sizeof(stem));
    char *body = bg_theme_name_valid(stem) ? http_json_body(req, THEME_BODY_MAX) : NULL;
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
 * @brief POST /api/location/lookup {"address": "..."}: the place it matches ({label, lat,
 *        lon}; 404 none; 502 the geocoder did not answer; 503 during the launch window, when
 *        outside data must not be processed). The address is not kept or logged.
 * v2: the shared JSON helpers (http_json).
 * @version 2
 * @callback
 * @req REQ-LOC-001
 * @return ESP_OK.
 */
static esp_err_t location_lookup_handler(httpd_req_t *req)
{
    char *body = http_json_body(req, 512);
    cJSON *in = body ? cJSON_Parse(body) : NULL;
    free(body);
    const cJSON *addr = cJSON_GetObjectItemCaseSensitive(in, "address");
    location_place_t p;
    char why[64] = "Send an address";
    esp_err_t err = ESP_ERR_INVALID_ARG;
    if (!http_api_launched()) snprintf(why, sizeof(why), "The clock is still starting");
    else if (cJSON_IsString(addr)) err = location_lookup(addr->valuestring, &p, why, sizeof(why));
    cJSON_Delete(in);
    cJSON *out = err == ESP_OK ? cJSON_CreateObject() : http_json_error(why);
    if (err == ESP_OK) {
        cJSON_AddStringToObject(out, "label", p.label);
        cJSON_AddNumberToObject(out, "lat", p.lat);
        cJSON_AddNumberToObject(out, "lon", p.lon);
    }
    const char *status = err == ESP_OK ? "200 OK" : err == ESP_ERR_NOT_FOUND ? "404 Not Found"
                         : !http_api_launched() ? "503 Service Unavailable"
                         : err == ESP_ERR_INVALID_ARG ? "400 Bad Request" : "502 Bad Gateway";
    http_json_send(req, status, out);
    return ESP_OK;
}

/**
 * @brief Read {label, lat, lon, tz} into @p out.
 * @version 1
 * @utility
 * @return Whether every field is there.
 */
static bool read_location(const cJSON *in, location_t *out)
{
    const cJSON *label = cJSON_GetObjectItemCaseSensitive(in, "label");
    const cJSON *lat = cJSON_GetObjectItemCaseSensitive(in, "lat");
    const cJSON *lon = cJSON_GetObjectItemCaseSensitive(in, "lon");
    const cJSON *tz = cJSON_GetObjectItemCaseSensitive(in, "tz");
    bool ok = cJSON_IsString(label) && cJSON_IsNumber(lat) && cJSON_IsNumber(lon) && cJSON_IsString(tz);
    if (ok) {
        snprintf(out->place.label, sizeof(out->place.label), "%s", label->valuestring);
        out->place.lat = (float)lat->valuedouble;
        out->place.lon = (float)lon->valuedouble;
        snprintf(out->tz, sizeof(out->tz), "%s", tz->valuestring);
    }
    return ok;
}

/**
 * @brief POST /api/location {label, lat, lon, tz}: keep the place and zone; the weather, the
 *        sky and the schedule follow.
 * v2: the shared JSON helpers (http_json).
 * @version 2
 * @callback
 * @req REQ-LOC-001
 * @return ESP_OK.
 */
static esp_err_t location_post_handler(httpd_req_t *req)
{
    char *body = http_json_body(req, 512);
    cJSON *in = body ? cJSON_Parse(body) : NULL;
    free(body);
    location_t loc;
    memset(&loc, 0, sizeof(loc));
    bool ok = read_location(in, &loc) && location_save(&loc) == ESP_OK;
    cJSON_Delete(in);
    if (ok) display_fsm_location_changed();
    cJSON *out = ok ? cJSON_CreateObject() : http_json_error("Not a place or zone the clock knows");
    if (ok) cJSON_AddStringToObject(out, "status", "ok");
    http_json_send(req, ok ? "200 OK" : "400 Bad Request", out);
    return ESP_OK;
}

/**
 * @brief Register the panel endpoints.
 * v2: POST /api/theme/save (was /api/theme/custom).
 * v3: POST /api/location/lookup and /api/location.
 * @version 3
 * @req REQ-UI-005
 */
void http_panel_register(httpd_handle_t srv)
{
    static const httpd_uri_t lookup = {
        .uri = "/api/location/lookup", .method = HTTP_POST, .handler = location_lookup_handler};
    static const httpd_uri_t place = {
        .uri = "/api/location", .method = HTTP_POST, .handler = location_post_handler};
    http_auth_register(srv, &lookup);
    http_auth_register(srv, &place);
    static const httpd_uri_t get = {
        .uri = "/api/panel", .method = HTTP_GET, .handler = panel_get_handler};
    static const httpd_uri_t theme = {
        .uri = "/api/theme/save", .method = HTTP_POST, .handler = theme_save_post_handler};
    http_auth_register(srv, &get);
    http_auth_register(srv, &theme);
}
