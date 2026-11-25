/**
 * @file settings_http.c
 * v2: upload: the loader page picks, crops and sends a picture; the result comes back
 *     through panel_upload_done.
 * @version 2
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

static settings_values_t s_values;
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

typedef struct {
    settings_done_cb_t done;
    void              *ctx;
    char              *body;   /**< The request's own copy: emscripten_fetch keeps only the
                                    pointer and reads it later, so it lives until the answer. */
} pending_t;

/**
 * @brief Why a request failed, for a person: the clock's "error" text, or the status.
 * @version 1
 * @utility
 */
static void failure_text(emscripten_fetch_t *f, char *out, size_t n)
{
    cJSON *o = f->numBytes ? cJSON_ParseWithLength(f->data, (size_t)f->numBytes) : NULL;
    const cJSON *e = cJSON_GetObjectItemCaseSensitive(o, "error");
    if (f->status == 401) snprintf(out, n, "Session ended: scan the code on the clock again");
    else if (cJSON_IsString(e)) snprintf(out, n, "%s", e->valuestring);
    else snprintf(out, n, "The clock did not answer (%d)", f->status);
    cJSON_Delete(o);
}

/**
 * @brief A request finished: tell whoever asked (or show a failure).
 * v2: frees the request's body copy.
 * @version 2
 * @callback
 */
static void fetch_done(emscripten_fetch_t *f)
{
    pending_t *p = (pending_t *)f->userData;
    char why[160] = "";
    bool ok = f->status == 200;
    if (!ok) failure_text(f, why, sizeof(why));
    if (p && p->done) p->done(ok, why, p->ctx);
    else if (!ok) settings_app_toast(why, false);
    if (p) free(p->body);
    free(p);
    emscripten_fetch_close(f);
}

/**
 * @brief POST @p body (JSON) to @p path with the session token; @p done (may be NULL)
 *        hears how it went. The body is copied: the caller may free its own at once.
 * v2: the request keeps its own copy of the body until it finishes (emscripten_fetch keeps
 *     only the pointer; a caller freeing it right away sent freed memory, which the clock
 *     refused as invalid JSON).
 * @version 2
 * @utility
 */
static void post(const char *path, const char *body, settings_done_cb_t done, void *ctx)
{
    static char auth[96];
    snprintf(auth, sizeof(auth), "Bearer %s", s_token);
    const char *headers[] = { "Authorization", auth, "Content-Type", "application/json", NULL };
    pending_t *p = calloc(1, sizeof(*p));
    char *copy = p ? strdup(body) : NULL;
    if (!copy) {
        free(p);
        if (done) done(false, "Out of memory", ctx);
        return;
    }
    *p = (pending_t){ done, ctx, copy };
    emscripten_fetch_attr_t attr;
    emscripten_fetch_attr_init(&attr);
    strcpy(attr.requestMethod, "POST");
    attr.attributes = EMSCRIPTEN_FETCH_LOAD_TO_MEMORY;
    attr.requestHeaders = headers;   // copied by emscripten_fetch
    attr.requestData = copy;
    attr.requestDataSize = strlen(copy);
    attr.onsuccess = fetch_done;
    attr.onerror = fetch_done;
    attr.userData = p;
    emscripten_fetch(&attr, path);
}

/**
 * @brief POST one setting: {"@p key": @p value} (a string) or a number when @p is_num.
 * @version 1
 * @utility
 */
static void post_setting(const char *key, const char *value, int num, bool is_num)
{
    cJSON *o = cJSON_CreateObject();
    if (is_num) cJSON_AddNumberToObject(o, key, num);
    else cJSON_AddStringToObject(o, key, value);
    char *body = cJSON_PrintUnformatted(o);
    cJSON_Delete(o);
    if (body) post("/api/settings", body, NULL, NULL);
    free(body);
}

// ---------------------------------------------------------------------------
// The backend
// ---------------------------------------------------------------------------

/**
 * @brief The cached values.
 * @version 1
 * @callback
 */
static void http_get(settings_values_t *out)
{
    *out = s_values;
}

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
 * @brief Brightness.
 * @version 1
 * @callback
 */
static void http_set_brightness(int pct)
{
    s_values.brightness = pct;
    post_setting("brightness", NULL, pct, true);
}

/**
 * @brief Background.
 * @version 1
 * @callback
 */
static void http_set_background(const char *path)
{
    snprintf(s_values.background, sizeof(s_values.background), "%s", path);
    post_setting("background_image", path, 0, false);
}

/**
 * @brief Theme.
 * @version 1
 * @callback
 */
static void http_set_theme(const char *stem)
{
    snprintf(s_values.theme, sizeof(s_values.theme), "%s", stem);
    post_setting("theme", stem, 0, false);
}

/**
 * @brief Clock animation.
 * @version 1
 * @callback
 */
static void http_set_anim(const char *name)
{
    snprintf(s_values.anim, sizeof(s_values.anim), "%s", name);
    post_setting("clock_anim", name, 0, false);
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

/**
 * @brief Send an edited theme.
 * @version 1
 * @callback
 */
static void http_save_theme(const ui_theme_t *t, settings_done_cb_t done, void *ctx)
{
    char body[2048];
    if (ui_theme_to_json(t, body, sizeof(body))) post("/api/theme/custom", body, done, ctx);
    else done(false, "Theme too large", ctx);
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
 * @version 1
 * @utility
 * @return Whether it is usable.
 */
static bool read_state(const char *json)
{
    cJSON *root = cJSON_Parse(json);
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(root, "values");
    const cJSON *b = cJSON_GetObjectItemCaseSensitive(v, "brightness");
    s_values.brightness = cJSON_IsNumber(b) ? b->valueint : 50;
    copy_str(v, "background", s_values.background, sizeof(s_values.background));
    copy_str(v, "theme", s_values.theme, sizeof(s_values.theme));
    copy_str(v, "anim", s_values.anim, sizeof(s_values.anim));
    copy_str(root, "version", s_version, sizeof(s_version));
    const cJSON *lists = cJSON_GetObjectItemCaseSensitive(root, "lists");
    read_list(lists, "backgrounds", SETTINGS_LIST_BACKGROUNDS);
    read_list(lists, "themes", SETTINGS_LIST_THEMES);
    read_list(lists, "anims", SETTINGS_LIST_ANIMS);
    char *sched = cJSON_PrintUnformatted(cJSON_GetObjectItemCaseSensitive(root, "schedule"));
    s_n = sched ? sched_from_json(sched, s_rules, SCHED_MAX_PROFILES) : -1;
    free(sched);
    bool ok = root && v && read_now_and_sun(root) && s_n >= 0;
    if (s_n < 0) s_n = 0;
    cJSON_Delete(root);
    return ok;
}

/**
 * @brief The backend over the clock's state.
 * v2: upload.
 * v3: no leave (the phone's menu has no Back).
 * @version 3
 * @req REQ-UI-005
 * @return It, or NULL when the state is not usable.
 */
const settings_backend_t *settings_http_backend(const char *state_json, const char *token)
{
    static const settings_backend_t BE = {
        .get = http_get, .list = http_list, .set_brightness = http_set_brightness,
        .set_background = http_set_background, .set_theme = http_set_theme,
        .set_anim = http_set_anim, .schedule_get = http_schedule_get,
        .schedule_set = http_schedule_set, .sun = http_sun, .now = http_now,
        .save_theme = http_save_theme, .about = http_about, .upload = http_upload,
    };
    snprintf(s_token, sizeof(s_token), "%s", token ? token : "");
    return state_json && read_state(state_json) ? &BE : NULL;
}
