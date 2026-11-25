/**
 * @file astro_moon.c
 * @version 1
 * @brief The moon's place, light, rise and set (see astro_moon.h). Pure C.
 */
#include "astro_moon.h"

#include <math.h>

#define DEG        (3.14159265358979 / 180.0)
#define HORIZON    (-0.8333)      // centre altitude at rise / set: 34' refraction + 15.5' radius
#define STEP_S     (10 * 60)      // search step: the moon moves well under a degree in it
#define SPAN_S     (48 * 3600)    // how far rise / set are looked for
#define REFINE_S   20             // bisection stops within this

/** Where the moon and the sun are in the sky's frame at one moment. */
typedef struct {
    double ra, dec;   /**< Moon, radians. */
    double par;       /**< Moon's horizontal parallax, radians. */
    double elong;     /**< Moon's longitude less the sun's, radians (0 new, pi full). */
    double gmst;      /**< Greenwich sidereal time, radians. */
} sky_t;

/**
 * @brief Sin of @p deg degrees.
 * @version 1
 * @utility
 * @return It.
 */
static double sind(double deg)
{
    return sin(deg * DEG);
}

/**
 * @brief The moon's and the sun's positions at @p t (low-precision series, J2000 epoch).
 * @version 1
 * @utility
 * @return Them.
 */
static sky_t sky_at(time_t t)
{
    double d = (double)t / 86400.0 + 2440587.5 - 2451545.0;   // days from J2000
    double T = d / 36525.0;
    double lon = 218.32 + 481267.881 * T + 6.29 * sind(134.9 + 477198.85 * T) -
                 1.27 * sind(259.2 - 413335.38 * T) + 0.66 * sind(235.7 + 890534.23 * T) +
                 0.21 * sind(269.9 + 954397.70 * T) - 0.19 * sind(357.5 + 35999.05 * T) -
                 0.11 * sind(186.6 + 966404.05 * T);
    double lat = 5.13 * sind(93.3 + 483202.03 * T) + 0.28 * sind(228.2 + 960400.87 * T) -
                 0.28 * sind(318.3 + 6003.18 * T) - 0.17 * sind(217.6 - 407332.20 * T);
    double par = 0.9508 + 0.0518 * cos((134.9 + 477198.85 * T) * DEG) +
                 0.0095 * cos((259.2 - 413335.38 * T) * DEG) +
                 0.0078 * cos((235.7 + 890534.23 * T) * DEG) +
                 0.0028 * cos((269.9 + 954397.70 * T) * DEG);
    double g = 357.529 + 0.98560028 * d;                       // the sun's mean anomaly
    double sun = 280.459 + 0.98564736 * d + 1.915 * sind(g) + 0.020 * sind(2 * g);
    double eps = (23.439 - 0.0000004 * d) * DEG;
    double l = lon * DEG, b = lat * DEG;
    sky_t s;
    s.ra = atan2(sin(l) * cos(eps) - tan(b) * sin(eps), cos(l));
    s.dec = asin(sin(b) * cos(eps) + cos(b) * sin(eps) * sin(l));
    s.par = par * DEG;
    s.elong = fmod((lon - sun) * DEG, 2 * 3.14159265358979);
    s.gmst = fmod(280.46061837 + 360.98564736629 * d, 360.0) * DEG;
    return s;
}

/**
 * @brief The moon's topocentric altitude at @p t, degrees.
 * @version 1
 * @utility
 * @return It.
 */
static double altitude(time_t t, double lat, double lon)
{
    sky_t s = sky_at(t);
    double ha = s.gmst + lon * DEG - s.ra;
    double phi = lat * DEG;
    double geo = asin(sin(phi) * sin(s.dec) + cos(phi) * cos(s.dec) * cos(ha));
    return (geo - s.par * cos(geo)) / DEG;   // the parallax lowers it
}

/**
 * @brief The moon at @p t.
 * @version 1
 * @req REQ-ASTRO-001
 */
void astro_moon_at(time_t t, float lat, float lon, astro_moon_t *out)
{
    sky_t s = sky_at(t);
    double e = s.elong < 0 ? s.elong + 2 * 3.14159265358979 : s.elong;
    out->alt_deg = (float)altitude(t, lat, lon);
    out->illum = (float)((1.0 - cos(e)) / 2.0);
    out->waxing = e < 3.14159265358979;
    out->age = (float)(e / (2 * 3.14159265358979));
}

/**
 * @brief The crossing of the rise / set altitude between @p a (below when @p rising) and
 *        @p b, by bisection.
 * @version 1
 * @utility
 * @return Its time.
 */
static time_t refine(time_t a, time_t b, double lat, double lon, bool rising)
{
    while (b - a > REFINE_S) {
        time_t m = a + (b - a) / 2;
        bool above = altitude(m, lat, lon) > HORIZON;
        if (above == rising) b = m;
        else a = m;
    }
    return a + (b - a) / 2;
}

/**
 * @brief A horizon crossing met in the step ending at @p at (walking in @p dir; above the
 *        horizon at @p at when @p now_up): refine it into @p rise or @p set unless that one
 *        is found already.
 * @version 1
 * @utility
 */
static void note_crossing(time_t at, int dir, bool now_up, double lat, double lon, time_t *rise,
                          time_t *set)
{
    time_t early = dir > 0 ? at - STEP_S : at;   // the step's earlier end
    bool rising = dir > 0 ? now_up : !now_up;    // below to above, in time's direction
    time_t *slot = rising ? rise : set;
    if (!*slot) *slot = refine(early, early + STEP_S, lat, lon, rising);
}

/**
 * @brief Walk from @p t in steps of @p dir * STEP_S for up to SPAN_S, noting the first rise
 *        and the first set met.
 * @version 1
 * @utility
 */
static void walk(time_t t, double lat, double lon, int dir, time_t *rise, time_t *set)
{
    *rise = *set = 0;
    bool up = altitude(t, lat, lon) > HORIZON;
    for (time_t step = STEP_S; step <= SPAN_S && !(*rise && *set); step += STEP_S) {
        time_t at = t + dir * step;
        bool now_up = altitude(at, lat, lon) > HORIZON;
        if (now_up != up) note_crossing(at, dir, now_up, lat, lon, rise, set);
        up = now_up;
    }
}

/**
 * @brief The next rise and set.
 * @version 1
 * @req REQ-ASTRO-001
 */
void astro_moon_next(time_t t, float lat, float lon, time_t *rise, time_t *set)
{
    walk(t, lat, lon, 1, rise, set);
}

/**
 * @brief The last rise and set.
 * @version 1
 * @req REQ-ASTRO-001
 */
void astro_moon_last(time_t t, float lat, float lon, time_t *rise, time_t *set)
{
    walk(t, lat, lon, -1, rise, set);
}
