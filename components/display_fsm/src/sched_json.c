/**
 * @file sched_json.c
 * @version 1
 * @brief The schedule file read and written (see sched_json.h). Pure C over cJSON.
 */
#include "sched_json.h"
#include "cJSON.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const REPEAT_NAMES[] = { "daily", "yearly", "once" };
#define MAX_OFFSET (12 * 60)   // a sun edge's offset, minutes either way

/**
 * @brief Parse a window edge: "HH:MM", or "sunrise"/"sunset" with an optional signed offset.
 * @version 1
 * @utility
 * @return Whether @p s is one (@p out set then).
 */
static bool parse_time(const char *s, sched_time_t *out)
{
    int h = -1, m = -1, off = 0, used = 0;
    bool ok = false;
    if (sscanf(s, "%2d:%2d%n", &h, &m, &used) == 2 && s[used] == '\0') {
        ok = h >= 0 && h < 24 && m >= 0 && m < 60;
        *out = (sched_time_t){ SCHED_AT_CLOCK, (int16_t)(h * 60 + m) };
    } else {
        bool rise = strncmp(s, "sunrise", 7) == 0, set = strncmp(s, "sunset", 6) == 0;
        const char *rest = s + (rise ? 7 : 6);
        ok = (rise || set) && (*rest == '\0' || (sscanf(rest, "%d%n", &off, &used) == 1 &&
                                                 rest[used] == '\0' && (*rest == '+' || *rest == '-')));
        ok = ok && off >= -MAX_OFFSET && off <= MAX_OFFSET;
        *out = (sched_time_t){ rise ? SCHED_AT_SUNRISE : SCHED_AT_SUNSET, (int16_t)off };
    }
    return ok;
}

/**
 * @brief Parse a date: "MM-DD" every year, "YYYY-MM-DD" once.
 * @version 1
 * @utility
 * @return Whether @p s is one for @p repeat (@p out set then).
 */
static bool parse_date(const char *s, sched_repeat_t repeat, sched_date_t *out)
{
    int y = 0, m = 0, d = 0, used = 0;
    bool ok = repeat == SCHED_ONCE
                  ? sscanf(s, "%4d-%2d-%2d%n", &y, &m, &d, &used) == 3 && y >= 2000 && y < 2200
                  : sscanf(s, "%2d-%2d%n", &m, &d, &used) == 2;
    ok = ok && s[used] == '\0' && m >= 1 && m <= 12 && d >= 1 && d <= 31;
    *out = (sched_date_t){ (uint16_t)y, (uint8_t)m, (uint8_t)d };
    return ok;
}

/**
 * @brief Copy the optional string @p key of @p o into @p out (@p n bytes); "" when absent.
 * @version 1
 * @utility
 * @return Whether it is absent, or a string that fits (and is not empty when @p required).
 */
static bool read_str(const cJSON *o, const char *key, char *out, size_t n, bool required)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
    out[0] = '\0';
    bool ok = v ? cJSON_IsString(v) && strlen(v->valuestring) < n : !required;
    if (ok && v) strcpy(out, v->valuestring);
    return ok && (!required || out[0]);
}

/**
 * @brief The repeat named by "repeat" in @p o.
 * @version 1
 * @utility
 * @return Whether it names one (@p out set then).
 */
static bool read_repeat(const cJSON *o, sched_repeat_t *out)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, "repeat");
    bool ok = false;
    for (int i = 0; i < 3 && cJSON_IsString(v) && !ok; i++) {
        ok = strcmp(v->valuestring, REPEAT_NAMES[i]) == 0;
        if (ok) *out = (sched_repeat_t)i;
    }
    return ok;
}

/**
 * @brief The window of @p o: both "start" and "end", or neither (all day).
 * @version 1
 * @utility
 * @return Whether it is valid.
 */
static bool read_window(const cJSON *o, sched_rule_t *r)
{
    const cJSON *a = cJSON_GetObjectItemCaseSensitive(o, "start");
    const cJSON *b = cJSON_GetObjectItemCaseSensitive(o, "end");
    r->all_day = !a && !b;
    return r->all_day || (cJSON_IsString(a) && cJSON_IsString(b) &&
                          parse_time(a->valuestring, &r->start) &&
                          parse_time(b->valuestring, &r->end));
}

/**
 * @brief The dates of an event ("from" and "to"); none for a daily profile.
 * @version 1
 * @utility
 * @return Whether they are valid for its repeat.
 */
static bool read_dates(const cJSON *o, sched_rule_t *r)
{
    char from[16], to[16];
    bool daily = r->repeat == SCHED_EVERY_DAY;
    return daily || (read_str(o, "from", from, sizeof(from), true) &&
                     read_str(o, "to", to, sizeof(to), true) &&
                     parse_date(from, r->repeat, &r->from) && parse_date(to, r->repeat, &r->to));
}

/**
 * @brief The optional brightness of @p o (0..100; SCHED_INHERIT when absent).
 * @version 1
 * @utility
 * @return Whether it is absent or in range.
 */
