/**
 * @file sky_data.c
 * @version 1
 * @brief What the sky screen shows (see sky_data.h): sun times in local minutes, where the
 *        day is, the moon, the aurora outlook and the next dated sky events. Pure C.
 */
#include "sky_data.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/** A yearly sky event on a fixed (approximate) date. */
typedef struct {
    int         month;
    int         day;
    const char *name;
} dated_event_t;

static const dated_event_t SEASONS[] = {
    {  3, 20, "Spring equinox" },
    {  6, 20, "Summer solstice" },
    {  9, 22, "Autumn equinox" },
    { 12, 21, "Winter solstice" },
};

static const dated_event_t SHOWERS[] = {
    {  1,  3, "Quadrantids" },
    {  4, 22, "Lyrids" },
    {  5,  6, "Eta Aquariids" },
    {  7, 30, "Delta Aquariids" },
    {  8, 12, "Perseids" },
    { 10, 21, "Orionids" },
    { 11, 17, "Leonids" },
    { 12, 14, "Geminids" },
};

#define COUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))

/**
 * @brief Local-minus-UTC offset at @p now in minutes (no tm_gmtoff on newlib).
 * @version 1
 * @utility
 * @return Minutes, negative west of UTC.
 */
static int tz_offset_min(time_t now)
{
    struct tm l, u;
    localtime_r(&now, &l);
    gmtime_r(&now, &u);
    int days = l.tm_yday - u.tm_yday;
    if (days > 1) days = -1;        // year wrap: local Jan 1, UTC Dec 31 ... and back
    else if (days < -1) days = 1;
    return days * 1440 + (l.tm_hour - u.tm_hour) * 60 + (l.tm_min - u.tm_min);
}

/**
 * @brief Wrap minutes into one day.
 * @version 1
 * @utility
 * @return 0..1439.
 */
static int wrap_day(int m)
{
    m %= 1440;
    return m < 0 ? m + 1440 : m;
}

/**
 * @brief Format local minute-of-day @p m as "7:03 AM".
 * @version 1
 * @req REQ-ASTRO-001
 */
void sky_fmt_clock(int m, char *out, size_t n)
{
    int h = m / 60;
    snprintf(out, n, "%d:%02d %s", h % 12 ? h % 12 : 12, m % 60, h < 12 ? "AM" : "PM");
}

/**
 * @brief Wrap @p deg into (-180, 180].
 * @version 1
 * @utility
 * @return Degrees.
 */
static float wrap_deg(float deg)
{
    deg = fmodf(deg, 360.0f);
    if (deg > 180.0f) deg -= 360.0f;
    if (deg <= -180.0f) deg += 360.0f;
    return deg;
}

/**
 * @brief The sun's place on the sky wheel (see sky_data.h).
 * @version 1
 * @req REQ-ASTRO-001
 * @return Degrees in (-180, 180].
 */
float sky_sun_angle(const sky_data_t *d)
{
    int day = d->sunset_min - d->sunrise_min;
    if (d->sunrise_min < 0 || day <= 0) return 180.0f;
    if (d->part == SKY_DAY) return -90.0f + 180.0f * (float)(d->now_min - d->sunrise_min) / day;
    int since = (d->now_min - d->sunset_min + 1440) % 1440;   // minutes since sunset
    return wrap_deg(90.0f + 180.0f * (float)since / (float)(1440 - day));
}

/**
 * @brief The moon's place on the sky wheel (see sky_data.h).
 * @version 1
 * @req REQ-ASTRO-001
 * @return Degrees in (-180, 180].
 */
float sky_moon_angle(const sky_data_t *d)
{
    int noon = d->sunrise_min >= 0 ? (d->sunrise_min + d->sunset_min) / 2 : 720;
    float sun = 360.0f * (float)(d->now_min - noon) / 1440.0f;
    return wrap_deg(sun - 360.0f * d->moon_frac);
}

/**
 * @brief Whole days from local date @p now to the next @p month / @p day (0 when today).
 * @version 1
 * @req REQ-AMB-001
 * @return Days, 0..365.
 */
