/**
 * @file sched_rule.c
 * v2: sched_rule_fill, sched_rule_gap.
 * v3: sched_theme_stem.
 * @version 3
 * @brief Schedule profiles as text (see sched_rule.h). Pure C.
 */
#include "sched_rule.h"
#include "sky_data.h"   // sky_fmt_clock

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const MONTHS[12] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };

/**
 * @brief A month's short name.
 * @version 1
 * @utility
 * @return "Jan".."Dec", or "?" when out of range.
 */
static const char *month_name(int month)
{
    return month >= 1 && month <= 12 ? MONTHS[month - 1] : "?";
}

/**
 * @brief Format a sun offset: "" (none), " + 1h", " - 30m", " + 1h 15m".
 * @version 1
 * @utility
 */
static void fmt_offset(int off, char *out, size_t n)
{
    int a = abs(off);
    const char *sign = off < 0 ? " - " : " + ";
    if (off == 0) out[0] = '\0';
    else if (a % 60 == 0) snprintf(out, n, "%s%dh", sign, a / 60);
    else if (a < 60) snprintf(out, n, "%s%dm", sign, a);
    else snprintf(out, n, "%s%dh %dm", sign, a / 60, a % 60);
}

/**
 * @brief A window edge as text.
 * @version 1
 * @req REQ-BG-004
 */
void sched_fmt_time(const sched_time_t *t, char *out, size_t n)
{
    char off[16];
    if (t->anchor == SCHED_AT_CLOCK) {
        sky_fmt_clock(t->min, out, n);
    } else {
        fmt_offset(t->min, off, sizeof(off));
        snprintf(out, n, "%s%s", t->anchor == SCHED_AT_SUNRISE ? "Sunrise" : "Sunset", off);
    }
}

/**
 * @brief The dates of a profile.
 * @version 1
 * @req REQ-BG-004
 */
void sched_fmt_dates(const sched_rule_t *r, char *out, size_t n)
{
    bool one = r->from.month == r->to.month && r->from.day == r->to.day;
    char year[8] = "";
    if (r->repeat == SCHED_ONCE) snprintf(year, sizeof(year), ", %u", (unsigned)r->to.year);
    if (r->repeat == SCHED_EVERY_DAY) out[0] = '\0';
    else if (one) snprintf(out, n, "%s %u%s", month_name(r->from.month), r->from.day, year);
    else
        snprintf(out, n, "%s %u - %s %u%s", month_name(r->from.month), r->from.day,
                 month_name(r->to.month), r->to.day, year);
}

/**
 * @brief When a profile runs, as one line.
 * v2: a daily profile is just its window.
 * @version 2
 * @req REQ-BG-004
 */
void sched_fmt_when(const sched_rule_t *r, char *out, size_t n)
{
    static const char *const REPEAT[] = { "Every day", "Every year", "Once" };
    char dates[40], window[48], a[20], b[20];
    sched_fmt_dates(r, dates, sizeof(dates));
    if (r->all_day) {
        snprintf(window, sizeof(window), "all day");
    } else {
        sched_fmt_time(&r->start, a, sizeof(a));
        sched_fmt_time(&r->end, b, sizeof(b));
        snprintf(window, sizeof(window), "%s - %s", a, b);
    }
    if (r->repeat == SCHED_EVERY_DAY) snprintf(out, n, "%s", window);
    else snprintf(out, n, "%s, %s, %s", REPEAT[r->repeat], dates, window);
}

/**
 * @brief The file name of an LVGL path without folder or extension.
 * @version 1
 * @req REQ-BG-004
 */
void sched_image_label(const char *path, char *out, size_t n)
{
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    snprintf(out, n, "%s", base);
    char *dot = strrchr(out, '.');
    if (dot) *dot = '\0';
}

/**
 * @brief A theme's display label from its file stem.
 * @version 1
 * @req REQ-BG-004
 */
void sched_theme_label(const char *stem, char *out, size_t n)
{
    snprintf(out, n, "%s", stem);
    out[0] = (char)toupper((unsigned char)out[0]);
}

/**
 * @brief Append ", @p part" to @p out (no separator when @p out is empty).
 * @version 1
 * @utility
 */
static void append(char *out, size_t n, const char *part)
{
    size_t len = strlen(out);
    if (len < n) snprintf(out + len, n - len, "%s%s", len ? ", " : "", part);
}

/**
 * @brief What a profile shows, as one line.
 * @version 1
 * @req REQ-BG-004
 */
void sched_fmt_look(const sched_rule_t *r, char *out, size_t n)
{
    char part[SCHED_IMAGE_LEN];
    out[0] = '\0';
    if (r->image[0]) {
        sched_image_label(r->image, part, sizeof(part));
        append(out, n, part);
    }
    if (r->theme[0]) {
        sched_theme_label(r->theme, part, sizeof(part));
        append(out, n, part);
    }
    if (r->anim[0]) append(out, n, r->anim);
    if (r->brightness != SCHED_INHERIT) {
        snprintf(part, sizeof(part), "%d%%", r->brightness);
        append(out, n, part);
    }
    if (!out[0]) snprintf(out, n, "Global settings");
}

/**
 * @brief Copy @p from into @p to (of @p n bytes) when @p to is empty.
 * @version 1
 * @utility
 * @return Whether it was copied.
 */
static bool fill_text(char *to, size_t n, const char *from)
{
    bool fill = !to[0] && from[0];
    if (fill) snprintf(to, n, "%s", from);
    return fill;
}

/**
 * @brief Fill @p r's unset fields from @p with.
 * @version 1
 * @req REQ-BG-004
 * @return Whether any was filled.
 */
bool sched_rule_fill(sched_rule_t *r, const sched_rule_t *with)
{
    bool changed = fill_text(r->image, sizeof(r->image), with->image);
    changed |= fill_text(r->theme, sizeof(r->theme), with->theme);
    changed |= fill_text(r->anim, sizeof(r->anim), with->anim);
    if (r->brightness == SCHED_INHERIT && with->brightness != SCHED_INHERIT) {
        r->brightness = with->brightness;
        changed = true;
    }
    return changed;
}

/**
 * @brief What a gap shows.
 * @version 1
 * @req REQ-BG-004
 */
void sched_rule_gap(sched_rule_t *out)
{
    memset(out, 0, sizeof(*out));
    snprintf(out->theme, sizeof(out->theme), "default");
    snprintf(out->anim, sizeof(out->anim), "None");
    out->brightness = SCHED_INHERIT;
}

/**
 * @brief A theme name's file stem.
 * @version 1
 * @req REQ-UI-004
 * @return Whether it is usable.
 */
bool sched_theme_stem(const char *name, char *out, size_t n)
{
    size_t len = 0, cap = n < SCHED_THEME_LEN ? n - 1 : SCHED_THEME_LEN - 1;
    for (const char *c = name; *c && len < cap; c++) {
        char lc = (char)tolower((unsigned char)*c);
        bool keep = isalnum((unsigned char)lc) && (unsigned char)lc < 0x80;
        if (keep) out[len++] = lc;
        else if (len && out[len - 1] != '-') out[len++] = '-';
    }
    while (len && out[len - 1] == '-') len--;
    out[len] = '\0';
    return len > 0 && strcmp(out, "default") != 0;
}
