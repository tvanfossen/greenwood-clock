/**
 * @file settings_http.c
 * v2: upload: the loader page picks, crops and sends a picture; the result comes back
 *     through panel_upload_done.
 * v3: no global settings; a named theme is kept (POST /api/theme/save).
 * v4: the clock's place and timezone.
 * v5: settings_http_request (any request, raw answer) for the Library.
 * @version 5
 * @brief The phone panel's settings backend (WebAssembly): the state GET /api/panel returned
 *        at start is the cache every getter reads; every change updates the cache and is
 *        POSTed to the clock with the session token (emscripten fetch, asynchronous). The
 *        clock's "now" runs on from the moment the state was read; its sun is the year of
 *        sunrise/sunset the state carries.
 */
#include "settings_http.h"
#include "settings_app.h"
#include "cJSON.h"

#include <emscripten.h>
#include <emscripten/fetch.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LIST_MAX  32
#define SUN_DAYS  366

static settings_choice_t s_lists[3][LIST_MAX];
static int               s_list_n[3];
static sched_rule_t      s_rules[SCHED_MAX_PROFILES];
static int               s_n;
static sched_sun_t       s_sun[SUN_DAYS];
static int               s_sun_n;
static sched_date_t      s_today;          // the clock's date and minute when the state was read
static int               s_min;
static double            s_read_ms;        // emscripten_get_now() then
static char              s_token[64];
static char              s_version[16];

// ---------------------------------------------------------------------------
// Dates
// ---------------------------------------------------------------------------

/**
 * @brief Days since an epoch for a Gregorian date (to subtract two dates).
 * @version 1
 * @utility
 * @return The day number.
 */
static long day_number(sched_date_t d)
{
    long y = d.year, m = d.month;
    if (m <= 2) {
        y--;
        m += 12;
    }
    return 365 * y + y / 4 - y / 100 + y / 400 + (153 * (m - 3) + 2) / 5 + d.day;
}

/**
 * @brief The date @p n days after @p d.
 * @version 1
 * @utility
 * @return It.
 */
