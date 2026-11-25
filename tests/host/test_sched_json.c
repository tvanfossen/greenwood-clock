/**
 * @file test_sched_json.c
 * @version 1
 * @brief Host tests for the schedule file: every field parses (daily, yearly, once; clock and
 *        sun window edges with offsets; the optional look), a round trip keeps everything,
 *        and malformed schedules are refused whole.
 */
#include "sched_json.h"

#include <stdio.h>
#include <stdlib.h>
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

static const char *SAMPLE =
    "{\"version\": 1, \"profiles\": ["
    "{\"name\": \"Morning\", \"repeat\": \"daily\", \"start\": \"07:00\", \"end\": \"12:00\","
    " \"image\": \"A:/backgrounds/winter_morning.png\", \"theme\": \"default\"},"
    "{\"name\": \"Evening\", \"repeat\": \"daily\", \"start\": \"sunset\", \"end\": \"22:00\"},"
    "{\"name\": \"Christmas\", \"repeat\": \"yearly\", \"from\": \"12-24\", \"to\": \"12-25\","
    " \"anim\": \"Snowfall\", \"brightness\": 80},"
    "{\"name\": \"Birthday\", \"repeat\": \"once\", \"from\": \"2027-06-03\","
    " \"to\": \"2027-06-03\", \"start\": \"sunrise-30\", \"end\": \"sunset+60\"}]}";

/**
 * @brief Every field of the sample parses to what it says.
 * @version 1
 * @utility
 */
static void test_parse(void)
{
    sched_rule_t r[SCHED_MAX_PROFILES];
    int n = sched_from_json(SAMPLE, r, SCHED_MAX_PROFILES);
    CHECK(n == 4, "4 profiles: %d", n);
    if (n != 4) return;
    CHECK(strcmp(r[0].name, "Morning") == 0 && r[0].repeat == SCHED_EVERY_DAY, "morning daily");
    CHECK(!r[0].all_day && r[0].start.anchor == SCHED_AT_CLOCK && r[0].start.min == 420 &&
              r[0].end.min == 720, "07:00-12:00");
    CHECK(strcmp(r[0].image, "A:/backgrounds/winter_morning.png") == 0, "image");
    CHECK(strcmp(r[0].theme, "default") == 0 && r[0].anim[0] == '\0', "theme, no anim");
    CHECK(r[0].brightness == SCHED_INHERIT, "brightness inherits");
    CHECK(r[1].start.anchor == SCHED_AT_SUNSET && r[1].start.min == 0, "sunset");
    CHECK(r[2].repeat == SCHED_YEARLY && r[2].all_day, "christmas yearly all day");
    CHECK(r[2].from.month == 12 && r[2].from.day == 24 && r[2].to.day == 25, "dates");
    CHECK(strcmp(r[2].anim, "Snowfall") == 0 && r[2].brightness == 80, "anim, brightness");
    CHECK(r[3].repeat == SCHED_ONCE && r[3].from.year == 2027 && r[3].from.month == 6,
          "once 2027-06-03");
    CHECK(r[3].start.anchor == SCHED_AT_SUNRISE && r[3].start.min == -30, "sunrise-30");
    CHECK(r[3].end.anchor == SCHED_AT_SUNSET && r[3].end.min == 60, "sunset+60");
}

/**
 * @brief Writing and reading back keeps every profile exactly.
 * @version 1
 * @utility
 */
static void test_round_trip(void)
{
    sched_rule_t a[SCHED_MAX_PROFILES], b[SCHED_MAX_PROFILES];
    int n = sched_from_json(SAMPLE, a, SCHED_MAX_PROFILES);
    char *json = sched_to_json(a, n);
    CHECK(json != NULL, "written");
    int m = json ? sched_from_json(json, b, SCHED_MAX_PROFILES) : -1;
    CHECK(m == n && memcmp(a, b, sizeof(sched_rule_t) * (size_t)n) == 0, "same back: %s",
          json ? json : "(null)");
    free(json);
}

/**
 * @brief Malformed schedules are refused whole.
 * @version 1
 * @utility
 */
static void test_rejects(void)
{
    static const char *const BAD[] = {
        "not json",
        "{\"version\": 2, \"profiles\": []}",
        "{\"version\": 1}",
        "{\"version\": 1, \"profiles\": [{\"repeat\": \"daily\"}]}",
        "{\"version\": 1, \"profiles\": [{\"name\": \"x\", \"repeat\": \"weekly\"}]}",
        "{\"version\": 1, \"profiles\": [{\"name\": \"x\", \"repeat\": \"daily\","
        " \"start\": \"25:00\", \"end\": \"26:00\"}]}",
        "{\"version\": 1, \"profiles\": [{\"name\": \"x\", \"repeat\": \"daily\","
        " \"start\": \"07:00\"}]}",
        "{\"version\": 1, \"profiles\": [{\"name\": \"x\", \"repeat\": \"yearly\","
        " \"from\": \"13-01\", \"to\": \"12-01\"}]}",
        "{\"version\": 1, \"profiles\": [{\"name\": \"x\", \"repeat\": \"daily\","
        " \"start\": \"noon\", \"end\": \"12:00\"}]}",
        "{\"version\": 1, \"profiles\": [{\"name\": \"x\", \"repeat\": \"daily\","
        " \"start\": \"07:00\", \"end\": \"08:00\", \"brightness\": 150}]}",
    };
    sched_rule_t r[SCHED_MAX_PROFILES];
    for (size_t i = 0; i < sizeof(BAD) / sizeof(BAD[0]); i++) {
        CHECK(sched_from_json(BAD[i], r, SCHED_MAX_PROFILES) < 0, "refused: %s", BAD[i]);
    }
    CHECK(sched_from_json(SAMPLE, r, 2) < 0, "too many profiles refused");
}

/**
 * @brief Run every check and report the tally.
 * @version 1
 * @utility
 * @return 0 when every check passed.
 */
int main(void)
{
    test_parse();
    test_round_trip();
    test_rejects();
    printf("sched_json: %d/%d checks passed\n", s_checks - s_failures, s_checks);
    return s_failures;
}
