/**
 * @file sky_data.c
 * v2: sky_sun_local (the sun alone, for the schedule's many lookups).
 * v3: the real moon (astro_moon): lit fraction, side, place, rise and set.
 * v4: the sky's events (sky_meteor_zhr, sky_aurora_strength, sky_note).
 * @version 4
 * @brief What the sky screen shows (see sky_data.h): sun times in local minutes, where the
 *        day is, the moon, the aurora outlook and the next dated sky events. Pure C.
 */
#include "sky_data.h"
#include "astro_moon.h"

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
    int         zhr;    /**< A meteor shower's zenithal hourly rate at its peak (0: not one). */
} dated_event_t;

static const dated_event_t SEASONS[] = {
    {  3, 20, "Spring equinox", 0 },
    {  6, 20, "Summer solstice", 0 },
    {  9, 22, "Autumn equinox", 0 },
    { 12, 21, "Winter solstice", 0 },
};

static const dated_event_t SHOWERS[] = {
    {  1,  3, "Quadrantids", 110 },
    {  4, 22, "Lyrids", 18 },
    {  5,  6, "Eta Aquariids", 50 },
    {  7, 30, "Delta Aquariids", 25 },
    {  8, 12, "Perseids", 100 },
    { 10, 21, "Orionids", 20 },
    { 11, 17, "Leonids", 15 },
    { 12, 14, "Geminids", 150 },
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
 * v2: computed with the sky (sky_data_compute), from the real rise and set.
 * @version 2
 * @req REQ-ASTRO-001
 * @return Degrees in (-180, 180].
 */
float sky_moon_angle(const sky_data_t *d)
{
    return d->moon_angle;
}

/**
 * @brief Whole days from local date @p now to the next @p month / @p day (0 when today).
 * @version 1
 * @req REQ-ASTRO-001
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
 * @brief Local sunrise / sunset for @p t's date (-1 both when the sun does not rise).
 * @version 1
 * @utility
 * @return The daylight minutes.
 */
static int sun_local(time_t now, const struct tm *t, float lat, float lon, int *rise, int *set)
{
    sun_times_t sun;
    astro_calc_sun_times(t->tm_year + 1900, t->tm_mon + 1, t->tm_mday, lat, lon, &sun);
    int off = tz_offset_min(now);
    bool rises = sun.sunrise_hour >= 0;
    *rise = rises ? wrap_day(sun.sunrise_hour * 60 + sun.sunrise_min + off) : -1;
    *set = rises ? wrap_day(sun.sunset_hour * 60 + sun.sunset_min + off) : -1;
    return sun.daylight_minutes;
}

/**
 * @brief The sun alone at @p noon's date.
 * @version 1
 * @req REQ-ASTRO-001
 * @req REQ-BG-004
 */
void sky_sun_local(time_t noon, float lat, float lon, int *rise, int *set)
{
    struct tm t;
    localtime_r(&noon, &t);
    sun_local(noon, &t, lat, lon, rise, set);
}

/**
 * @brief Fill @p out's sun fields: local sunrise / sunset, daylight, and where the day is.
 * v2: the times from sun_local (shared with sky_sun_local).
 * @version 2
 * @utility
 */
static void sun_part(time_t now, const struct tm *t, float lat, float lon, sky_data_t *out)
{
    out->daylight_min = sun_local(now, t, lat, lon, &out->sunrise_min, &out->sunset_min);
    bool rises = out->sunrise_min >= 0;
    out->part = SKY_NIGHT_AFTER;
    if (rises && out->now_min < out->sunrise_min) out->part = SKY_NIGHT_BEFORE;
    else if (rises && out->now_min < out->sunset_min) out->part = SKY_DAY;
}

/**
 * @brief Local minute of the day of @p t, or -1 when @p t is 0 (none).
 * @version 1
 * @utility
 * @return Minutes.
 */
static int local_min(time_t t)
{
    struct tm lt;
    if (!t) return -1;
    localtime_r(&t, &lt);
    return lt.tm_hour * 60 + lt.tm_min;
}

/**
 * @brief How far @p now is from @p from to @p to (0..1), mapped onto the wheel from @p start
 *        over half a turn; @p fallback when either end is unknown.
 * @version 1
 * @utility
 * @return Degrees.
 */
static float half_turn(time_t now, time_t from, time_t to, float start, float fallback)
{
    if (!from || !to || to <= from) return fallback;
    return wrap_deg(start + 180.0f * (float)(now - from) / (float)(to - from));
}

/** The moon's horizon crossings around the last moment asked, kept until the next one: they
 *  do not change in between, and the search costs milliseconds. Not shared between tasks
 *  (the display task is the only caller on the clock). */
static struct {
    float  lat, lon;
    time_t last_rise, last_set, next_rise, next_set;
    bool   valid;
} s_moon;

/**
 * @brief The moon's last and next rise and set around @p now (from the cache when no crossing
 *        has happened since it was found).
 * @version 1
 * @utility
 */
static void moon_crossings(time_t now, float lat, float lon)
{
    time_t soonest = s_moon.next_rise && s_moon.next_set ? (s_moon.next_rise < s_moon.next_set ? s_moon.next_rise : s_moon.next_set)
                                                         : (s_moon.next_rise > s_moon.next_set ? s_moon.next_rise : s_moon.next_set);
    time_t latest = (s_moon.last_rise > s_moon.last_set ? s_moon.last_rise : s_moon.last_set);
    bool fresh = s_moon.valid && s_moon.lat == lat && s_moon.lon == lon && now >= latest &&
                 soonest && now < soonest;
    if (fresh) return;
    astro_moon_last(now, lat, lon, &s_moon.last_rise, &s_moon.last_set);
    astro_moon_next(now, lat, lon, &s_moon.next_rise, &s_moon.next_set);
    s_moon.lat = lat;
    s_moon.lon = lon;
    s_moon.valid = true;
}

/**
 * @brief Fill @p out's moon from where it really is: lit fraction and side, up or down, its
 *        place on the wheel (rise to set above, set to rise below), the next rise and set.
 * v2: the crossings from moon_crossings (cached).
 * @version 2
 * @utility
 */
static void moon_part(time_t now, float lat, float lon, sky_data_t *out)
{
    astro_moon_t m;
    astro_moon_at(now, lat, lon, &m);
    out->moon.illumination_pct = m.illum * 100.0f;
    out->waxing = m.waxing;
    out->moon_frac = m.age;
    moon_crossings(now, lat, lon);
    time_t last_rise = s_moon.last_rise, last_set = s_moon.last_set;
    time_t next_rise = s_moon.next_rise, next_set = s_moon.next_set;
    out->moon_up = last_rise > last_set || (!last_set && last_rise);
    out->moon_angle = out->moon_up ? half_turn(now, last_rise, next_set, -90.0f, 0.0f)
                                   : half_turn(now, last_set, next_rise, 90.0f, 180.0f);
    out->moonrise_min = local_min(next_rise);
    out->moonset_min = local_min(next_set);
}

/**
 * @brief Compute the sky at @p now for @p lat / @p lon with Kp @p kp.
 * v2: moon_frac (the month fraction, from the lit fraction and side).
 * v3: the real moon (moon_part): lit fraction, side, place, rise and set.
 * v4: the next shower's ZHR; the night's date (its random sky's seed).
 * @version 4
 * @req REQ-ASTRO-001
 */
void sky_data_compute(time_t now, float lat, float lon, float kp, sky_data_t *out)
{
    memset(out, 0, sizeof(*out));
    struct tm t;
    localtime_r(&now, &t);
    out->now_min = t.tm_hour * 60 + t.tm_min;
    sun_part(now, &t, lat, lon, out);
    astro_calc_moon_phase(t.tm_year + 1900, t.tm_mon + 1, t.tm_mday, &out->moon);   // names, days
    moon_part(now, lat, lon, out);
    out->kp = kp > 0.0f ? kp : 0.0f;
    out->aurora_prob = out->kp > 0.0f ? astro_calc_aurora_probability(kp, lat) : 0.0f;
    out->season = next_event(now, SEASONS, COUNT(SEASONS), &out->season_days);
    out->shower = next_event(now, SHOWERS, COUNT(SHOWERS), &out->shower_days);
    for (size_t i = 0; i < COUNT(SHOWERS); i++) {
        if (SHOWERS[i].name == out->shower) out->shower_zhr = SHOWERS[i].zhr;
    }
    time_t evening = out->part == SKY_NIGHT_BEFORE ? now - 86400 : now;
    struct tm e;
    localtime_r(&evening, &e);
    out->night = (unsigned)((e.tm_year + 1900) * 10000 + (e.tm_mon + 1) * 100 + e.tm_mday);
}

/**
 * @brief Whether @p d's night is its shower's peak night: the evening before the peak date
 *        or the small hours of it.
 * @version 1
 * @utility
 * @return Whether.
 */
static bool peak_tonight(const sky_data_t *d)
{
    return d->shower && ((d->shower_days == 1 && d->part != SKY_NIGHT_BEFORE) ||
                         d->shower_days == 0);
}

/**
 * @brief The meteors to show now.
 * @version 1
 * @req REQ-ASTRO-001
 * @return The shower's ZHR on its peak night in the dark, else 0.
 */
int sky_meteor_zhr(const sky_data_t *d)
{
    return d->part != SKY_DAY && peak_tonight(d) ? d->shower_zhr : 0;
}

/**
 * @brief The aurora to show now.
 * @version 1
 * @req REQ-ASTRO-001
 * @return Its strength (the chance) when dark and at least SKY_AURORA_MIN, else 0.
 */
float sky_aurora_strength(const sky_data_t *d)
{
    return d->part != SKY_DAY && d->aurora_prob >= SKY_AURORA_MIN ? d->aurora_prob : 0.0f;
}

/**
 * @brief The sky card's note.
 * @version 1
 * @req REQ-ASTRO-001
 */
void sky_note(const sky_data_t *d, char *out, size_t n)
{
    if (d->aurora_prob >= SKY_AURORA_MIN) snprintf(out, n, "Aurora possible tonight");
    else if (peak_tonight(d)) snprintf(out, n, "%s peak tonight", d->shower);
    else if (d->shower && d->shower_days == 1) snprintf(out, n, "%s peak tomorrow", d->shower);
    else if (d->shower && d->shower_days <= SKY_NOTE_DAYS) snprintf(out, n, "%s peak in %d days", d->shower, d->shower_days);
    else snprintf(out, n, "%s", d->moon.phase_name);
}
