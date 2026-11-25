/**
 * @file sky_data.h
 * @version 1
 * @brief What the sky screen shows (sky_panel.h), for one moment at one place: local sun
 *        times and where the day is, the moon, the aurora outlook and the next dated sky
 *        events. Pure C (no LVGL): host-tested.
 */
#pragma once

#include "astro_calc.h"

#include <stdbool.h>
#include <stddef.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Where the day is: before sunrise, between sunrise and sunset, after sunset. */
typedef enum {
    SKY_NIGHT_BEFORE = 0,
    SKY_DAY,
    SKY_NIGHT_AFTER,
} sky_part_t;

/** Everything the sky screen shows, for one moment at one place. Minutes are local
 *  minutes of the day; sunrise_min < 0 when the sun does not rise (polar night). */
typedef struct {
    int          now_min;
    int          sunrise_min;
    int          sunset_min;
    int          daylight_min;
    sky_part_t   part;
    moon_phase_t moon;
    bool         waxing;
    float        moon_frac;      /* where in its month: 0 new, 0.5 full */
    float        kp;             /* 0 when unknown */
    float        aurora_prob;    /* 0..1 */
    const char  *season;         /* next solstice / equinox */
    int          season_days;
    const char  *shower;         /* next meteor shower peak */
    int          shower_days;
} sky_data_t;

/**
 * @brief Compute the sky at @p now (local time from TZ) for @p lat / @p lon, with the
 *        current Kp index @p kp (0 when unknown).
 * @version 1
 * @req REQ-ASTRO-001
 * @req REQ-AMB-001
 */
void sky_data_compute(time_t now, float lat, float lon, float kp, sky_data_t *out);

/**
 * @brief Only the sun, which is cheap (no moon or events): local sunrise and sunset minutes on
 *        @p noon's date at @p lat / @p lon, both -1 when the sun does not rise.
 * @version 1
 * @req REQ-ASTRO-001
 * @req REQ-BG-004
 */
void sky_sun_local(time_t noon, float lat, float lon, int *rise, int *set);

/**
 * @brief Whole days from local date @p now to the next @p month / @p day (0 when today).
 * @version 1
 * @req REQ-AMB-001
 * @return Days, 0..365.
 */
int sky_days_until(time_t now, int month, int day);

/**
 * @brief Format local minute-of-day @p m as "7:03 AM".
 * @version 1
 * @req REQ-ASTRO-001
 */
void sky_fmt_clock(int m, char *out, size_t n);

/**
 * @brief The sun's place on the sky wheel, in degrees from the top (clockwise): -90 at
 *        sunrise (on the horizon, left), 0 midway through the day, +90 at sunset, 180 midway
 *        through the night. Day and night each take half the wheel, so the sun crosses the
 *        horizon exactly at sunrise and sunset.
 * @version 1
 * @req REQ-ASTRO-001
 * @return Degrees in (-180, 180]; 180 in polar night.
 */
float sky_sun_angle(const sky_data_t *d);

/**
 * @brief The moon's place on the sky wheel (degrees, as sky_sun_angle): it trails the sun by
 *        its month fraction of a turn (new: with the sun, full: opposite), on a 24 h wheel
 *        turning about the middle of the day. Within about an hour of the real moonrise.
 * @version 1
 * @req REQ-ASTRO-001
 * @return Degrees in (-180, 180].
 */
float sky_moon_angle(const sky_data_t *d);

#ifdef __cplusplus
}
#endif
