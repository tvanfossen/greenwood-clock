/**
 * @file http_library.c
 * @version 1
 * @brief The Library for the phone panel (REQ-LIB-001), behind the session gate:
 *          GET  /api/library?kind=backgrounds|album|themes|anims
 *               -> {"items":[{name,label,size,mtime,thumb}], "total_mb", "free_mb"}
 *          GET  /api/library/thumb?kind=&name=   -> the thumbnail PNG
 *          GET  /api/library/theme?stem=         -> the theme file (ui_theme JSON)
 *          POST /api/library/delete {kind, name} -> ok, or 409 {"error": "Used by ..."}
 *          POST /api/library/rename {kind, from, to}
 *        Refused (503) during the launch window, like every handler touching the card's
 *        content.
 */
#include "http_library.h"
#include "http_auth.h"
#include "http_json.h"
#include "http_api.h"
#include "library_store.h"
#include "theme_store.h"
#include "ui_theme.h"

#include "esp_heap_caps.h"
#include "esp_log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "http_library";

#define THUMB_MAX (64 * 1024)

/**
 * @brief The kind named @p s ("backgrounds", "album", "themes", "anims").
 * @version 1
 * @utility
 * @return It, or LIB_KINDS when none.
 */
static lib_kind_t kind_of(const char *s)
{
    static const char *const NAMES[LIB_KINDS] = { "backgrounds", "album", "themes", "anims" };
    int k = 0;
    while (k < LIB_KINDS && (!s || strcmp(s, NAMES[k]) != 0)) k++;
    return (lib_kind_t)k;
}

/**
 * @brief Query key @p key of @p req into @p out.
 * @version 1
 * @utility
 */
static void query(httpd_req_t *req, const char *key, char *out, size_t n)
{
    char q[160] = "";
    out[0] = '\0';
    httpd_req_get_url_query_str(req, q, sizeof(q));
    httpd_query_key_value(q, key, out, n);
}

/**
 * @brief Whether the clock has started (else 503 sent).
 * @version 1
 * @utility
 * @return Whether.
 */
static bool launched(httpd_req_t *req)
{
    bool ok = http_api_launched();
    if (!ok) http_json_send(req, "503 Service Unavailable", http_json_error("The clock is still starting"));
    return ok;
}

/**
 * @brief The items as JSON.
 * @version 1
 * @utility
 * @return The array.
 */
static cJSON *items_json(const lib_item_t *it, int n)
{
    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < n; i++) {
        cJSON *o = cJSON_CreateObject();
        cJSON_AddStringToObject(o, "name", it[i].name);
        cJSON_AddStringToObject(o, "label", it[i].label);
        cJSON_AddNumberToObject(o, "size", it[i].size);
        cJSON_AddNumberToObject(o, "mtime", it[i].mtime);
        cJSON_AddBoolToObject(o, "thumb", it[i].thumb);
        cJSON_AddItemToArray(arr, o);
    }
    return arr;
}

/**
 * @brief GET /api/library?kind=: what the kind holds, and the card's space.
 * @version 1
 * @callback
 * @req REQ-LIB-001
 * @return ESP_OK.
 */
static esp_err_t list_handler(httpd_req_t *req)
{
    char k[16];
    query(req, "kind", k, sizeof(k));
    lib_kind_t kind = kind_of(k);
    lib_item_t *items = kind < LIB_KINDS && launched(req)
                        ? heap_caps_malloc(sizeof(lib_item_t) * LIB_ALBUM_MAX, MALLOC_CAP_SPIRAM) : NULL;
    if (items) {
        uint32_t total = 0, free_mb = 0;
        int n = library_store_list(kind, items, LIB_ALBUM_MAX);
        library_store_storage(&total, &free_mb);
        cJSON *o = cJSON_CreateObject();
        cJSON_AddItemToObject(o, "items", items_json(items, n));
        cJSON_AddNumberToObject(o, "total_mb", total);
        cJSON_AddNumberToObject(o, "free_mb", free_mb);
        http_json_send(req, "200 OK", o);
    } else if (kind >= LIB_KINDS) {
        http_json_send(req, "400 Bad Request", http_json_error("No such kind"));
    }
    heap_caps_free(items);
    return ESP_OK;
}

/**
 * @brief Send file @p path (at most THUMB_MAX) as a PNG, or 404.
 * @version 1
 * @utility
 */
static void send_png(httpd_req_t *req, const char *path)
{
    FILE *f = fopen(path, "rb");
    uint8_t *buf = f ? heap_caps_malloc(THUMB_MAX, MALLOC_CAP_SPIRAM) : NULL;
    size_t len = buf ? fread(buf, 1, THUMB_MAX, f) : 0;
    if (f) fclose(f);
    if (len > 0 && len < THUMB_MAX) {
        httpd_resp_set_type(req, "image/png");
        httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
        httpd_resp_send(req, (const char *)buf, (ssize_t)len);
    } else {
        http_json_send(req, "404 Not Found", http_json_error("No thumbnail yet"));
    }
    heap_caps_free(buf);
}

