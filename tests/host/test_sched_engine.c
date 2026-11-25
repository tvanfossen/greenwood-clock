/**
 * @file test_sched_engine.c
 * v2: days until a profile next runs.
 * @version 2
 * @brief Host tests for the schedule engine: which profile is showing at a local date and
 *        minute (an event over the daily profile, windows past midnight, year wrap, once vs
 *        yearly, gaps), and the overlap check that refuses a save (events by dates, daily
 *        profiles by times, sun-anchored edges that collide only part of the year).
 */
#include "sched_engine.h"

#include <stdio.h>
#include <string.h>

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

#define AT(h, m)    ((sched_time_t){ SCHED_AT_CLOCK, (int16_t)((h) * 60 + (m)) })
#define SUNSET(off) ((sched_time_t){ SCHED_AT_SUNSET, (int16_t)(off) })

/**
 * @brief A fixed sun: rises 7:00, sets 17:00 every day.
 * @version 1
 * @utility
 * @return The sun times.
 */
static sched_sun_t fixed_sun(sched_date_t d, void *ctx)
{
    (void)d;
    (void)ctx;
    return (sched_sun_t){ 7 * 60, 17 * 60 };
}

/**
 * @brief A seasonal sun: sets 19:00 April through September, 17:00 otherwise.
 * @version 1
 * @utility
 * @return The sun times.
 */
static sched_sun_t seasonal_sun(sched_date_t d, void *ctx)
{
    (void)ctx;
    bool summer = d.month >= 4 && d.month <= 9;
    return (sched_sun_t){ 6 * 60, summer ? 19 * 60 : 17 * 60 };
}

/**
 * @brief A daily profile from @p a to @p b.
 * @version 1
 * @utility
 * @return The profile.
 */
static sched_rule_t daily(const char *name, sched_time_t a, sched_time_t b)
{
    sched_rule_t r;
    memset(&r, 0, sizeof(r));
    snprintf(r.name, sizeof(r.name), "%s", name);
    r.repeat = SCHED_EVERY_DAY;
    r.start = a;
    r.end = b;
    r.brightness = SCHED_INHERIT;
    return r;
}

/**
 * @brief A yearly all-day event from @p m1 / @p d1 to @p m2 / @p d2.
 * @version 1
 * @utility
 * @return The profile.
 */
static sched_rule_t event(const char *name, int m1, int d1, int m2, int d2)
{
    sched_rule_t r = daily(name, AT(0, 0), AT(0, 0));
    r.repeat = SCHED_YEARLY;
    r.all_day = true;
    r.from = (sched_date_t){ 0, (uint8_t)m1, (uint8_t)d1 };
    r.to = (sched_date_t){ 0, (uint8_t)m2, (uint8_t)d2 };
    return r;
}

/**
 * @brief The four daily profiles of the sample schedule.
 * @version 1
 * @utility
 */
static void day_parts(sched_rule_t r[4])
{
    r[0] = daily("Morning", AT(7, 0), AT(12, 0));
    r[1] = daily("Afternoon", AT(12, 0), SUNSET(0));
    r[2] = daily("Evening", SUNSET(0), AT(22, 0));
    r[3] = daily("Night", AT(22, 0), AT(7, 0));
}

/**
 * @brief The index of the profile showing on @p y-@p m-@p d at @p hh:@p mm.
 * @version 1
 * @utility
 * @return Its index, or -1 (global settings).
 */
static int at(const sched_rule_t *r, int n, int y, int m, int d, int hh, int mm)
{
    sched_sun_src_t sun = { fixed_sun, NULL };
    sched_date_t day = { (uint16_t)y, (uint8_t)m, (uint8_t)d };
    return sched_active(r, n, day, hh * 60 + mm, &sun);
}

/**
 * @brief Daily profiles tile the day, including the night past midnight; a gap shows none.
 * @version 1
 * @utility
 */
static void test_daily(void)
{
    sched_rule_t r[4];
    day_parts(r);
    CHECK(at(r, 4, 2026, 11, 24, 8, 0) == 0, "8:00 morning");
    CHECK(at(r, 4, 2026, 11, 24, 13, 0) == 1, "13:00 afternoon");
    CHECK(at(r, 4, 2026, 11, 24, 16, 59) == 1, "16:59 still afternoon");
    CHECK(at(r, 4, 2026, 11, 24, 17, 0) == 2, "sunset: evening");
    CHECK(at(r, 4, 2026, 11, 24, 23, 0) == 3, "23:00 night");
    CHECK(at(r, 4, 2026, 11, 25, 3, 0) == 3, "3:00 night (from yesterday)");
    CHECK(at(r, 4, 2026, 11, 25, 7, 0) == 0, "7:00 morning again");
    r[2] = r[3];   // drop Evening: sunset..22:00 is a gap
    CHECK(at(r, 3, 2026, 11, 24, 18, 0) == -1, "gap: global settings");
}

/**
 * @brief Events replace the daily profile: all day, across the new year, a window past
 *        midnight, and once versus every year.
 * @version 1
 * @utility
 */
