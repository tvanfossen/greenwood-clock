/**
 * @file test_sched_rule.c
 * v2: a daily profile's when-line is its window only.
 * v3: filling a profile's unset fields; what a gap shows.
 * v4: a theme name's file stem.
 * @version 4
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
 * v2: daily is the window only.
 * @version 2
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
    CHECK_STR(s, "7:00 AM - 12:00 PM");

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
 * @brief Filling: only the unset fields take the given values, and it says whether any did.
 * @version 1
 * @utility
 */
static void test_fill(void)
{
    sched_rule_t with = blank();
    snprintf(with.image, sizeof(with.image), "A:/backgrounds/campfire.png");
    snprintf(with.theme, sizeof(with.theme), "default");
    snprintf(with.anim, sizeof(with.anim), "Snowfall");
    with.brightness = 60;

    sched_rule_t r = blank();
    snprintf(r.theme, sizeof(r.theme), "sassafras");
    s_checks++;
    if (!sched_rule_fill(&r, &with)) {
        s_failures++;
        printf("FAIL %s:%d: unset fields filled but not reported\n", __FILE__, __LINE__);
    }
    CHECK_STR(r.image, "A:/backgrounds/campfire.png");
    CHECK_STR(r.theme, "sassafras");   // set: kept
    CHECK_STR(r.anim, "Snowfall");
    s_checks++;
    if (r.brightness != 60) {
        s_failures++;
        printf("FAIL %s:%d: brightness %d != 60\n", __FILE__, __LINE__, r.brightness);
    }
    s_checks++;
    if (sched_rule_fill(&r, &with)) {
        s_failures++;
        printf("FAIL %s:%d: nothing left to fill but a change reported\n", __FILE__, __LINE__);
    }
}

/**
 * @brief A gap (no profile covers now): the built-in Default theme, no picture, no animation.
 * @version 1
 * @utility
 */
static void test_gap(void)
{
    sched_rule_t g;
    memset(&g, 0x5a, sizeof(g));
    sched_rule_gap(&g);
    CHECK_STR(g.image, "");
    CHECK_STR(g.theme, "default");
    CHECK_STR(g.anim, "None");
    s_checks++;
    if (g.brightness != SCHED_INHERIT) {
        s_failures++;
        printf("FAIL %s:%d: gap brightness %d is not the stored one\n", __FILE__, __LINE__,
               g.brightness);
    }
}

/**
 * @brief Theme names to file stems: lower case, runs of anything else one '-', at most 15
 *        characters, no edge dashes; nothing usable, or the built-in "default", refused.
 * @version 1
 * @utility
 */
static void test_theme_stem(void)
{
    static const char *const CASES[][2] = {
        { "Rose Gold", "rose-gold" },
        { "  Valentine's  Day!", "valentine-s-day" },
        { "Christmas Eve 2026 night", "christmas-eve-2" },
        { "Night--Owl", "night-owl" },
        { "abcdefghijklmn- x", "abcdefghijklmn" },
    };
    char stem[16];
    for (size_t i = 0; i < sizeof(CASES) / sizeof(CASES[0]); i++) {
        stem[0] = '\0';
        s_checks++;
        if (!sched_theme_stem(CASES[i][0], stem, sizeof(stem))) {
            s_failures++;
            printf("FAIL %s:%d: \"%s\" refused\n", __FILE__, __LINE__, CASES[i][0]);
        }
        CHECK_STR(stem, CASES[i][1]);
    }
    static const char *const REFUSED[] = { "", "  !!  ", "Default", "DEFAULT " };
    for (size_t i = 0; i < sizeof(REFUSED) / sizeof(REFUSED[0]); i++) {
        s_checks++;
        if (sched_theme_stem(REFUSED[i], stem, sizeof(stem))) {
            s_failures++;
            printf("FAIL %s:%d: \"%s\" accepted as \"%s\"\n", __FILE__, __LINE__, REFUSED[i], stem);
        }
    }
}

/**
 * @brief Run every check and report the tally.
 * v2: fill and gap.
 * v3: theme stems.
 * @version 3
 * @utility
 * @return 0 when every check passed.
 */
int main(void)
{
    test_times();
    test_when();
    test_look();
    test_fill();
    test_gap();
    test_theme_stem();
    printf("sched_rule: %d/%d checks passed\n", s_checks - s_failures, s_checks);
    return s_failures;
}
