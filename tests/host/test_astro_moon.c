/**
 * @file test_astro_moon.c
 * @version 1
 * @brief Host tests for the moon's place and light (astro_moon) against PyEphem 4.2.1 at a
 *        public sample place (Chicago, 41.88 N 87.63 W; refraction folded into a -34'
 *        horizon, upper limb): altitude within 1 degree, lit fraction within 0.02, waxing
 *        side, and moonrise / moonset within 5 minutes.
 */
#include "astro_moon.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

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
#define RISE_SET_SLACK (5 * 60)

/**
 * @brief UTC @p y - @p mo - @p d @p h : @p mi : @p s as epoch seconds.
 * @version 1
 * @utility
 * @return The epoch.
 */
static time_t utc(int y, int mo, int d, int h, int mi, int s)
{
    struct tm t = { .tm_year = y - 1900, .tm_mon = mo - 1, .tm_mday = d, .tm_hour = h,
                    .tm_min = mi, .tm_sec = s };
    return timegm(&t);
}

/** One PyEphem sample: the moment, and what it says. */
typedef struct {
    time_t when;
    float  alt;      /**< Degrees (geometric centre, no refraction). */
    float  illum;    /**< Lit fraction. */
    bool   waxing;
    time_t rise;     /**< The next moonrise after when. */
    time_t set;      /**< The next moonset after when. */
} sample_t;

/**
 * @brief Each sample: altitude, light, side, next rise and set.
 * @version 1
 * @utility
 */
static void test_samples(void)
{
    const sample_t S[] = {
        { utc(2026, 11, 24, 18, 0, 0), -23.06f, 0.998f, true,
          utc(2026, 11, 24, 22, 7, 34), utc(2026, 11, 25, 14, 24, 2) },
        { utc(2026, 12, 1, 18, 0, 0), 3.97f, 0.448f, false,
          utc(2026, 12, 2, 6, 33, 18), utc(2026, 12, 1, 18, 26, 11) },
        { utc(2026, 12, 8, 18, 0, 0), 19.74f, 0.002f, false,
          utc(2026, 12, 9, 14, 0, 18), utc(2026, 12, 8, 21, 43, 28) },
        { utc(2027, 2, 14, 18, 0, 0), 15.08f, 0.548f, true,
          utc(2027, 2, 15, 17, 10, 22), utc(2027, 2, 15, 8, 25, 15) },
        { utc(2026, 7, 1, 17, 0, 0), -56.33f, 0.973f, false,
          utc(2026, 7, 2, 3, 0, 4), utc(2026, 7, 2, 12, 37, 51) },
    };
    for (size_t i = 0; i < sizeof(S) / sizeof(S[0]); i++) {
        astro_moon_t m;
        astro_moon_at(S[i].when, LAT, LON, &m);
        CHECK(m.alt_deg > S[i].alt - 1.0f && m.alt_deg < S[i].alt + 1.0f,
              "sample %zu: altitude %.2f, ephem %.2f", i, (double)m.alt_deg, (double)S[i].alt);
        CHECK(m.illum > S[i].illum - 0.02f && m.illum < S[i].illum + 0.02f,
              "sample %zu: lit %.3f, ephem %.3f", i, (double)m.illum, (double)S[i].illum);
        CHECK(m.illum > 0.99f || m.illum < 0.01f || m.waxing == S[i].waxing,
              "sample %zu: waxing %d, ephem %d", i, m.waxing, S[i].waxing);
        time_t rise = 0, set = 0;
        astro_moon_next(S[i].when, LAT, LON, &rise, &set);
        CHECK(labs((long)(rise - S[i].rise)) <= RISE_SET_SLACK, "sample %zu: rise off by %ld s",
              i, (long)(rise - S[i].rise));
        CHECK(labs((long)(set - S[i].set)) <= RISE_SET_SLACK, "sample %zu: set off by %ld s", i,
              (long)(set - S[i].set));
    }
}

/**
 * @brief The last horizon crossing before a moment is the one the next-crossing search finds
 *        from a little earlier.
 * @version 1
 * @utility
 */
static void test_previous(void)
{
    time_t now = utc(2026, 12, 1, 18, 0, 0);
    time_t rise = 0, set = 0;
    astro_moon_last(now, LAT, LON, &rise, &set);
    CHECK(labs((long)(rise - utc(2026, 12, 1, 5, 24, 12))) <= RISE_SET_SLACK,
          "previous rise off by %ld s", (long)(rise - utc(2026, 12, 1, 5, 24, 12)));
    CHECK(set < now && now - set < 26 * 3600, "previous set within the day before");
}

/**
 * @brief Run the moon tests; exit status is the failure count.
 * @version 1
 * @utility
 * @return 0 when every check passed.
 */
int main(void)
{
    test_samples();
    test_previous();
    printf("astro_moon: %d/%d checks passed\n", s_checks - s_failures, s_checks);
    return s_failures;
}
