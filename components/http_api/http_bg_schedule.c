/**
 * @file http_bg_schedule.c
 * v2: a profile names a theme (was a text colour).
 * @version 2
 * @brief GET/POST /api/bg_schedule: the background schedule as JSON.
 *
 * Format (GET returns it, POST replaces the whole schedule):
 * {
 *   "slots":    ["07:00", "12:00", "17:00", "22:00"],            // shared start times
 *   "profiles": [{"name": "Winter morning",
 *                 "image": "A:/backgrounds/winter_morning.png",   // "" or absent = inherit
 *                 "theme": "greenwood-night",                     // null/absent = inherit
 *                 "brightness": 40,                               // null/absent = inherit
 *                 "anim": 2}],                                    // clock_anim_t; null = inherit
 *   "days":     [[0,1,2,3], ... 7 rows, Sunday first]            // profile index per slot
 * }
 */
#include "http_bg_schedule.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bg_schedule.h"
#include "cJSON.h"
#include "display_events.h"
#include "display_fsm.h"
#include "esp_log.h"

static const char *TAG = "http_bg_sched";

#define BG_SCHED_BODY_MAX 4096

/**
 * @brief Parse "HH:MM" into minutes after midnight.
 * @version 1
 * @utility
 * @return Minutes, or -1 if malformed.
 */
static int parse_hhmm(const char *s)
{
    int h = -1, m = -1;
    bool ok = s && sscanf(s, "%d:%d", &h, &m) == 2 && h >= 0 && h < 24 && m >= 0 && m < 60;
    return ok ? h * 60 + m : -1;
}

/**
 * @brief Fill the slot starts from the "slots" array.
 * @version 1
 * @utility
 * @return true if every entry parsed and the count is in range.
 */
static bool parse_slots(const cJSON *arr, bg_schedule_t *s)
{
    int n = cJSON_GetArraySize(arr);
    if (!cJSON_IsArray(arr) || n < 1 || n > BG_SCHED_SLOTS) return false;
    s->slot_count = (uint8_t)n;
    bool ok = true;
    for (int i = 0; i < n; i++) {
        int m = parse_hhmm(cJSON_GetStringValue(cJSON_GetArrayItem(arr, i)));
        ok = ok && m >= 0;
        s->slot_start[i] = (uint16_t)(m < 0 ? 0 : m);
    }
    return ok;
}

/**
 * @brief Read an optional small integer override (null/absent = inherit).
 * @version 1
 * @utility
 * @return The value, or BG_INHERIT.
 */
static int8_t opt_int(const cJSON *obj, const char *key)
{
    const cJSON *v = cJSON_GetObjectItem(obj, key);
    return cJSON_IsNumber(v) ? (int8_t)v->valueint : (int8_t)BG_INHERIT;
}

/**
 * @brief Fill one profile from its JSON object (an invalid theme name is kept, so the
 *        schedule fails validation and the POST is rejected).
 * v2: "theme" (was "text_color").
 * @version 2
 * @utility
 */
static void parse_profile(const cJSON *obj, bg_profile_t *p)
{
    const char *name  = cJSON_GetStringValue(cJSON_GetObjectItem(obj, "name"));
    const char *image = cJSON_GetStringValue(cJSON_GetObjectItem(obj, "image"));
    const char *theme = cJSON_GetStringValue(cJSON_GetObjectItem(obj, "theme"));
    snprintf(p->name, sizeof(p->name), "%s", name ? name : "");
    snprintf(p->image, sizeof(p->image), "%s", image ? image : "");
    snprintf(p->theme, sizeof(p->theme), "%s", theme ? theme : "");
    if (theme && !bg_theme_name_valid(theme)) p->theme[0] = '?';   // invalid: refuse the POST
    p->brightness = opt_int(obj, "brightness");
    p->anim = opt_int(obj, "anim");
}

/**
 * @brief Fill the profiles from the "profiles" array.
 * @version 1
 * @utility
 * @return true if the count is in range.
 */
static bool parse_profiles(const cJSON *arr, bg_schedule_t *s)
{
    int n = cJSON_GetArraySize(arr);
    if (!cJSON_IsArray(arr) || n < 1 || n > BG_SCHED_PROFILES) return false;
    s->profile_count = (uint8_t)n;
    for (int i = 0; i < n; i++) parse_profile(cJSON_GetArrayItem(arr, i), &s->profiles[i]);
    return true;
}

/**
 * @brief Fill the weekday grid from the "days" array (7 rows, Sunday first).
 * @version 1
 * @utility
 * @return true if there are 7 rows of slot_count numbers.
 */
static bool parse_days(const cJSON *arr, bg_schedule_t *s)
{
    bool ok = cJSON_IsArray(arr) && cJSON_GetArraySize(arr) == BG_SCHED_DAYS;
    for (int d = 0; ok && d < BG_SCHED_DAYS; d++) {
        const cJSON *row = cJSON_GetArrayItem(arr, d);
        ok = cJSON_IsArray(row) && cJSON_GetArraySize(row) == s->slot_count;
        for (int i = 0; ok && i < s->slot_count; i++) {
            const cJSON *v = cJSON_GetArrayItem(row, i);
            ok = cJSON_IsNumber(v) && v->valueint >= 0;
            s->grid[d][i] = ok ? (uint8_t)v->valueint : 0;
        }
    }
    return ok;
}