static sched_date_t add_days(sched_date_t d, int n)
{
    static const int DAYS[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    for (int i = 0; i < n; i++) {
        bool leap = (d.year % 4 == 0 && d.year % 100 != 0) || d.year % 400 == 0;
        int dim = d.month == 2 && leap ? 29 : DAYS[d.month - 1];
        if (++d.day > dim) {
            d.day = 1;
            if (++d.month > 12) {
                d.month = 1;
                d.year++;
            }
        }
    }
    return d;
}

// ---------------------------------------------------------------------------
// Talking to the clock
// ---------------------------------------------------------------------------

/** A request in flight: whom to tell, and its own copy of the body (emscripten_fetch keeps
 *  only the pointer and reads it later, so it lives until the answer). */
typedef struct {
    settings_http_raw_cb_t cb;
    void                  *ctx;
    char                  *body;
} raw_t;

/**
 * @brief A request finished: hand its status and answer to whoever asked.
 * @version 1
 * @callback
 */
static void raw_done(emscripten_fetch_t *f)
{
    raw_t *r = (raw_t *)f->userData;
    r->cb(f->status, f->data, (size_t)f->numBytes, r->ctx);
    free(r->body);
    free(r);
    emscripten_fetch_close(f);
}

/**
 * @brief Send @p method @p path (with JSON @p body, or none) and the session token; @p cb
 *        gets the status and the raw answer.
 * @version 1
 * @req REQ-UI-005
 */
void settings_http_request(const char *method, const char *path, const char *body,
                           settings_http_raw_cb_t cb, void *ctx)
{
    static char auth[96];
    snprintf(auth, sizeof(auth), "Bearer %s", s_token);
    const char *headers[] = { "Authorization", auth, "Content-Type", "application/json", NULL };
    raw_t *r = calloc(1, sizeof(*r));
    char *copy = r && body ? strdup(body) : NULL;
    if (!r || (body && !copy)) {
        free(r);
        cb(0, NULL, 0, ctx);
        return;
    }
    *r = (raw_t){ cb, ctx, copy };
    emscripten_fetch_attr_t attr;
    emscripten_fetch_attr_init(&attr);
    snprintf(attr.requestMethod, sizeof(attr.requestMethod), "%s", method);
    attr.attributes = EMSCRIPTEN_FETCH_LOAD_TO_MEMORY;
    attr.requestHeaders = headers;   // copied by emscripten_fetch
    attr.requestData = copy;
    attr.requestDataSize = copy ? strlen(copy) : 0;
    attr.onsuccess = raw_done;
    attr.onerror = raw_done;
    attr.userData = r;
    emscripten_fetch(&attr, path);
}

/**
 * @brief Why a request failed, for a person: the clock's "error" text, or the status.
 * v2: from the status and answer (any request).
 * @version 2
 * @req REQ-UI-005
 */
void settings_http_failure(int status, const char *data, size_t len, char *out, size_t n)
{
    cJSON *o = data && len ? cJSON_ParseWithLength(data, len) : NULL;
    const cJSON *e = cJSON_GetObjectItemCaseSensitive(o, "error");
    if (status == 401) snprintf(out, n, "Session ended: scan the code on the clock again");
    else if (cJSON_IsString(e)) snprintf(out, n, "%s", e->valuestring);
    else snprintf(out, n, "The clock did not answer (%d)", status);
    cJSON_Delete(o);
}

typedef struct {
    settings_done_cb_t done;
    void              *ctx;
} pending_t;

static char s_reply[512];   // the last request's answer, read by its callback

/**
 * @brief A POST finished: tell whoever asked (or show a failure).
 * v2: frees the request's body copy.
 * v3: keeps the reply (s_reply) for the callback.
 * v4: over settings_http_request.
 * @version 4
 * @callback
 */
static void post_done(int status, const char *data, size_t len, void *ctx)
{
    pending_t *p = ctx;
    char why[160] = "";
    bool ok = status == 200;
    size_t n = len < sizeof(s_reply) - 1 ? len : sizeof(s_reply) - 1;
    memcpy(s_reply, data ? data : "", data ? n : 0);   // the answer, for its caller
    s_reply[data ? n : 0] = '\0';
    if (!ok) settings_http_failure(status, data, len, why, sizeof(why));
    if (p && p->done) p->done(ok, why, p->ctx);
    else if (!ok) settings_app_toast(why, false);
    free(p);
}

/**
 * @brief POST @p body (JSON) to @p path with the session token; @p done (may be NULL)
 *        hears how it went. The body is copied: the caller may free its own at once.
 * v2: the request keeps its own copy of the body until it finishes.
 * v3: over settings_http_request.
 * @version 3
 * @utility
 */
static void post(const char *path, const char *body, settings_done_cb_t done, void *ctx)
{
    pending_t *p = calloc(1, sizeof(*p));
    if (!p) {
        if (done) done(false, "Out of memory", ctx);
        return;
    }
    *p = (pending_t){ done, ctx };
    settings_http_request("POST", path, body, post_done, p);
}

/**
 * @brief The cached schedule, for the Library's rename to follow.
 * @version 1
 * @req REQ-LIB-001
 * @return It; its length in @p n.
 */
sched_rule_t *settings_http_rules(int *n)
{
    *n = s_n;
    return s_rules;
}

// ---------------------------------------------------------------------------
// The backend
// ---------------------------------------------------------------------------

/**
 * @brief A cached list.
 * @version 1
 * @callback
 * @return How many.
 */
static int http_list(settings_list_t which, settings_choice_t *out, int max)
{
    int n = s_list_n[which] < max ? s_list_n[which] : max;
    memcpy(out, s_lists[which], sizeof(settings_choice_t) * (size_t)n);
    return n;
}

/**
 * @brief The cached schedule.
 * @version 1
 * @callback
 * @return How many.
 */
static int http_schedule_get(sched_rule_t *out, int max)
{
    int n = s_n < max ? s_n : max;
    memcpy(out, s_rules, sizeof(sched_rule_t) * (size_t)n);
    return n;
}

typedef struct {
    sched_rule_t       rules[SCHED_MAX_PROFILES];
    int                n;
    settings_done_cb_t done;
    void              *ctx;
} sched_save_t;

static sched_save_t s_save;   // one schedule save at a time

/**
 * @brief The clock answered a schedule save: adopt it into the cache when it was kept.
 * @version 1
 * @callback
 */
static void schedule_saved(bool ok, const char *why, void *ctx)
{
    (void)ctx;
    if (ok) {
        memcpy(s_rules, s_save.rules, sizeof(sched_rule_t) * (size_t)s_save.n);
        s_n = s_save.n;
    }
    s_save.done(ok, why, s_save.ctx);
}

/**
 * @brief Send the schedule to the clock (it checks overlaps too).
 * @version 1
 * @callback
 */
static void http_schedule_set(const sched_rule_t *r, int n, settings_done_cb_t done, void *ctx)
{
    memcpy(s_save.rules, r, sizeof(sched_rule_t) * (size_t)n);
    s_save.n = n;
    s_save.done = done;
    s_save.ctx = ctx;
    char *body = sched_to_json(r, n);
    if (body) post("/api/schedule", body, schedule_saved, NULL);
    else done(false, "Out of memory", ctx);
    free(body);
}

/**
 * @brief The clock's sun on @p d, from the table (6:00/18:00 outside it).
 * @version 1
 * @callback
 * @return The sun times.
 */
static sched_sun_t table_sun(sched_date_t d, void *ctx)
{
    (void)ctx;
    long i = day_number(d) - day_number(s_today);
    return i >= 0 && i < s_sun_n ? s_sun[i] : (sched_sun_t){ 6 * 60, 18 * 60 };
}

static const sched_sun_src_t SUN = { table_sun, NULL };

/**
 * @brief The table's sun.
 * @version 1
 * @callback
 * @return It.
 */
static const sched_sun_src_t *http_sun(void)
{
    return &SUN;
}

/**
 * @brief The clock's now: the state's moment plus the time since.
 * @version 1
 * @callback
 */
static void http_now(sched_date_t *day, int *min)
{
    int m = s_min + (int)((emscripten_get_now() - s_read_ms) / 60000.0);
    *day = add_days(s_today, m / (24 * 60));
    *min = m % (24 * 60);
}

/** A theme being saved: what to add to the theme list once the clock has it. */
typedef struct {
    settings_done_cb_t done;
    void              *ctx;
    settings_choice_t  entry;
} theme_pending_t;

static theme_pending_t s_theme_save;

/**
 * @brief The clock kept the theme (or not): list it (once), then tell the caller.
 * @version 1
 * @callback
 */
static void theme_saved_cb(bool ok, const char *why, void *ctx)
{
    LV_UNUSED(ctx);
    settings_choice_t *list = s_lists[SETTINGS_LIST_THEMES];
    int *n = &s_list_n[SETTINGS_LIST_THEMES];
    int at = 0;
    while (at < *n && strcmp(list[at].value, s_theme_save.entry.value) != 0) at++;
    if (ok && at < LIST_MAX) {
        list[at] = s_theme_save.entry;   // replaces one of the same stem, else appends
        if (at == *n) (*n)++;
    }
    s_theme_save.done(ok, why, s_theme_save.ctx);
}

/**
 * @brief Send an edited theme to be kept as the theme file @p stem.
 * v2: a named theme file (POST /api/theme/save?stem=), listed once kept.
 * @version 2
 * @callback
 */
static void http_save_theme(const ui_theme_t *t, const char *stem, settings_done_cb_t done,
                            void *ctx)
{
    char body[2048], path[64];
    if (!ui_theme_to_json(t, body, sizeof(body))) {
        done(false, "Theme too large", ctx);
        return;
    }
    s_theme_save = (theme_pending_t){ done, ctx, { "", "" } };
    snprintf(s_theme_save.entry.value, sizeof(s_theme_save.entry.value), "%s", stem);
    snprintf(s_theme_save.entry.label, sizeof(s_theme_save.entry.label), "%s", t->name);
    snprintf(path, sizeof(path), "/api/theme/save?stem=%s", stem);
    post(path, body, theme_saved_cb, NULL);
}

static void copy_str(const cJSON *o, const char *key, char *out, size_t n);

static settings_location_t s_location;   // where the clock is (from the state)

/**
 * @brief Where the clock is.
 * @version 1
 * @callback
 */
static void http_location_get(settings_location_t *out)
{
    *out = s_location;
}

/** A lookup in flight: whom to tell. */
static struct {
    settings_lookup_cb_t done;
    void                *ctx;
} s_lookup;

/**
 * @brief The clock answered the lookup: the place in its reply, or why not.
 * @version 1
 * @callback
 */
static void lookup_done(bool ok, const char *why, void *ctx)
{
    LV_UNUSED(ctx);
    settings_location_t found = s_location;
    cJSON *o = ok ? cJSON_Parse(s_reply) : NULL;
    const cJSON *lat = cJSON_GetObjectItemCaseSensitive(o, "lat");
    const cJSON *lon = cJSON_GetObjectItemCaseSensitive(o, "lon");
    ok = ok && cJSON_IsNumber(lat) && cJSON_IsNumber(lon);
    if (ok) {
        copy_str(o, "label", found.label, sizeof(found.label));
        found.lat = (float)lat->valuedouble;
        found.lon = (float)lon->valuedouble;
    }
    cJSON_Delete(o);
    s_lookup.done(ok, why, ok ? &found : NULL, s_lookup.ctx);
}

/**
 * @brief Ask the clock to look an address up (it asks the geocoder; the address is not kept).
 * @version 1
 * @callback
 */
static void http_location_lookup(const char *address, settings_lookup_cb_t done, void *ctx)
{
    s_lookup.done = done;
    s_lookup.ctx = ctx;
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "address", address);
    char *body = cJSON_PrintUnformatted(o);
    cJSON_Delete(o);
    if (body) post("/api/location/lookup", body, lookup_done, NULL);
    else done(false, "Out of memory", NULL, ctx);
    free(body);
}

