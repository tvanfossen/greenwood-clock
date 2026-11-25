/**
 * @file astro_calc.h
 * @version 1
 * @brief Astronomical computation (moon phase, sunrise / sunset, aurora odds): pure math, no
 *        LVGL, no network, so the host tests link it. Split out of display_widgets.h.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/** The moon on a date. */
typedef struct {
    float illumination_pct;        // 0.0 - 100.0
    char  phase_name[20];          // "Waxing Crescent", "Full Moon", etc.
    int   phase_index;             // 0-7
    int   days_to_full;
    int   days_to_new;
} moon_phase_t;

/** The sun on a date (UTC; hours -1 for polar night). */
typedef struct {
    int   sunrise_hour, sunrise_min;
    int   sunset_hour, sunset_min;
    int   daylight_minutes;
    float solar_noon_elevation;    // degrees
} sun_times_t;

/**
 * @brief Moon phase for a date (at 00:00 UTC, mean synodic month).
 * @version 1
 * @req REQ-ASTRO-001
 */
void astro_calc_moon_phase(int year, int month, int day, moon_phase_t *out);

/**
 * @brief Sunrise / sunset (UTC) and daylight for a date at @p lat / @p lon.
 * @version 1
 * @req REQ-ASTRO-001
 */
void astro_calc_sun_times(int year, int month, int day, float lat, float lon, sun_times_t *out);

/**
 * @brief Aurora visibility odds (0..1) for Kp @p kp_index at @p latitude.
 * @version 1
 * @req REQ-ASTRO-001
 * @return The probability.
 */
float astro_calc_aurora_probability(float kp_index, float latitude);

#ifdef __cplusplus
}
#endif