/**
 * @brief Build a schedule from the request JSON.
 * @version 1
 * @utility
 * @return true if it parsed and validated.
 */
static bool schedule_from_json(const cJSON *root, bg_schedule_t *s)
{
    bg_schedule_defaults(s);
    bool ok = parse_slots(cJSON_GetObjectItem(root, "slots"), s);
    ok = ok && parse_profiles(cJSON_GetObjectItem(root, "profiles"), s);
    ok = ok && parse_days(cJSON_GetObjectItem(root, "days"), s);
    return ok && bg_schedule_valid(s);
}

/**
 * @brief Add one profile object to @p arr.
 * v2: "theme" (was "text_color").
 * @version 2
 * @utility
 */
static void profile_to_json(cJSON *arr, const bg_profile_t *p)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "name", p->name);
    cJSON_AddStringToObject(o, "image", p->image);
    if (p->theme[0]) cJSON_AddStringToObject(o, "theme", p->theme);
    else cJSON_AddNullToObject(o, "theme");
    if (p->brightness == BG_INHERIT) cJSON_AddNullToObject(o, "brightness");
    else cJSON_AddNumberToObject(o, "brightness", p->brightness);
    if (p->anim == BG_INHERIT) cJSON_AddNullToObject(o, "anim");
    else cJSON_AddNumberToObject(o, "anim", p->anim);
    cJSON_AddItemToArray(arr, o);
}

/**
 * @brief Render the schedule as JSON (same format POST accepts).
 * @version 1
 * @utility
 * @return New cJSON tree (caller deletes).
 */
static cJSON *schedule_to_json(const bg_schedule_t *s)
{
    cJSON *root = cJSON_CreateObject();
    cJSON *slots = cJSON_AddArrayToObject(root, "slots");
    for (int i = 0; i < s->slot_count; i++) {
        char hhmm[8];
        snprintf(hhmm, sizeof(hhmm), "%02u:%02u", s->slot_start[i] / 60, s->slot_start[i] % 60);
        cJSON_AddItemToArray(slots, cJSON_CreateString(hhmm));
    }
    cJSON *profiles = cJSON_AddArrayToObject(root, "profiles");
    for (int i = 0; i < s->profile_count; i++) profile_to_json(profiles, &s->profiles[i]);
    cJSON *days = cJSON_AddArrayToObject(root, "days");
    for (int d = 0; d < BG_SCHED_DAYS; d++) {
        cJSON *row = cJSON_CreateArray();
        for (int i = 0; i < s->slot_count; i++) cJSON_AddItemToArray(row, cJSON_CreateNumber(s->grid[d][i]));
        cJSON_AddItemToArray(days, row);
    }
    return root;
}

/**
 * @brief GET /api/bg_schedule: the current schedule as JSON.
 * @version 1
 * @callback
 * @return ESP_OK.
 */
static esp_err_t bg_schedule_get_handler(httpd_req_t *req)
{
    bg_schedule_t s;
    bg_schedule_get(&s);
    cJSON *root = schedule_to_json(&s);
    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
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
    char *buf = (len > 0 && len < BG_SCHED_BODY_MAX) ? malloc(len + 1) : NULL;
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
 * @brief POST /api/bg_schedule: validate and store a new schedule, then make the display
 *        re-apply the active profile.
 * @version 1
 * @callback
 * @return ESP_OK, or ESP_FAIL after sending a 4xx/5xx.
 */
static esp_err_t bg_schedule_post_handler(httpd_req_t *req)
{
    char *body = recv_body(req);
    cJSON *root = body ? cJSON_Parse(body) : NULL;
    free(body);
    bg_schedule_t s;
    bool ok = root && schedule_from_json(root, &s);
    cJSON_Delete(root);
    esp_err_t err = ok ? bg_schedule_set(&s) : ESP_ERR_INVALID_ARG;
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "POST rejected: %s", esp_err_to_name(err));
        httpd_resp_send_err(req, ok ? HTTPD_500_INTERNAL_SERVER_ERROR : HTTPD_400_BAD_REQUEST,
                            ok ? "Failed to save schedule" : "Invalid schedule");
        return ESP_FAIL;
    }
    display_event_t evt = {.type = DISPLAY_EVT_REFRESH_BG};
    display_fsm_send_event(&evt);   // re-apply the active profile now
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"status\":\"ok\"}\n", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

/**
 * @brief Register the background schedule endpoints (before any wildcard handler).
 * @version 1
 * @req REQ-BG-004
 */
void http_bg_schedule_register(httpd_handle_t srv)
{
    static const httpd_uri_t get = {
        .uri = "/api/bg_schedule", .method = HTTP_GET, .handler = bg_schedule_get_handler};
    static const httpd_uri_t post = {
        .uri = "/api/bg_schedule", .method = HTTP_POST, .handler = bg_schedule_post_handler};
    httpd_register_uri_handler(srv, &get);
    httpd_register_uri_handler(srv, &post);
}
