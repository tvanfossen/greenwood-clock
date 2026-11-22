/**
 * @file test_bg_schedule.c
 * @version 1
 * @brief Host tests for the background schedule model: defaults, slot/profile resolution
 *        (including the slot that wraps past midnight) and validation.
 */
#include "bg_schedule_model.h"

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

enum { SUN = 0, MON = 1, TUE = 2, SAT = 6 };

/**
 * @brief The user's winter schedule: 07:00 morning, 12:00 afternoon, 17:00 current photo,
 *        22:00 night, the same profiles every day.
 * @version 1
 * @dg_internal
 */
static void winter_schedule(bg_schedule_t *s)
{
    bg_schedule_defaults(s);
    s->slot_count = 4;
    s->profile_count = 4;
    const uint16_t starts[4] = {7 * 60, 12 * 60, 17 * 60, 22 * 60};
    memcpy(s->slot_start, starts, sizeof(starts));
    for (int d = 0; d < BG_SCHED_DAYS; d++) {
        for (int i = 0; i < 4; i++) s->grid[d][i] = (uint8_t)i;
    }
    for (int i = 1; i < 4; i++) {
        snprintf(s->profiles[i].name, sizeof(s->profiles[i].name), "p%d", i);
        s->profiles[i].text_color = BG_INHERIT_COLOR;
        s->profiles[i].brightness = BG_INHERIT;
        s->profiles[i].anim = BG_INHERIT;
    }
}

/**
 * @brief Defaults are one all-day slot showing profile 0, which inherits every setting.
 * @version 1
 * @req REQ-BG-004
 */
static void test_defaults(void)
{
    bg_schedule_t s;
    bg_schedule_defaults(&s);
    CHECK(bg_schedule_valid(&s), "defaults must validate");
    CHECK(s.slot_count == 1 && s.profile_count == 1, "slots %u profiles %u", s.slot_count,
          s.profile_count);
    CHECK(s.profiles[0].image[0] == '\0', "default profile inherits the image");
    CHECK(s.profiles[0].text_color == BG_INHERIT_COLOR, "inherits text colour");
    CHECK(s.profiles[0].brightness == BG_INHERIT && s.profiles[0].anim == BG_INHERIT,
          "inherits brightness and animation");
    CHECK(bg_schedule_profile_at(&s, MON, 0) == 0, "midnight -> 0");
    CHECK(bg_schedule_profile_at(&s, SAT, 23 * 60 + 59) == 0, "23:59 -> 0");
}

/**
 * @brief Each time of day resolves to the slot whose start is the latest one not after it.
 * @version 1
 * @req REQ-BG-004
 */
static void test_slot_boundaries(void)
{
    bg_schedule_t s;
    winter_schedule(&s);
    CHECK(bg_schedule_valid(&s), "winter schedule must validate");
    CHECK(bg_schedule_profile_at(&s, MON, 7 * 60) == 0, "07:00 morning");
    CHECK(bg_schedule_profile_at(&s, MON, 11 * 60 + 59) == 0, "11:59 morning");
    CHECK(bg_schedule_profile_at(&s, MON, 12 * 60) == 1, "12:00 afternoon");
    CHECK(bg_schedule_profile_at(&s, MON, 16 * 60 + 59) == 1, "16:59 afternoon");
    CHECK(bg_schedule_profile_at(&s, MON, 17 * 60) == 2, "17:00 current photo");
    CHECK(bg_schedule_profile_at(&s, MON, 22 * 60) == 3, "22:00 night");
    CHECK(bg_schedule_profile_at(&s, MON, 3 * 60) == 3, "03:00 night (wraps)");
}

/**
 * @brief Before the first slot starts, the previous day's last slot is still running.
 * @version 1
 * @req REQ-BG-004
 */
static void test_wrap_uses_previous_day(void)
{
    bg_schedule_t s;
    winter_schedule(&s);
    s.grid[SUN][3] = 2;   // Sunday night shows the current photo
    s.grid[MON][3] = 3;
    CHECK(bg_schedule_profile_at(&s, MON, 3 * 60) == 2, "Mon 03:00 -> Sunday's night slot");
    CHECK(bg_schedule_profile_at(&s, MON, 23 * 60) == 3, "Mon 23:00 -> Monday's night slot");
    CHECK(bg_schedule_profile_at(&s, SUN, 1 * 60) == s.grid[SAT][3], "Sun 01:00 -> Saturday's");
}

/**
 * @brief Invalid schedules are rejected (they would index out of range or never match).
 * @version 1
 * @req REQ-BG-004
 */
static void test_validation(void)
{
    bg_schedule_t s;
    winter_schedule(&s);
    s.slot_start[2] = s.slot_start[1];
    CHECK(!bg_schedule_valid(&s), "slot starts must strictly increase");

    winter_schedule(&s);
    s.slot_start[3] = 24 * 60;
    CHECK(!bg_schedule_valid(&s), "slot start must be < 24:00");

    winter_schedule(&s);
    s.grid[TUE][1] = 4;
    CHECK(!bg_schedule_valid(&s), "grid index must be < profile_count");

    winter_schedule(&s);
    s.slot_count = 0;
    CHECK(!bg_schedule_valid(&s), "at least one slot");
    s.slot_count = BG_SCHED_SLOTS + 1;
    CHECK(!bg_schedule_valid(&s), "at most BG_SCHED_SLOTS slots");

    winter_schedule(&s);
    memset(s.profiles[1].image, 'x', sizeof(s.profiles[1].image));
    CHECK(!bg_schedule_valid(&s), "image path must be NUL-terminated");

    winter_schedule(&s);
    s.profiles[2].brightness = 101;
    CHECK(!bg_schedule_valid(&s), "brightness must be inherit or 0..100");
}

/**
 * @brief Run every test and report.
 * @version 1
 * @dg_internal
 * @return 0 when every check passed, 1 otherwise.
 */
int main(void)
{
    test_defaults();
    test_slot_boundaries();
    test_wrap_uses_previous_day();
    test_validation();
    printf("bg_schedule: %d/%d checks passed\n", s_checks - s_failures, s_checks);
    return s_failures ? 1 : 0;
}
