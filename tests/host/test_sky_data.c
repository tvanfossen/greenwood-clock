/**
 * @file test_sky_data.c
 * @version 1
 * @brief Host tests for the sky screen's data: where the day is, local sun times, days to
 *        dated events (year wrap, leap years) and the moon's waxing side. At a public sample
 *        place (Chicago), never the device's.
 */
#include "sky_data.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static int s_failures = 0;
static int s_checks   = 0;

#define CHECK(cond, ...)                                              \
    do {                                                              \
        s_checks++;                                                   \
        if (!(cond)) {                                                \
            s_failures++;                                             \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);               \
            printf(__VA_ARGS__);                                      \
            printf("\n");                                             \
        }                                                             \
    } while (0)

#define LAT  41.88f
#define LON  (-87.63f)

/**
 * @brief Local time @p y - @p mo - @p d @p h : @p mi as epoch seconds.
 * @version 1
 * @utility
 * @return The epoch.
 */
static time_t at(int y, int mo, int d, int h, int mi)
{
    struct tm t = { .tm_year = y - 1900, .tm_mon = mo - 1, .tm_mday = d, .tm_hour = h,
                    .tm_min = mi, .tm_isdst = -1 };
    return mktime(&t);
}

/**
 * @brief Sun times (local, within 3 min of an almanac) and where the day is.
 * @version 1
 * @utility
 */
static void test_sun(void)
{
    sky_data_t s;
    sky_data_compute(at(2026, 11, 24, 14, 20), LAT, LON, 0.0f, &s);
    CHECK(abs(s.sunrise_min - (6 * 60 + 51)) <= 3, "sunrise ~6:51, got %d", s.sunrise_min);
    CHECK(abs(s.sunset_min - (16 * 60 + 22)) <= 3, "sunset ~16:22, got %d", s.sunset_min);
    CHECK(s.part == SKY_DAY, "14:20 is day");
    sky_data_compute(at(2026, 11, 24, 20, 40), LAT, LON, 0.0f, &s);
    CHECK(s.part == SKY_NIGHT_AFTER, "20:40 is after sunset");
    sky_data_compute(at(2026, 12, 5, 6, 10), LAT, LON, 0.0f, &s);
    CHECK(s.part == SKY_NIGHT_BEFORE, "6:10 is before sunrise");
    sky_data_compute(at(2026, 7, 1, 12, 0), LAT, LON, 0.0f, &s);   // summer time
    CHECK(abs(s.sunrise_min - (5 * 60 + 18)) <= 3, "July sunrise ~5:18 CDT, got %d",
          s.sunrise_min);
}

/**
 * @brief Days to dated events: today is 0, the year wraps, leap days count.
 * @version 1
 * @utility
 */
static void test_days_until(void)
{
    CHECK(sky_days_until(at(2026, 11, 24, 20, 0), 12, 21) == 27, "Nov 24 -> Dec 21");
    CHECK(sky_days_until(at(2026, 12, 21, 23, 0), 12, 21) == 0, "today");
    CHECK(sky_days_until(at(2026, 12, 30, 9, 0), 1, 3) == 4, "year wrap");
    CHECK(sky_days_until(at(2028, 2, 28, 9, 0), 3, 20) == 21, "leap year");
    CHECK(sky_days_until(at(2026, 3, 1, 9, 0), 3, 20) == 19, "across the DST change");
    sky_data_t s;
    sky_data_compute(at(2026, 11, 24, 14, 20), LAT, LON, 0.0f, &s);
    CHECK(s.season_days == 27 && s.shower_days == 20, "solstice 27, Geminids 20: %d %d",
          s.season_days, s.shower_days);
}

/**
 * @brief The moon's lit side: waxing before full, waning after; Kp unknown means no aurora.
 * @version 1
 * @utility
 */
static void test_moon_and_kp(void)
{
    sky_data_t s;
    sky_data_compute(at(2026, 11, 20, 21, 0), LAT, LON, 0.0f, &s);
    CHECK(s.waxing, "Nov 20: waxing (full Nov 24)");
    CHECK(s.kp == 0.0f && s.aurora_prob == 0.0f, "no Kp, no aurora");
    sky_data_compute(at(2026, 12, 5, 6, 10), LAT, LON, 5.0f, &s);
    CHECK(!s.waxing, "Dec 5: waning (new Dec 9)");
    CHECK(s.moon.illumination_pct > 5.0f && s.moon.illumination_pct < 35.0f,
          "Dec 5: a crescent, got %.0f%%", (double)s.moon.illumination_pct);
    CHECK(s.kp == 5.0f, "Kp kept");
}

/**
 * @brief The sun on the wheel: on the horizon at sunrise and sunset, at the top midday,
 *        under it at night; the full moon opposite the sun, the new moon with it.
 * @version 1
 * @utility
 */
static void test_wheel(void)
{
    sky_data_t s = { .sunrise_min = 420, .sunset_min = 1020, .part = SKY_DAY };
    s.now_min = 420;
    CHECK(fabsf(sky_sun_angle(&s) + 90.0f) < 0.1f, "sunrise: -90, got %.1f",
          (double)sky_sun_angle(&s));
    s.now_min = 720;
    CHECK(fabsf(sky_sun_angle(&s)) < 0.1f, "midday: 0");
    s.part = SKY_NIGHT_AFTER;
    s.now_min = 1020;
    CHECK(fabsf(sky_sun_angle(&s) - 90.0f) < 0.1f, "sunset: 90");
    s.now_min = 0;   // 7 h after sunset of a 14 h night: half way
    CHECK(fabsf(fabsf(sky_sun_angle(&s)) - 180.0f) < 0.1f, "mid-night: 180, got %.1f",
          (double)sky_sun_angle(&s));
    s.part = SKY_NIGHT_BEFORE;
    s.now_min = 300;
    float a = sky_sun_angle(&s);
    CHECK(a < -90.0f && a > -180.0f, "before dawn: below the left horizon, got %.1f", (double)a);
    s.part = SKY_DAY;
    s.now_min = 720;
    s.moon_frac = 0.5f;
    CHECK(fabsf(fabsf(sky_moon_angle(&s)) - 180.0f) < 0.1f, "full moon at noon: opposite");
    s.now_min = 0;
    CHECK(fabsf(sky_moon_angle(&s)) < 0.1f, "full moon at midnight: at the top");
    s.moon_frac = 0.0f;
    s.now_min = 720;
    CHECK(fabsf(sky_moon_angle(&s)) < 0.1f, "new moon: with the sun");
    s.moon_frac = 0.25f;   // first quarter: rises about noon, highest at sunset
    s.now_min = 1080;
    CHECK(fabsf(sky_moon_angle(&s)) < 0.1f, "first quarter highest at 18:00");
    s.sunrise_min = -1;
    CHECK(sky_sun_angle(&s) == 180.0f, "polar night: no sun");
}

/**
 * @brief Run the sky data tests in Chicago time; exit status is the failure count.
 * v2: the sky wheel test.
 * @version 2
 * @utility
 * @return 0 when every check passed.
 */
int main(void)
{
    setenv("TZ", "CST6CDT,M3.2.0,M11.1.0", 1);
    tzset();
    test_sun();
    test_days_until();
    test_moon_and_kp();
    test_wheel();
    printf("sky_data: %d/%d checks passed\n", s_checks - s_failures, s_checks);
    return s_failures;
}
