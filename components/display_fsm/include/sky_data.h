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
 * @brief Whole days from local date @p now to the next @p month / @p day (0 when today).
 * @version 1
 * @req REQ-AMB-001
 * @return Days, 0..365.
 */
int sky_days_until(time_t now, int month, int day);

#ifdef __cplusplus
}
#endif