int sky_days_until(time_t now, int month, int day)
{
    struct tm t;
    localtime_r(&now, &t);
    struct tm today = { .tm_year = t.tm_year, .tm_mon = t.tm_mon, .tm_mday = t.tm_mday,
                        .tm_hour = 12, .tm_isdst = -1 };
    struct tm ev = today;
    ev.tm_mon = month - 1;
    ev.tm_mday = day;
    time_t a = mktime(&today), b = mktime(&ev);
    if (b < a) {   // passed this year: next year's (leap years counted by mktime)
        ev = (struct tm){ .tm_year = t.tm_year + 1, .tm_mon = month - 1, .tm_mday = day,
                          .tm_hour = 12, .tm_isdst = -1 };
        b = mktime(&ev);
    }
    return (int)((b - a + 43200) / 86400);   // noon to noon: DST hours round away
}

/**
 * @brief The soonest of @p n events after @p now.
 * @version 1
 * @utility
 * @return Its name, with @p days_out set.
 */
static const char *next_event(time_t now, const dated_event_t *ev, int n, int *days_out)
{
    const char *best = NULL;
    int best_days = 400;
    for (int i = 0; i < n; i++) {
        int d = sky_days_until(now, ev[i].month, ev[i].day);
        if (d < best_days) {
            best_days = d;
            best = ev[i].name;
        }
    }
    *days_out = best_days;
    return best;
}

/**
 * @brief Fill @p out's sun fields: local sunrise / sunset, daylight, and where the day is.
 * @version 1
 * @utility
 */
static void sun_part(time_t now, const struct tm *t, float lat, float lon, sky_data_t *out)
{
    sun_times_t sun;
    astro_calc_sun_times(t->tm_year + 1900, t->tm_mon + 1, t->tm_mday, lat, lon, &sun);
    int off = tz_offset_min(now);
    bool rises = sun.sunrise_hour >= 0;
    out->sunrise_min = rises ? wrap_day(sun.sunrise_hour * 60 + sun.sunrise_min + off) : -1;
    out->sunset_min = rises ? wrap_day(sun.sunset_hour * 60 + sun.sunset_min + off) : -1;
    out->daylight_min = sun.daylight_minutes;
    out->part = SKY_NIGHT_AFTER;
    if (rises && out->now_min < out->sunrise_min) out->part = SKY_NIGHT_BEFORE;
    else if (rises && out->now_min < out->sunset_min) out->part = SKY_DAY;
}

/**
 * @brief Compute the sky at @p now for @p lat / @p lon with Kp @p kp.
 * v2: moon_frac (the month fraction, from the lit fraction and side).
 * @version 2
 * @req REQ-ASTRO-001
 * @req REQ-AMB-001
 */
void sky_data_compute(time_t now, float lat, float lon, float kp, sky_data_t *out)
{
    memset(out, 0, sizeof(*out));
    struct tm t;
    localtime_r(&now, &t);
    out->now_min = t.tm_hour * 60 + t.tm_min;
    sun_part(now, &t, lat, lon, out);
    astro_calc_moon_phase(t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, &out->moon);
    out->waxing = out->moon.days_to_full < out->moon.days_to_new;
    // Back from the lit fraction: f = (1 - cos 2 pi x) / 2, waxing x in [0, .5].
    float f = out->moon.illumination_pct / 100.0f;
    float x = acosf(fminf(fmaxf(1.0f - 2.0f * f, -1.0f), 1.0f)) / (2.0f * (float)M_PI);
    out->moon_frac = out->waxing ? x : 1.0f - x;
    out->kp = kp > 0.0f ? kp : 0.0f;
    out->aurora_prob = out->kp > 0.0f ? astro_calc_aurora_probability(kp, lat) : 0.0f;
    out->season = next_event(now, SEASONS, COUNT(SEASONS), &out->season_days);
    out->shower = next_event(now, SHOWERS, COUNT(SHOWERS), &out->shower_days);
}