/**
 * @brief GET /api/library/thumb?kind=&name=: a picture's thumbnail.
 * @version 1
 * @callback
 * @req REQ-LIB-001
 * @return ESP_OK.
 */
static esp_err_t thumb_handler(httpd_req_t *req)
{
    char k[16], name[LIB_NAME_MAX + 8], path[160];
    query(req, "kind", k, sizeof(k));
    query(req, "name", name, sizeof(name));
    bool there = launched(req) && library_store_thumb_path(kind_of(k), name, path, sizeof(path));
    if (there) send_png(req, path);
    else if (http_api_launched()) http_json_send(req, "404 Not Found", http_json_error("No thumbnail yet"));
    return ESP_OK;
}

/**
 * @brief GET /api/library/theme?stem=: the theme file, to edit or duplicate on the phone.
 * @version 1
 * @callback
 * @req REQ-LIB-001
 * @return ESP_OK.
 */
static esp_err_t theme_handler(httpd_req_t *req)
{
    static ui_theme_t t;   // off the httpd stack
    enum { JSON_MAX = 2048 };
    char stem[LIB_NAME_MAX + 8];
    query(req, "stem", stem, sizeof(stem));
    char *json = heap_caps_malloc(JSON_MAX, MALLOC_CAP_SPIRAM);
    bool ok = json && launched(req) && lib_name_ok(stem) && theme_store_load(stem, &t) &&
              ui_theme_to_json(&t, json, JSON_MAX);
    if (ok) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
    } else if (http_api_launched()) {
        http_json_send(req, "404 Not Found", http_json_error("No such theme"));
    }
    heap_caps_free(json);
    return ESP_OK;
}

/**
 * @brief The status for a Library change's result @p err.
 * @version 1
 * @utility
 * @return It.
 */
static const char *status_of(esp_err_t err)
{
    return err == ESP_OK ? "200 OK" : err == ESP_ERR_INVALID_STATE ? "409 Conflict"
           : err == ESP_ERR_INVALID_ARG ? "400 Bad Request" : "500 Internal Server Error";
}

/**
 * @brief String @p key of @p o, or "".
 * @version 1
 * @utility
 * @return It.
 */
static const char *str_of(const cJSON *o, const char *key)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
    return cJSON_IsString(v) ? v->valuestring : "";
}

/**
 * @brief POST /api/library/delete {kind, name} and /api/library/rename {kind, from, to}
 *        (@p req's user_ctx non-NULL: rename).
 * @version 1
 * @callback
 * @req REQ-LIB-001
 * @return ESP_OK.
 */
static esp_err_t change_handler(httpd_req_t *req)
{
    if (!launched(req)) return ESP_OK;
    char *body = http_json_body(req, 512);
    cJSON *in = body ? cJSON_Parse(body) : NULL;
    free(body);
    lib_kind_t kind = kind_of(str_of(in, "kind"));
    char why[128] = "Not a Library item";
    esp_err_t err = ESP_ERR_INVALID_ARG;
    bool rename = req->user_ctx != NULL;
    if (kind < LIB_KINDS && rename) err = library_store_rename(kind, str_of(in, "from"), str_of(in, "to"), why, sizeof(why));
    else if (kind < LIB_KINDS) err = library_store_delete(kind, str_of(in, "name"), why, sizeof(why));
    cJSON_Delete(in);
    ESP_LOGI(TAG, "%s from the panel: %s", rename ? "rename" : "delete", err == ESP_OK ? "done" : why);
    cJSON *out = err == ESP_OK ? cJSON_CreateObject() : http_json_error(why);
    if (err == ESP_OK) cJSON_AddStringToObject(out, "status", "ok");
    http_json_send(req, status_of(err), out);
    return ESP_OK;
}

/**
 * @brief Register the Library endpoints.
 * @version 1
 * @req REQ-LIB-001
 */
void http_library_register(httpd_handle_t srv)
{
    static const httpd_uri_t URIS[] = {
        { .uri = "/api/library", .method = HTTP_GET, .handler = list_handler },
        { .uri = "/api/library/thumb", .method = HTTP_GET, .handler = thumb_handler },
        { .uri = "/api/library/theme", .method = HTTP_GET, .handler = theme_handler },
        { .uri = "/api/library/delete", .method = HTTP_POST, .handler = change_handler },
        { .uri = "/api/library/rename", .method = HTTP_POST, .handler = change_handler, .user_ctx = (void *)1 },
    };
    for (size_t i = 0; i < sizeof(URIS) / sizeof(URIS[0]); i++) http_auth_register(srv, &URIS[i]);
}