static void test_events(void)
{
    sched_rule_t r[8];
    day_parts(r);
    r[4] = event("Christmas", 12, 24, 12, 25);
    r[5] = event("Winter break", 12, 27, 1, 2);
    r[6] = event("Valentine's Day", 2, 14, 2, 14);
    r[6].all_day = false;
    r[6].start = AT(18, 0);
    r[6].end = AT(2, 0);
    r[7] = event("Birthday", 6, 3, 6, 3);
    r[7].repeat = SCHED_ONCE;
    r[7].from.year = r[7].to.year = 2027;

    CHECK(at(r, 8, 2026, 12, 24, 13, 0) == 4, "Christmas Eve afternoon");
    CHECK(at(r, 8, 2026, 12, 25, 23, 59) == 4, "Christmas night");
    CHECK(at(r, 8, 2026, 12, 26, 0, 30) == 3, "Dec 26: back to night");
    CHECK(at(r, 8, 2027, 1, 1, 10, 0) == 5, "New Year's Day: winter break");
    CHECK(at(r, 8, 2027, 1, 3, 10, 0) == 0, "Jan 3: morning");
    CHECK(at(r, 8, 2027, 2, 14, 17, 0) == 2, "Feb 14 before 18:00: evening");
    CHECK(at(r, 8, 2027, 2, 15, 1, 0) == 6, "Feb 15 1:00: Valentine's window");
    CHECK(at(r, 8, 2027, 2, 15, 3, 0) == 3, "Feb 15 3:00: night");
    CHECK(at(r, 8, 2027, 6, 3, 9, 0) == 7, "Jun 3 2027: birthday");
    CHECK(at(r, 8, 2028, 6, 3, 9, 0) == 0, "Jun 3 2028: once means once");
}

/**
 * @brief The overlap that refuses a save, as the editor says it.
 * @version 1
 * @utility
 * @return Whether there is one (@p msg set).
 */
static bool overlap(const sched_rule_t *r, int n, int cand, sched_sun_fn fn, char *msg,
                    size_t len)
{
    sched_sun_src_t sun = { fn, NULL };
    sched_overlap_t o;
    bool hit = sched_find_overlap(r, n, cand, (sched_date_t){ 2026, 1, 1 }, 366, &sun, &o);
    msg[0] = '\0';
    if (hit) sched_fmt_overlap(r, &o, msg, len);
    return hit;
}

/**
 * @brief Events may not overlap events; daily profiles may not overlap daily profiles; an
 *        event over a daily profile is fine; a sun edge can collide only in summer.
 * @version 1
 * @utility
 */
static void test_overlaps(void)
{
    char msg[128];
    sched_rule_t r[7];
    day_parts(r);
    r[4] = event("Christmas", 12, 24, 12, 25);
    r[5] = event("Winter break", 12, 20, 1, 2);
    CHECK(overlap(r, 6, 5, fixed_sun, msg, sizeof(msg)), "winter break hits Christmas");
    CHECK(strcmp(msg, "Overlaps Christmas on Dec 24 - Dec 25") == 0, "%s", msg);

    r[5] = event("Winter break", 12, 27, 1, 2);
    CHECK(!overlap(r, 6, 5, fixed_sun, msg, sizeof(msg)), "moved: no overlap (%s)", msg);
    CHECK(!overlap(r, 6, 4, fixed_sun, msg, sizeof(msg)), "an event over the daily is fine");

    r[6] = daily("Brunch", AT(11, 0), AT(13, 0));
    CHECK(overlap(r, 7, 6, fixed_sun, msg, sizeof(msg)), "brunch hits morning");
    CHECK(strcmp(msg, "Overlaps Morning from 11:00 AM to 12:00 PM") == 0, "%s", msg);

    r[6] = daily("Dusk", AT(17, 30), AT(18, 0));
    r[2] = daily("Evening", AT(19, 0), AT(22, 0));
    CHECK(!overlap(r, 7, 6, fixed_sun, msg, sizeof(msg)), "fixed sun 17:00: no overlap");
    CHECK(overlap(r, 7, 6, seasonal_sun, msg, sizeof(msg)), "summer sunset 19:00: overlap");
    CHECK(strcmp(msg, "Overlaps Afternoon from 5:30 PM to 6:00 PM, Apr 1 - Sep 30") == 0, "%s",
          msg);
}

/**
 * @brief Days until a profile next runs: today is 0; a yearly event counts to its next
 *        start, across the new year; a one-off in the past never runs again.
 * @version 1
 * @utility
 */
static void test_days_until(void)
{
    sched_date_t nov24 = { 2026, 11, 24 };
    sched_rule_t xmas = event("Christmas", 12, 24, 12, 25);
    CHECK(sched_days_until(&xmas, nov24) == 30, "Christmas in 30 days: %d",
          sched_days_until(&xmas, nov24));
    CHECK(sched_days_until(&xmas, (sched_date_t){ 2026, 12, 25 }) == 0, "running today");
    sched_rule_t brk = event("Winter break", 12, 27, 1, 2);
    CHECK(sched_days_until(&brk, (sched_date_t){ 2027, 1, 1 }) == 0, "across the new year");
    sched_rule_t morning = daily("Morning", AT(7, 0), AT(12, 0));
    CHECK(sched_days_until(&morning, nov24) == 0, "daily: today");
    sched_rule_t once = event("Birthday", 6, 3, 6, 3);
    once.repeat = SCHED_ONCE;
    once.from.year = once.to.year = 2026;
    CHECK(sched_days_until(&once, nov24) == -1, "a past one-off never");
    once.from.year = once.to.year = 2027;
    CHECK(sched_days_until(&once, nov24) == 191, "next June: %d", sched_days_until(&once, nov24));
}

/**
 * @brief Run every check and report the tally.
 * v2: days until.
 * @version 2
 * @utility
 * @return 0 when every check passed.
 */
int main(void)
{
    test_daily();
    test_events();
    test_overlaps();
    test_days_until();
    printf("sched_engine: %d/%d checks passed\n", s_checks - s_failures, s_checks);
    return s_failures;
}
