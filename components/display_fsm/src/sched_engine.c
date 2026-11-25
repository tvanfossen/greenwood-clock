/**
 * @file sched_engine.c
 * v2: sched_days_until.
 * @version 2
 * @brief The schedule engine (see sched_engine.h): profile coverage per local day as minute
 *        spans, the active profile, and the overlap check. Pure C.
 */
#include "sched_engine.h"

#include <stdio.h>

#define DAY_MIN 1440

/** A half-open span of minutes within one day. */
typedef struct {
    int a;
    int b;
} span_t;

// ---------------------------------------------------------------------------
// Calendar
// ---------------------------------------------------------------------------

/**
 * @brief Days in month @p m of year @p y (Gregorian).
 * @version 1
 * @utility
 * @return 28..31.
 */
static int days_in_month(int y, int m)
{
    static const int DAYS[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    bool leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
    return m == 2 && leap ? 29 : DAYS[m - 1];
}

/**
 * @brief The day after @p d.
 * @version 1
 * @utility
 * @return The date.
 */
static sched_date_t next_day(sched_date_t d)
{
    if (d.day < days_in_month(d.year, d.month)) {
        d.day++;
    } else {
        d.day = 1;
        d.month = d.month == 12 ? 1 : d.month + 1;
        d.year = d.month == 1 ? d.year + 1 : d.year;
    }
    return d;
}

/**
 * @brief The day before @p d.
 * @version 1
 * @utility
 * @return The date.
 */
static sched_date_t prev_day(sched_date_t d)
{
    if (d.day > 1) {
        d.day--;
    } else {
        d.month = d.month == 1 ? 12 : d.month - 1;
        d.year = d.month == 12 ? d.year - 1 : d.year;
        d.day = (uint8_t)days_in_month(d.year, d.month);
    }
    return d;
}

/**
 * @brief A sortable key of month and day (year ignored).
 * @version 1
 * @utility
 * @return month * 32 + day.
 */
static int md_key(sched_date_t d)
{
    return d.month * 32 + d.day;
}

/**
 * @brief Whether profile @p r runs on date @p d (every day; within its yearly range, which may
 *        wrap past the new year; or within its one-off range).
 * @version 1
 * @utility
 * @return True when it does.
 */
static bool on_date(const sched_rule_t *r, sched_date_t d)
{
    bool in = true;
    if (r->repeat == SCHED_YEARLY) {
        int a = md_key(r->from), b = md_key(r->to), x = md_key(d);
        in = a <= b ? (x >= a && x <= b) : (x >= a || x <= b);
    } else if (r->repeat == SCHED_ONCE) {
        long a = r->from.year * 512L + md_key(r->from), b = r->to.year * 512L + md_key(r->to);
        long x = d.year * 512L + md_key(d);
        in = x >= a && x <= b;
    }
    return in;
}

// ---------------------------------------------------------------------------
// Coverage
// ---------------------------------------------------------------------------

/**
 * @brief A window edge in minutes on a day with sun @p s, clamped to the day.
 * @version 1
 * @utility
 * @return 0..DAY_MIN.
 */
static int edge(const sched_time_t *t, sched_sun_t s)
{
    int m = t->min;
    if (t->anchor == SCHED_AT_SUNRISE) m = s.rise_min + t->min;
    else if (t->anchor == SCHED_AT_SUNSET) m = s.set_min + t->min;
    return m < 0 ? 0 : (m > DAY_MIN ? DAY_MIN : m);
}

/**
 * @brief What @p r covers on day @p d: the part of its window that starts on @p d (when it
 *        runs on @p d) and the part spilling past midnight from the day before.
 * @version 1
 * @utility
 * @return Number of spans written to @p out (0..2).
 */
static int day_cover(const sched_rule_t *r, sched_date_t d, const sched_sun_src_t *sun,
                     span_t out[2])
{
    int n = 0;
    if (on_date(r, d)) {
        sched_sun_t s = sun->fn(d, sun->ctx);
        int a = edge(&r->start, s), b = edge(&r->end, s);
        if (r->all_day) out[n++] = (span_t){ 0, DAY_MIN };
        else if (a != b) out[n++] = (span_t){ a, a < b ? b : DAY_MIN };
    }
    sched_date_t y = prev_day(d);
    if (!r->all_day && on_date(r, y)) {
        sched_sun_t s = sun->fn(y, sun->ctx);
        int a = edge(&r->start, s), b = edge(&r->end, s);
        if (a > b && b > 0) out[n++] = (span_t){ 0, b };
    }
    return n;
}

/**
 * @brief Whether @p r covers minute @p min of day @p d.
 * @version 1
 * @utility
 * @return True when it does.
 */
static bool covers(const sched_rule_t *r, sched_date_t d, int min, const sched_sun_src_t *sun)
{
    span_t sp[2];
    int n = day_cover(r, d, sun, sp);
    bool in = false;
    for (int i = 0; i < n; i++) in = in || (min >= sp[i].a && min < sp[i].b);
    return in;
}

/**
 * @brief The profile showing at a date and minute.
 * @version 1
 * @req REQ-BG-004
 * @return Its index, or -1.
 */
int sched_active(const sched_rule_t *r, int n, sched_date_t day, int min,
                 const sched_sun_src_t *sun)
{
    int found = -1;
    for (int pass = 0; pass < 2 && found < 0; pass++) {   // events first, then daily
        for (int i = 0; i < n && found < 0; i++) {
            bool daily = r[i].repeat == SCHED_EVERY_DAY;
            if (daily == (pass == 1) && covers(&r[i], day, min, sun)) found = i;
        }
    }
    return found;
}

// ---------------------------------------------------------------------------
// Overlaps
// ---------------------------------------------------------------------------

/**
 * @brief The first minutes @p a and @p b share on day @p d.
 * @version 1
 * @utility
 * @return Whether they share any (@p out set then).
 */
static bool shared(const sched_rule_t *a, const sched_rule_t *b, sched_date_t d,
                   const sched_sun_src_t *sun, span_t *out)
{
    span_t sa[2], sb[2];
    int na = day_cover(a, d, sun, sa), nb = day_cover(b, d, sun, sb);
    bool hit = false;
    for (int i = 0; i < na && !hit; i++) {
        for (int j = 0; j < nb && !hit; j++) {
            int lo = sa[i].a > sb[j].a ? sa[i].a : sb[j].a;
            int hi = sa[i].b < sb[j].b ? sa[i].b : sb[j].b;
            hit = lo < hi;
            if (hit) *out = (span_t){ lo, hi };
        }
    }
    return hit;
}

/**
 * @brief The first profile of the candidate's kind (or only @p want when >= 0) it shares
 *        minutes with on day @p d.
 * @version 1
 * @utility
 * @return Its index, or -1 (@p sp set when found).
 */
static int hit_on_day(const sched_rule_t *r, int n, int cand, int want, sched_date_t d,
                      const sched_sun_src_t *sun, span_t *sp)
{
    bool daily = r[cand].repeat == SCHED_EVERY_DAY;
    int found = -1;
    for (int j = 0; j < n && found < 0; j++) {
        bool same_kind = (r[j].repeat == SCHED_EVERY_DAY) == daily;
        if (j != cand && same_kind && (want < 0 || want == j) && shared(&r[cand], &r[j], d, sun, sp))
            found = j;
    }
    return found;
}

/**
 * @brief The first collision of a candidate with another profile of its kind.
 * @version 1
 * @req REQ-BG-004
 * @return Whether there is one.
 */
bool sched_find_overlap(const sched_rule_t *r, int n, int cand, sched_date_t from, int days,
                        const sched_sun_src_t *sun, sched_overlap_t *out)
{
    bool found = false, done = false;
    int hits = 0;
    sched_date_t d = from;
    for (int k = 0; k < days && !done; k++, d = next_day(d)) {
        span_t sp = { 0, 0 };
        int j = hit_on_day(r, n, cand, found ? out->other : -1, d, sun, &sp);
        if (j >= 0 && !found) {
            *out = (sched_overlap_t){ j, d, d, sp.a, sp.b, false };
            found = true;
        }
        if (j >= 0) {
            out->last = d;
            hits++;
        } else {
            done = found;   // the first run of days ends at the first day without it
        }
    }
    if (found) out->every_day = hits == days;
    return found;
}

#define DAYS_AHEAD 800   // far enough for a one-off next year

/**
 * @brief Days until a profile next runs.
 * @version 1
 * @req REQ-BG-004
 * @return Days, or -1.
 */
int sched_days_until(const sched_rule_t *r, sched_date_t today)
{
    int found = -1;
    sched_date_t d = today;
    for (int k = 0; k < DAYS_AHEAD && found < 0; k++, d = next_day(d)) {
        if (on_date(r, d)) found = k;
    }
    return found;
}

/**
 * @brief The editor's reason for refusing a save.
 * @version 1
 * @req REQ-BG-004
 */
void sched_fmt_overlap(const sched_rule_t *r, const sched_overlap_t *o, char *out, size_t n)
{
    sched_rule_t span = { .repeat = SCHED_YEARLY, .from = o->first, .to = o->last };
    char dates[40], a[16], b[16];
    sched_fmt_dates(&span, dates, sizeof(dates));
    const char *name = r[o->other].name;
    if (r[o->other].repeat != SCHED_EVERY_DAY) {
        snprintf(out, n, "Overlaps %s on %s", name, dates);
    } else {
        sched_time_t ta = { SCHED_AT_CLOCK, (int16_t)o->from_min };
        sched_time_t tb = { SCHED_AT_CLOCK, (int16_t)o->to_min };
        sched_fmt_time(&ta, a, sizeof(a));
        sched_fmt_time(&tb, b, sizeof(b));
        snprintf(out, n, "Overlaps %s from %s to %s%s%s", name, a, b, o->every_day ? "" : ", ",
                 o->every_day ? "" : dates);
    }
}
