/**
 * @file test_sky_data.c
 * v2: the real moon (astro_moon): its place, rise and set, lit fraction.
 * @version 2
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
 * @brief The sun alone (sky_sun_local) agrees with the whole sky's sun, summer time included,
 *        and reports no sunrise in polar night.
 * @version 1
 * @utility
 */
static void test_sun_local(void)
{
    static const int DATES[][3] = { { 2026, 11, 24 }, { 2026, 7, 1 }, { 2026, 3, 8 }, { 2027, 2, 14 } };
    for (size_t i = 0; i < sizeof(DATES) / sizeof(DATES[0]); i++) {
        time_t noon = at(DATES[i][0], DATES[i][1], DATES[i][2], 12, 0);
        sky_data_t s;
        sky_data_compute(noon, LAT, LON, 0.0f, &s);
        int rise = 0, set = 0;
        sky_sun_local(noon, LAT, LON, &rise, &set);
        CHECK(rise == s.sunrise_min && set == s.sunset_min, "%d-%d-%d: %d/%d vs %d/%d",
              DATES[i][0], DATES[i][1], DATES[i][2], rise, set, s.sunrise_min, s.sunset_min);
    }
    int rise = 0, set = 0;
    sky_sun_local(at(2026, 12, 21, 12, 0), 80.0f, 15.0f, &rise, &set);
    CHECK(rise == -1 && set == -1, "polar night: no sunrise (%d/%d)", rise, set);
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
 *        under it at night.
 * v2: the moon is placed by its real rise and set (test_moon_place), not by its phase.
 * @version 2
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
    s.sunrise_min = -1;
    CHECK(sky_sun_angle(&s) == 180.0f, "polar night: no sun");
}

/**
 * @brief The real moon on the wheel (against PyEphem 4.2.1, Chicago): above the horizon when
 *        it is up, by day too; -90 at moonrise; the next moonrise / moonset in local minutes
 *        within 5; the lit fraction from its place, not the calendar.
 * @version 1
 * @utility
 */
static void test_moon_place(void)
{
    sky_data_t s;
    sky_data_compute(at(2026, 12, 1, 12, 0), LAT, LON, 0.0f, &s);   // 3.97 deg up, setting
    float a = sky_moon_angle(&s);
    CHECK(s.moon_up && a > 60.0f && a < 90.0f, "Dec 1 noon: up, about to set, got %.1f", (double)a);
    CHECK(abs(s.moonset_min - (12 * 60 + 26)) <= 5, "Dec 1: sets 12:26, got %d", s.moonset_min);
    CHECK(fabsf(s.moon.illumination_pct - 44.8f) < 2.0f, "Dec 1: 44.8%% lit, got %.1f",
          (double)s.moon.illumination_pct);
    sky_data_compute(at(2026, 11, 24, 12, 0), LAT, LON, 0.0f, &s);  // full, 23 deg down
    a = sky_moon_angle(&s);
    CHECK(!s.moon_up && (a > 90.0f || a < -90.0f), "Nov 24 noon: down, got %.1f", (double)a);
    CHECK(abs(s.moonrise_min - (16 * 60 + 7)) <= 5, "Nov 24: rises 4:07 PM, got %d",
          s.moonrise_min);
    sky_data_compute(at(2026, 11, 24, 16, 9), LAT, LON, 0.0f, &s);  // just risen
    a = sky_moon_angle(&s);
    CHECK(s.moon_up && fabsf(a + 90.0f) < 3.0f, "just after moonrise: -90, got %.1f", (double)a);
    sky_data_compute(at(2027, 2, 14, 12, 0), LAT, LON, 0.0f, &s);   // 15 deg up, waxing
    CHECK(s.moon_up && s.waxing, "Feb 14 noon: a waxing moon up by day");
    CHECK(abs(s.moonset_min - (2 * 60 + 25)) <= 5, "Feb 14: sets 2:25 AM, got %d",
          s.moonset_min);
}

/**
 * @brief Run the sky data tests in Chicago time; exit status is the failure count.
 * v2: the sky wheel test.
 * v3: the sun alone.
 * v4: the real moon's place.
 * @version 4
 * @utility
 * @return 0 when every check passed.
 */
int main(void)
{
    setenv("TZ", "CST6CDT,M3.2.0,M11.1.0", 1);
    tzset();
    test_sun();
    test_sun_local();
    test_days_until();
    test_moon_and_kp();
    test_wheel();
    test_moon_place();
    printf("sky_data: %d/%d checks passed\n", s_checks - s_failures, s_checks);
    return s_failures;
}
