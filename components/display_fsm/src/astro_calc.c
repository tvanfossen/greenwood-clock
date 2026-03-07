// components/display_fsm/src/astro_calc.c
//
// Layer 1: Astronomical computation — moon phase, sunrise/sunset, aurora.
// Pure math — no LVGL, no network. Based on Jean Meeus "Astronomical Algorithms".

#include "display_widgets.h"
#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define DEG_TO_RAD(d) ((d) * M_PI / 180.0)
#define RAD_TO_DEG(r) ((r) * 180.0 / M_PI)

// ============================================================================
// Julian Date helpers
// ============================================================================

static double julian_date(int year, int month, int day)
{
    if (month <= 2) {
        year -= 1;
        month += 12;
    }
    int A = year / 100;
    int B = 2 - A + A / 4;
    return (int)(365.25 * (year + 4716)) + (int)(30.6001 * (month + 1)) + day + B - 1524.5;
}

// ============================================================================
// Moon phase (Meeus Ch. 49 simplified)
// ============================================================================

void astro_calc_moon_phase(int year, int month, int day, moon_phase_t *out)
{
    if (!out) return;
    memset(out, 0, sizeof(*out));

    double jd = julian_date(year, month, day);

    // Days since known new moon (Jan 6, 2000 18:14 UTC = JD 2451550.1)
    double days_since = jd - 2451550.1;

    // Synodic month = 29.53058770576 days
    double synodic = 29.53058770576;
    double cycles = days_since / synodic;
    double phase_frac = cycles - (int)cycles;
    if (phase_frac < 0) phase_frac += 1.0;

    // Illumination: approximate using cosine of phase angle
    // Phase angle = phase_frac * 2 * PI
    out->illumination_pct = (float)((1.0 - cos(phase_frac * 2.0 * M_PI)) / 2.0 * 100.0);

    // Phase index (0-7) and name
    // 0=New, 1=Waxing Crescent, 2=First Quarter, 3=Waxing Gibbous,
    // 4=Full, 5=Waning Gibbous, 6=Last Quarter, 7=Waning Crescent
    int idx = (int)(phase_frac * 8.0 + 0.5) % 8;
    out->phase_index = idx;

    static const char *names[] = {
        "New Moon", "Waxing Crescent", "First Quarter", "Waxing Gibbous",
        "Full Moon", "Waning Gibbous", "Last Quarter", "Waning Crescent"
    };
    strncpy(out->phase_name, names[idx], sizeof(out->phase_name) - 1);

    // Days to next full moon (phase_frac ~0.5)
    double to_full = 0.5 - phase_frac;
    if (to_full < 0) to_full += 1.0;
    out->days_to_full = (int)(to_full * synodic + 0.5);

    // Days to next new moon (phase_frac ~0.0)
    double to_new = 1.0 - phase_frac;
    if (to_new >= 1.0) to_new -= 1.0;
    out->days_to_new = (int)(to_new * synodic + 0.5);
}

// ============================================================================
// Sunrise/sunset (simplified solar position)
// ============================================================================

void astro_calc_sun_times(int year, int month, int day, float lat, float lon,
                          sun_times_t *out)
{
    if (!out) return;
    memset(out, 0, sizeof(*out));

    // Day of year
    int N1 = (int)(275.0 * month / 9.0);
    int N2 = (int)((month + 9.0) / 12.0);
    int N3 = 1 + (int)((year - 4 * (int)(year / 4.0) + 2) / 3.0);
    int N = N1 - N2 * N3 + day - 30;

    // Solar declination (simplified)
    double gamma = 2.0 * M_PI / 365.0 * (N - 1);
    double decl = 0.006918 - 0.399912 * cos(gamma) + 0.070257 * sin(gamma)
                - 0.006758 * cos(2.0 * gamma) + 0.000907 * sin(2.0 * gamma)
                - 0.002697 * cos(3.0 * gamma) + 0.00148 * sin(3.0 * gamma);

    // Equation of time (minutes)
    double eqtime = 229.18 * (0.000075 + 0.001868 * cos(gamma) - 0.032077 * sin(gamma)
                              - 0.014615 * cos(2.0 * gamma) - 0.040849 * sin(2.0 * gamma));

    // Hour angle for sunrise/sunset (solar zenith 90.833 deg for atmospheric refraction)
    double lat_rad = DEG_TO_RAD(lat);
    double zenith = DEG_TO_RAD(90.833);

    double cos_ha = (cos(zenith) / (cos(lat_rad) * cos(decl))) - tan(lat_rad) * tan(decl);

    // Clamp for polar day/night
    if (cos_ha > 1.0) {
        // No sunrise — polar night
        out->sunrise_hour = -1;
        out->sunset_hour = -1;
        out->daylight_minutes = 0;
        return;
    }
    if (cos_ha < -1.0) {
        // No sunset — polar day
        out->sunrise_hour = 0;
        out->sunrise_min = 0;
        out->sunset_hour = 23;
        out->sunset_min = 59;
        out->daylight_minutes = 1440;
        return;
    }

    double ha = RAD_TO_DEG(acos(cos_ha));

    // Sunrise time in minutes from midnight UTC
    double sunrise_utc = 720.0 - 4.0 * (lon + ha) - eqtime;
    double sunset_utc  = 720.0 - 4.0 * (lon - ha) - eqtime;

    // These are UTC minutes — caller should apply timezone offset.
    // For now, return as-is. The FSM state can adjust with local timezone.
    int sr = (int)sunrise_utc;
    int ss = (int)sunset_utc;

    // Normalize to 0-1440
    while (sr < 0) sr += 1440;
    while (sr >= 1440) sr -= 1440;
    while (ss < 0) ss += 1440;
    while (ss >= 1440) ss -= 1440;

    out->sunrise_hour = sr / 60;
    out->sunrise_min  = sr % 60;
    out->sunset_hour  = ss / 60;
    out->sunset_min   = ss % 60;
    out->daylight_minutes = ss - sr;
    if (out->daylight_minutes < 0) out->daylight_minutes += 1440;

    // Solar noon elevation
    double solar_noon_alt = RAD_TO_DEG(asin(sin(lat_rad) * sin(decl) + cos(lat_rad) * cos(decl)));
    out->solar_noon_elevation = (float)solar_noon_alt;
}

// ============================================================================
// Aurora probability
// ============================================================================

float astro_calc_aurora_probability(float kp_index, float latitude)
{
    // At 43°N (Newaygo, MI):
    //   Kp < 5  → 0.0 (not visible)
    //   Kp = 5  → 0.05
    //   Kp = 6  → 0.25
    //   Kp = 7  → 0.55
    //   Kp = 8  → 0.80
    //   Kp >= 9 → 0.95
    //
    // Lower latitudes require higher Kp. Adjust threshold based on latitude.
    // Aurora oval reaches ~65° geomagnetic at Kp=0, expanding ~2° per Kp step.
    // Geomagnetic latitude ~= geographic latitude + ~10° for North America.

    float geo_mag_lat = latitude + 10.0f;  // rough geomagnetic offset for NA
    float aurora_lat = 65.0f - 2.0f * kp_index;  // approx auroral oval boundary

    if (geo_mag_lat < aurora_lat) {
        return 0.0f;  // too far south
    }

    // Linear ramp: 0.0 at boundary, 1.0 at 10° inside
    float inside = geo_mag_lat - aurora_lat;
    float prob = inside / 10.0f;
    if (prob > 0.95f) prob = 0.95f;
    if (prob < 0.0f) prob = 0.0f;

    return prob;
}
