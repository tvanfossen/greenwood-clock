/**
 * @file astro_moon.h
 * @version 1
 * @brief The moon from where the clock stands: its altitude, lit fraction and side (waxing:
 *        lit on the right, as seen from the northern hemisphere), and its rise and set times.
 *        The Astronomical Almanac's low-precision lunar series (about 0.3 degrees) with the
 *        parallax applied; rise and set are the upper limb on a horizon lowered 34' by
 *        refraction. Pure C, UTC epoch seconds in and out.
 */
#pragma once

#include <stdbool.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/** The moon at one moment. */
typedef struct {
    float alt_deg;   /**< Altitude of its centre, degrees (topocentric, no refraction). */
    float illum;     /**< Lit fraction of the disc, 0..1. */
    bool  waxing;    /**< Growing towards full (lit on its west side, towards the sun). */
    float age;       /**< Fraction of the month since new, 0..1 (0.5 full). */
} astro_moon_t;

/**
 * @brief The moon at @p t seen from @p lat / @p lon (degrees, east positive).
 * @version 1
 * @req REQ-ASTRO-001
 */
void astro_moon_at(time_t t, float lat, float lon, astro_moon_t *out);

/**
 * @brief The first moonrise and moonset after @p t (each 0 when there is none within two
 *        days, as far north as it can stay up or down).
 * @version 1
 * @req REQ-ASTRO-001
 */
void astro_moon_next(time_t t, float lat, float lon, time_t *rise, time_t *set);

/**
 * @brief The last moonrise and moonset at or before @p t (each 0 when none within two days).
 * @version 1
 * @req REQ-ASTRO-001
 */
void astro_moon_last(time_t t, float lat, float lon, time_t *rise, time_t *set);

#ifdef __cplusplus
}
#endif
