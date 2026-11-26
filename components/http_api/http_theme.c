/**
 * @file http_theme.c
 * @version 1
 * @brief GET /api/themes and GET /api/theme.
 *
 * GET /api/themes → [{"stem": "default", "name": "Default"}, ...]   (files in /sdcard/themes)
 * GET /api/theme  → {"name": "Default", "dark": true,
 *                    "roles": {"screen": "#rrggbb", "surface": "#rrggbb", ..., "light": "#rrggbb"},
 *                    "on":    {"accent": "#rrggbb", ...}}   // legible text ON each role as a fill
 * The roles are the device's own derived colours, so the web panel draws with exactly what
 * the screen does.
 */
#include "http_auth.h"
#include "http_theme.h"

#include <stdio.h>
#include <stdlib.h>
#include "cJSON.h"
#include "theme_store.h"
#include "ui_theme.h"
#include "http_json.h"
#include "esp_log.h"

static const char *TAG = "http_theme";

/**
 * @brief GET /api/themes: the valid theme files on the SD card.
 * v2: replies through http_json_send.
 * @version 2
 * @callback
 * @return ESP_OK.
 */
static esp_err_t themes_get_handler(httpd_req_t *req)
{
    static theme_entry_t entries[THEME_STORE_MAX];   // static: off the httpd task stack
    int n = theme_store_scan(entries);
    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < n; i++) {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "stem", entries[i].stem);
        cJSON_AddStringToObject(o, "name", entries[i].name);
        cJSON_AddItemToArray(arr, o);
    }
    http_json_send(req, "200 OK", arr);
    return ESP_OK;
}

/**
 * @brief Add @p c to @p obj as "#rrggbb" under @p key.
 * @version 1
 * @utility
 */
static void add_hex(cJSON *obj, const char *key, lv_color_t c)
{
    char hex[8];
    snprintf(hex, sizeof(hex), "#%02x%02x%02x", c.red, c.green, c.blue);
    cJSON_AddStringToObject(obj, key, hex);
}

/**
 * @brief GET /api/theme: the current theme's name, polarity, every role as "#rrggbb", and the
 *        legible text colour on each role used as a fill.
 * v2: replies through http_json_send.
 * @version 2
 * @callback
 * @return ESP_OK.
 */
static esp_err_t theme_get_handler(httpd_req_t *req)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "name", ui_theme_get()->name);
    cJSON_AddBoolToObject(root, "dark", ui_theme_is_dark());
    cJSON *roles = cJSON_AddObjectToObject(root, "roles");
    cJSON *on = cJSON_AddObjectToObject(root, "on");
    for (int i = 0; i < UI_ROLE_COUNT; i++) {
        lv_color_t c = ui_theme_color((ui_role_t)i);
        add_hex(roles, ui_theme_role_name((ui_role_t)i), c);
        add_hex(on, ui_theme_role_name((ui_role_t)i), ui_theme_text_on(UI_ROLE_TEXT, c));
    }
    http_json_send(req, "200 OK", root);
    return ESP_OK;
}

/**
 * @brief Register the theme endpoints.
 * v2: registered through http_auth_register (the gate).
 * @version 2
 * @req REQ-UI-003
 */
void http_theme_register(httpd_handle_t srv)
{
    static const httpd_uri_t list = {
        .uri = "/api/themes", .method = HTTP_GET, .handler = themes_get_handler};
    static const httpd_uri_t current = {
        .uri = "/api/theme", .method = HTTP_GET, .handler = theme_get_handler};
    http_auth_register(srv, &list);
    http_auth_register(srv, &current);
}