/** A place being kept: what, and whom to tell. */
static struct {
    settings_location_t loc;
    settings_done_cb_t  done;
    void               *ctx;
} s_place;

/**
 * @brief The clock kept the place (or not): remember it, then tell the caller.
 * @version 1
 * @callback
 */
static void place_saved(bool ok, const char *why, void *ctx)
{
    LV_UNUSED(ctx);
    if (ok) s_location = s_place.loc;
    s_place.done(ok, why, s_place.ctx);
}

/**
 * @brief Send a place and zone to keep.
 * @version 1
 * @callback
 */
static void http_location_set(const settings_location_t *loc, settings_done_cb_t done, void *ctx)
{
    s_place.loc = *loc;
    s_place.done = done;
    s_place.ctx = ctx;
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "label", loc->label);
    cJSON_AddNumberToObject(o, "lat", loc->lat);
    cJSON_AddNumberToObject(o, "lon", loc->lon);
    cJSON_AddStringToObject(o, "tz", loc->tz);
    char *body = cJSON_PrintUnformatted(o);
    cJSON_Delete(o);
    if (body) post("/api/location", body, place_saved, NULL);
    else done(false, "Out of memory", ctx);
    free(body);
}

/**
 * @brief What the panel knows about the clock.
 * @version 1
 * @callback
 */