static bool read_brightness(const cJSON *o, int8_t *out)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, "brightness");
    bool ok = !v || (cJSON_IsNumber(v) && v->valueint >= 0 && v->valueint <= 100);
    *out = v && ok ? (int8_t)v->valueint : SCHED_INHERIT;
    return ok;
}

/**
 * @brief Parse one profile object.
 * @version 1
 * @utility
 * @return Whether it is valid (@p r fully set, unused fields zero).
 */
static bool parse_profile(const cJSON *o, sched_rule_t *r)
{
    memset(r, 0, sizeof(*r));
    return cJSON_IsObject(o) && read_str(o, "name", r->name, sizeof(r->name), true) &&
           read_repeat(o, &r->repeat) && read_window(o, r) && read_dates(o, r) &&
           read_str(o, "image", r->image, sizeof(r->image), false) &&
           read_str(o, "theme", r->theme, sizeof(r->theme), false) &&
           read_str(o, "anim", r->anim, sizeof(r->anim), false) &&
           read_brightness(o, &r->brightness);
}

/**
 * @brief Parse a schedule.
 * @version 1
 * @req REQ-BG-004
 * @return The number of profiles, or -1.
 */
int sched_from_json(const char *json, sched_rule_t *out, int max)
{
    cJSON *root = json ? cJSON_Parse(json) : NULL;
    const cJSON *ver = cJSON_GetObjectItemCaseSensitive(root, "version");
    const cJSON *arr = cJSON_GetObjectItemCaseSensitive(root, "profiles");
    bool ok = cJSON_IsNumber(ver) && ver->valueint == SCHED_JSON_VERSION && cJSON_IsArray(arr) &&
              cJSON_GetArraySize(arr) <= max;
    int n = 0;
    const cJSON *o;
    const cJSON *list = ok ? arr : (const cJSON *)NULL;
    cJSON_ArrayForEach(o, list) {
        ok = ok && parse_profile(o, &out[n]);
        n++;
    }
    cJSON_Delete(root);
    return ok ? n : -1;
}

/**
 * @brief A window edge as the file writes it.
 * @version 1
 * @utility
 */
static void fmt_edge(const sched_time_t *t, char *out, size_t n)
{
    const char *sun = t->anchor == SCHED_AT_SUNRISE ? "sunrise" : "sunset";
    if (t->anchor == SCHED_AT_CLOCK) snprintf(out, n, "%02d:%02d", t->min / 60, t->min % 60);
    else if (t->min) snprintf(out, n, "%s%+d", sun, t->min);
    else snprintf(out, n, "%s", sun);
}

/**
 * @brief A date as the file writes it ("MM-DD", or "YYYY-MM-DD" once).
 * @version 1
 * @utility
 */
static void fmt_date(const sched_date_t *d, bool once, char *out, size_t n)
{
    if (once) snprintf(out, n, "%04u-%02u-%02u", (unsigned)d->year, d->month, d->day);
    else snprintf(out, n, "%02u-%02u", d->month, d->day);
}

/**
 * @brief Add @p key: @p s to @p o when @p s is not empty.
 * @version 1
 * @utility
 */
static void add_opt(cJSON *o, const char *key, const char *s)
{
    if (s[0]) cJSON_AddStringToObject(o, key, s);
}

/**
 * @brief One profile as an object.
 * @version 1
 * @utility
 * @return The object.
 */
static cJSON *profile_to_json(const sched_rule_t *r)
{
    char a[24], b[24];
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "name", r->name);
    cJSON_AddStringToObject(o, "repeat", REPEAT_NAMES[r->repeat]);
    if (r->repeat != SCHED_EVERY_DAY) {
        fmt_date(&r->from, r->repeat == SCHED_ONCE, a, sizeof(a));
        fmt_date(&r->to, r->repeat == SCHED_ONCE, b, sizeof(b));
        cJSON_AddStringToObject(o, "from", a);
        cJSON_AddStringToObject(o, "to", b);
    }
    if (!r->all_day) {
        fmt_edge(&r->start, a, sizeof(a));
        fmt_edge(&r->end, b, sizeof(b));
        cJSON_AddStringToObject(o, "start", a);
        cJSON_AddStringToObject(o, "end", b);
    }
    add_opt(o, "image", r->image);
    add_opt(o, "theme", r->theme);
    add_opt(o, "anim", r->anim);
    if (r->brightness != SCHED_INHERIT) cJSON_AddNumberToObject(o, "brightness", r->brightness);
    return o;
}

/**
 * @brief Write a schedule.
 * @version 1
 * @req REQ-BG-004
 * @return A heap string, or NULL.
 */
char *sched_to_json(const sched_rule_t *r, int n)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "version", SCHED_JSON_VERSION);
    cJSON *arr = cJSON_AddArrayToObject(root, "profiles");
    for (int i = 0; i < n; i++) cJSON_AddItemToArray(arr, profile_to_json(&r[i]));
    char *out = cJSON_Print(root);
    cJSON_Delete(root);
    return out;
}
