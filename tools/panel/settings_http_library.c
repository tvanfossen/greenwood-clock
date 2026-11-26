/**
 * @file settings_http_library.c
 * @version 1
 * @brief The phone panel's Library calls (REQ-LIB-001) over the clock's /api/library
 *        endpoints: the list and its space, thumbnails (decoded here), a theme file, delete
 *        and rename (the cached schedule follows a rename, as the clock's does).
 */
#include "settings_http.h"
#include "library_thumb.h"
#include "cJSON.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const KINDS[LIB_KINDS] = { "backgrounds", "album", "themes", "anims" };

/** A call in flight: whom to tell. */
typedef struct {
    void      *done;   /**< The call's own callback type. */
    void      *ctx;
    lib_kind_t kind;
    char       from[LIB_NAME_MAX + 8], to[LIB_NAME_MAX + 8];
} call_t;

/**
 * @brief A new call record (NULL when out of memory).
 * @version 1
 * @utility
 * @return It.
 */
static call_t *call_new(void *done, void *ctx, lib_kind_t kind)
{
    call_t *c = calloc(1, sizeof(*c));
    if (c) *c = (call_t){ .done = done, .ctx = ctx, .kind = kind };
    return c;
}

/**
 * @brief Number @p key of @p o, or 0.
 * @version 1
 * @utility
 * @return It.
 */
static uint32_t num_of(const cJSON *o, const char *key)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
    return cJSON_IsNumber(v) ? (uint32_t)v->valuedouble : 0;
}

/**
 * @brief Item @p o of the list into @p it.
 * @version 1
 * @utility
 */
static void read_item(const cJSON *o, lib_item_t *it)
{
    const cJSON *name = cJSON_GetObjectItemCaseSensitive(o, "name");
    const cJSON *label = cJSON_GetObjectItemCaseSensitive(o, "label");
    memset(it, 0, sizeof(*it));
    snprintf(it->name, sizeof(it->name), "%s", cJSON_IsString(name) ? name->valuestring : "");
    snprintf(it->label, sizeof(it->label), "%s", cJSON_IsString(label) ? label->valuestring : it->name);
    it->size = num_of(o, "size");
    it->mtime = num_of(o, "mtime");
    it->thumb = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(o, "thumb"));
}

/**
 * @brief The list arrived.
 * @version 1
 * @callback
 */
static void listed(int status, const char *data, size_t len, void *ctx)
{
    static lib_item_t s_items[LIB_ALBUM_MAX];
    call_t *c = ctx;
    char why[160] = "";
    cJSON *o = status == 200 ? cJSON_ParseWithLength(data, len) : NULL;
    const cJSON *arr = cJSON_GetObjectItemCaseSensitive(o, "items");
    int n = 0;
    const cJSON *e;
    cJSON_ArrayForEach(e, arr) {
        if (n < LIB_ALBUM_MAX) read_item(e, &s_items[n++]);
    }
    if (!o) settings_http_failure(status, data, len, why, sizeof(why));
    ((settings_library_cb_t)c->done)(o != NULL, why, s_items, n, num_of(o, "total_mb"),
                                     num_of(o, "free_mb"), c->ctx);
    cJSON_Delete(o);
    free(c);
}

/**
 * @brief What a kind holds.
 * @version 1
 * @callback
 */
static void http_list(lib_kind_t kind, settings_library_cb_t done, void *ctx)
{
    char path[64];
    call_t *c = call_new((void *)done, ctx, kind);
    snprintf(path, sizeof(path), "/api/library?kind=%s", KINDS[kind]);
    if (c) settings_http_request("GET", path, NULL, listed, c);
    else done(false, "Out of memory", NULL, 0, 0, 0, ctx);
}

/**
 * @brief A thumbnail arrived: decode it.
 * @version 1
 * @callback
 */
static void thumbed(int status, const char *data, size_t len, void *ctx)
{
    call_t *c = ctx;
    lv_image_dsc_t *d = status == 200 ? library_thumb_decode((const uint8_t *)data, len) : NULL;
    ((settings_thumb_cb_t)c->done)(d, c->ctx);
    free(c);
}