static void http_about(char *out, size_t n)
{
    snprintf(out, n, "Greenwood Clock %s\n\nControlled from this phone.\nThe session lasts 30 "
                     "minutes from the scan.", s_version);
}

typedef struct {
    settings_done_cb_t done;
    void              *ctx;
    settings_upload_t  kind;
} upload_pending_t;

static upload_pending_t s_upload;   // one upload at a time

EM_JS(void, js_upload, (int kind, const char *token), {
    window.panelUpload(kind, UTF8ToString(token));
});

/**
 * @brief The loader page finished an upload: tell the screen, and add a new background to
 *        the cached list so the pickers offer it at once.
 * @version 1
 * @callback
 */
EMSCRIPTEN_KEEPALIVE void panel_upload_done(int ok, const char *name, const char *msg)
{
    if (ok && s_upload.kind == SETTINGS_UPLOAD_BACKGROUND && s_list_n[0] < LIST_MAX) {
        settings_choice_t *c = &s_lists[0][s_list_n[0]++];
        snprintf(c->value, sizeof(c->value), "A:/backgrounds/%s", name);
        snprintf(c->label, sizeof(c->label), "%.*s", (int)strcspn(name, "."), name);
    }
    if (s_upload.done) s_upload.done(ok != 0, msg, s_upload.ctx);
    s_upload.done = NULL;
}

/**
 * @brief Pick and send a picture (the loader page does the picking, cropping and POST).
 * @version 1
 * @callback
 */
static void http_upload(settings_upload_t kind, settings_done_cb_t done, void *ctx)
{
    s_upload = (upload_pending_t){ done, ctx, kind };
    js_upload((int)kind, s_token);
}

// ---------------------------------------------------------------------------
// Reading the state
// ---------------------------------------------------------------------------

/**
 * @brief Copy string @p key of @p o into @p out.
 * @version 1
 * @utility
 */
static void copy_str(const cJSON *o, const char *key, char *out, size_t n)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
    snprintf(out, n, "%s", cJSON_IsString(v) ? v->valuestring : "");
}

/**
 * @brief The [value, label] pairs of list @p key.
 * @version 1
 * @utility
 */
