/**
 * @file test_sched_rule.c
 * @version 1
 * @brief Host tests for the schedule profile text: window edges (clock and sun-anchored),
 *        the "when" line for every repeat kind, and the "look" line.
 */
#include "sched_rule.h"

#include <stdio.h>
#include <string.h>

static int s_failures = 0;
static int s_checks   = 0;

#define CHECK_STR(got, want)                                                       \
    do {                                                                           \
        s_checks++;                                                                \
        if (strcmp((got), (want)) != 0) {                                          \
            s_failures++;                                                          \
            printf("FAIL %s:%d: \"%s\" != \"%s\"\n", __FILE__, __LINE__, (got),    \
                   (want));                                                        \
        }                                                                          \
    } while (0)

/**
 * @brief A profile with nothing set: every day, all day, global look.
 * @version 1
 * @utility
 * @return The profile.
 */
static sched_rule_t blank(void)
{
    sched_rule_t r;
    memset(&r, 0, sizeof(r));
    r.all_day = true;
    r.brightness = SCHED_INHERIT;
    return r;
}

/**
 * @brief Window edges at a clock time and at sunrise/sunset with offsets.
 * @version 1
 * @utility
 */
static void test_times(void)
{
    char s[32];
    sched_time_t t = { SCHED_AT_CLOCK, 7 * 60 };
    sched_fmt_time(&t, s, sizeof(s));
    CHECK_STR(s, "7:00 AM");
    t = (sched_time_t){ SCHED_AT_SUNSET, 0 };
    sched_fmt_time(&t, s, sizeof(s));
    CHECK_STR(s, "Sunset");
    t = (sched_time_t){ SCHED_AT_SUNSET, 60 };
    sched_fmt_time(&t, s, sizeof(s));
    CHECK_STR(s, "Sunset + 1h");
    t = (sched_time_t){ SCHED_AT_SUNRISE, -30 };
    sched_fmt_time(&t, s, sizeof(s));
    CHECK_STR(s, "Sunrise - 30m");
    t = (sched_time_t){ SCHED_AT_SUNRISE, 75 };
    sched_fmt_time(&t, s, sizeof(s));
    CHECK_STR(s, "Sunrise + 1h 15m");
}

/**
 * @brief The "when" line for each repeat kind.
 * @version 1
 * @utility
 */
static void test_when(void)
{
    char s[96];
    sched_rule_t r = blank();
    r.all_day = false;
    r.start = (sched_time_t){ SCHED_AT_CLOCK, 7 * 60 };
    r.end = (sched_time_t){ SCHED_AT_CLOCK, 12 * 60 };
    sched_fmt_when(&r, s, sizeof(s));
    CHECK_STR(s, "Every day, 7:00 AM - 12:00 PM");

    r = blank();
    r.repeat = SCHED_YEARLY;
    r.from = (sched_date_t){ 0, 2, 14 };
    r.to = r.from;
    sched_fmt_when(&r, s, sizeof(s));
    CHECK_STR(s, "Every year, Feb 14, all day");

    r.from = (sched_date_t){ 0, 12, 24 };
    r.to = (sched_date_t){ 0, 12, 25 };
    sched_fmt_when(&r, s, sizeof(s));
    CHECK_STR(s, "Every year, Dec 24 - Dec 25, all day");

    r.repeat = SCHED_ONCE;
    r.from = r.to = (sched_date_t){ 2027, 6, 3 };
    sched_fmt_when(&r, s, sizeof(s));
    CHECK_STR(s, "Once, Jun 3, 2027, all day");
}

/**
 * @brief The "look" line: set fields only, global when none.
 * @version 1
 * @utility
 */
static void test_look(void)
{
    char s[128];
    sched_rule_t r = blank();
    sched_fmt_look(&r, s, sizeof(s));
    CHECK_STR(s, "Global settings");
    strcpy(r.image, "A:/backgrounds/winter_morning.png");
    strcpy(r.theme, "default");
    strcpy(r.anim, "Snowfall");
    r.brightness = 60;
    sched_fmt_look(&r, s, sizeof(s));
    CHECK_STR(s, "winter_morning, Default, Snowfall, 60%");
    r = blank();
    r.brightness = 12;
    sched_fmt_look(&r, s, sizeof(s));
    CHECK_STR(s, "12%");
}

/**
 * @brief Run every check and report the tally.
 * @version 1
 * @utility
 * @return 0 when every check passed.
 */
int main(void)
{
    test_times();
    test_when();
    test_look();
    printf("sched_rule: %d/%d checks passed\n", s_checks - s_failures, s_checks);
    return s_failures;
}