/**
 * @brief A picture's thumbnail.
 * @version 1
 * @callback
 */
static void http_thumb(lib_kind_t kind, const char *name, settings_thumb_cb_t done, void *ctx)
{
    char path[128];
    call_t *c = call_new((void *)done, ctx, kind);
    snprintf(path, sizeof(path), "/api/library/thumb?kind=%s&name=%s", KINDS[kind], name);
    if (c) settings_http_request("GET", path, NULL, thumbed, c);
    else done(NULL, ctx);
}

/**
 * @brief A change answered: the schedule follows a rename; tell the caller.
 * @version 1
 * @callback
 */
static void changed(int status, const char *data, size_t len, void *ctx)
{
    call_t *c = ctx;
    char why[160] = "";
    bool ok = status == 200;
    if (!ok) settings_http_failure(status, data, len, why, sizeof(why));
    if (ok && c->to[0]) {
        char a[SCHED_IMAGE_LEN], b[SCHED_IMAGE_LEN];
        int n;
        sched_rule_t *r = settings_http_rules(&n);
        lib_ref(c->kind, c->from, a, sizeof(a));
        lib_ref(c->kind, c->to, b, sizeof(b));
        lib_rename_refs(r, n, c->kind, a, b);
    }
    ((settings_done_cb_t)c->done)(ok, why, c->ctx);
    free(c);
}

/**
 * @brief POST @p body to @p path as call @p c (or say out of memory).
 * @version 1
 * @utility
 */
static void send_change(const char *path, cJSON *body, call_t *c, settings_done_cb_t done, void *ctx)
{
    char *json = body ? cJSON_PrintUnformatted(body) : NULL;
    cJSON_Delete(body);
    if (c && json) settings_http_request("POST", path, json, changed, c);
    else done(false, "Out of memory", ctx);
    if (!(c && json)) free(c);
    free(json);
}

/**
 * @brief Delete an item.
 * @version 1
 * @callback
 */
static void http_delete(lib_kind_t kind, const char *name, settings_done_cb_t done, void *ctx)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "kind", KINDS[kind]);
    cJSON_AddStringToObject(o, "name", name);
    send_change("/api/library/delete", o, call_new((void *)done, ctx, kind), done, ctx);
}

/**
 * @brief Rename an item.
 * @version 1
 * @callback
 */
static void http_rename(lib_kind_t kind, const char *from, const char *to, settings_done_cb_t done,
                        void *ctx)
{
    call_t *c = call_new((void *)done, ctx, kind);
    if (c) {
        snprintf(c->from, sizeof(c->from), "%s", from);
        snprintf(c->to, sizeof(c->to), "%s", to);
    }
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "kind", KINDS[kind]);
    cJSON_AddStringToObject(o, "from", from);
    cJSON_AddStringToObject(o, "to", to);
    send_change("/api/library/rename", o, c, done, ctx);
}

/**
 * @brief A theme file arrived.
 * @version 1
 * @callback
 */
static void theme_read(int status, const char *data, size_t len, void *ctx)
{
    static ui_theme_t t;
    call_t *c = ctx;
    char *json = status == 200 ? strndup(data, len) : NULL;
    bool ok = json && ui_theme_parse(json, &t);
    free(json);
    ((settings_theme_cb_t)c->done)(ok, &t, c->ctx);
    free(c);
}

/**
 * @brief Read a theme file.
 * @version 1
 * @callback
 */
static void http_theme_load(const char *stem, settings_theme_cb_t done, void *ctx)
{
    char path[96];
    call_t *c = call_new((void *)done, ctx, LIB_THEMES);
    snprintf(path, sizeof(path), "/api/library/theme?stem=%s", stem);
    if (c) settings_http_request("GET", path, NULL, theme_read, c);
    else done(false, NULL, ctx);
}

/**
 * @brief Fill the backend's Library calls.
 * @version 1
 * @req REQ-LIB-001
 */
void settings_http_library_fill(settings_backend_t *be)
{
    be->library_list = http_list;
    be->library_thumb = http_thumb;
    be->library_delete = http_delete;
    be->library_rename = http_rename;
    be->theme_load = http_theme_load;
}