static void read_list(const cJSON *lists, const char *key, int which)
{
    const cJSON *pair;
    s_list_n[which] = 0;
    cJSON_ArrayForEach(pair, cJSON_GetObjectItemCaseSensitive(lists, key)) {
        if (s_list_n[which] >= LIST_MAX || cJSON_GetArraySize(pair) != 2) continue;
        settings_choice_t *c = &s_lists[which][s_list_n[which]++];
        snprintf(c->value, sizeof(c->value), "%s", cJSON_GetArrayItem(pair, 0)->valuestring);
        snprintf(c->label, sizeof(c->label), "%s", cJSON_GetArrayItem(pair, 1)->valuestring);
    }
}

/**
 * @brief "now" and "sun".
 * @version 1
 * @utility
 * @return Whether "now" is a date.
 */
static bool read_now_and_sun(const cJSON *root)
{
    const cJSON *now = cJSON_GetObjectItemCaseSensitive(root, "now");
    const cJSON *date = cJSON_GetObjectItemCaseSensitive(now, "date");
    const cJSON *min = cJSON_GetObjectItemCaseSensitive(now, "min");
    unsigned y = 0, m = 0, d = 0;
    bool ok = cJSON_IsString(date) && cJSON_IsNumber(min) &&
              sscanf(date->valuestring, "%u-%u-%u", &y, &m, &d) == 3;
    s_today = (sched_date_t){ (uint16_t)y, (uint8_t)m, (uint8_t)d };
    s_min = ok ? min->valueint : 0;
    s_read_ms = emscripten_get_now();
    const cJSON *pair;
    s_sun_n = 0;
    cJSON_ArrayForEach(pair, cJSON_GetObjectItemCaseSensitive(root, "sun")) {
        if (s_sun_n >= SUN_DAYS) break;
        s_sun[s_sun_n++] = (sched_sun_t){ cJSON_GetArrayItem(pair, 0)->valueint,
                                          cJSON_GetArrayItem(pair, 1)->valueint };
    }
    return ok;
}

/**
 * @brief Read the whole state.
 * v2: no global values.
 * v3: the clock's location.
 * @version 3
 * @utility
 * @return Whether it is usable.
 */
static bool read_state(const char *json)
{
    cJSON *root = cJSON_Parse(json);
    copy_str(root, "version", s_version, sizeof(s_version));
    const cJSON *where = cJSON_GetObjectItemCaseSensitive(root, "location");
    const cJSON *lat = cJSON_GetObjectItemCaseSensitive(where, "lat");
    const cJSON *lon = cJSON_GetObjectItemCaseSensitive(where, "lon");
    copy_str(where, "label", s_location.label, sizeof(s_location.label));
    copy_str(where, "tz", s_location.tz, sizeof(s_location.tz));
    s_location.lat = cJSON_IsNumber(lat) ? (float)lat->valuedouble : 0.0f;
    s_location.lon = cJSON_IsNumber(lon) ? (float)lon->valuedouble : 0.0f;
    const cJSON *lists = cJSON_GetObjectItemCaseSensitive(root, "lists");
    read_list(lists, "backgrounds", SETTINGS_LIST_BACKGROUNDS);
    read_list(lists, "themes", SETTINGS_LIST_THEMES);
    read_list(lists, "anims", SETTINGS_LIST_ANIMS);
    char *sched = cJSON_PrintUnformatted(cJSON_GetObjectItemCaseSensitive(root, "schedule"));
    s_n = sched ? sched_from_json(sched, s_rules, SCHED_MAX_PROFILES) : -1;
    free(sched);
    bool ok = root && read_now_and_sun(root) && s_n >= 0;
    if (s_n < 0) s_n = 0;
    cJSON_Delete(root);
    return ok;
}

/**
 * @brief The backend over the clock's state.
 * v2: upload.
 * v3: no leave (the phone's menu has no Back).
 * v4: no global setters; save_theme keeps a named theme.
 * v5: the location.
 * v6: the Library (settings_http_library.c).
 * @version 6
 * @req REQ-UI-005
 * @return It, or NULL when the state is not usable.
 */
const settings_backend_t *settings_http_backend(const char *state_json, const char *token)
{
    static settings_backend_t BE = {
        .list = http_list, .schedule_get = http_schedule_get,
        .schedule_set = http_schedule_set, .sun = http_sun, .now = http_now,
        .save_theme = http_save_theme, .about = http_about, .upload = http_upload,
        .location_get = http_location_get, .location_lookup = http_location_lookup,
        .location_set = http_location_set,
    };
    settings_http_library_fill(&BE);
    snprintf(s_token, sizeof(s_token), "%s", token ? token : "");
    return state_json && read_state(state_json) ? &BE : NULL;
}
