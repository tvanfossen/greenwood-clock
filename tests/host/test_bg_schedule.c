/**
 * @file test_bg_schedule.c
 * @version 2
 * @brief Host tests for the background schedule model: defaults, slot/profile resolution
 *        (including the slot that wraps past midnight), validation and the editor's edits.
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
/**
 * @brief Adding a slot appends one starting halfway between the last start and midnight, with
 *        every day's new cell copying that day's last slot; adding past the maximum fails.
 * @version 1
 * @req REQ-BG-004
 */
static void test_add_slot(void)
{
    bg_schedule_t s;
    bg_schedule_defaults(&s);   // one slot at 00:00
    CHECK(bg_schedule_add_slot(&s), "add to one slot");
    CHECK(s.slot_count == 2 && s.slot_start[1] == 12 * 60, "new slot at noon, got %u", s.slot_start[1]);
    CHECK(s.grid[MON][1] == s.grid[MON][0], "new cell copies the day's last slot");
    winter_schedule(&s);
    CHECK(!bg_schedule_add_slot(&s), "a fifth slot is refused");
    CHECK(bg_schedule_valid(&s), "still valid after the refusal");
}

/**
 * @brief Removing a slot closes the gap in the start times and every day's cells; the last
 *        slot cannot be removed.
 * @version 1
 * @req REQ-BG-004
 */
static void test_remove_slot(void)
{
    bg_schedule_t s;
    winter_schedule(&s);
    CHECK(bg_schedule_remove_slot(&s, 1), "remove the 12:00 slot");
    CHECK(s.slot_count == 3 && s.slot_start[1] == 17 * 60, "17:00 moved into slot 1");
    CHECK(s.grid[SAT][1] == 2 && s.grid[SAT][2] == 3, "cells shifted left");
    CHECK(bg_schedule_valid(&s), "valid after removal");
    bg_schedule_defaults(&s);
    CHECK(!bg_schedule_remove_slot(&s, 0), "the only slot stays");
    CHECK(!bg_schedule_remove_slot(&s, 3), "out of range is refused");
}

/**
 * @brief A slot start can move only between its neighbours (starts stay strictly ascending).
 * @version 1
 * @req REQ-BG-004
 */
static void test_set_slot_start(void)
{
    bg_schedule_t s;
    winter_schedule(&s);
    CHECK(bg_schedule_set_slot_start(&s, 1, 13 * 60), "12:00 -> 13:00");
    CHECK(s.slot_start[1] == 13 * 60, "stored");
    CHECK(!bg_schedule_set_slot_start(&s, 1, 17 * 60), "onto the next start is refused");
    CHECK(!bg_schedule_set_slot_start(&s, 1, 6 * 60), "before the previous start is refused");
    CHECK(s.slot_start[1] == 13 * 60, "a refused move changes nothing");
    CHECK(!bg_schedule_set_slot_start(&s, 3, BG_MINUTES_PER_DAY), "past midnight is refused");
}

/**
 * @brief A new profile inherits everything and is named; at the maximum it is refused.
 * @version 1
 * @req REQ-BG-004
 */
static void test_add_profile(void)
{
    bg_schedule_t s;
    winter_schedule(&s);
    int i = bg_schedule_add_profile(&s, "Night");
    CHECK(i == 4 && s.profile_count == 5, "added as profile 4, got %d", i);
    CHECK(strcmp(s.profiles[4].name, "Night") == 0, "named");
    CHECK(s.profiles[4].image[0] == '\0' && s.profiles[4].text_color == BG_INHERIT_COLOR &&
              s.profiles[4].brightness == BG_INHERIT && s.profiles[4].anim == BG_INHERIT,
          "inherits everything");
    while (s.profile_count < BG_SCHED_PROFILES) bg_schedule_add_profile(&s, "x");
    CHECK(bg_schedule_add_profile(&s, "y") < 0, "a ninth profile is refused");
}

/**
 * @brief Removing a profile points the cells that used it at profile 0 and renumbers the cells
 *        of the profiles after it; the last profile cannot be removed.
 * @version 1
 * @req REQ-BG-004
 */
static void test_remove_profile(void)
{
    bg_schedule_t s;
    winter_schedule(&s);
    CHECK(bg_schedule_remove_profile(&s, 1), "remove profile 1");
    CHECK(s.profile_count == 3, "three left");
    CHECK(s.grid[TUE][1] == 0, "its cells now use profile 0");
    CHECK(s.grid[TUE][2] == 1 && s.grid[TUE][3] == 2, "later profiles renumbered");
    CHECK(strcmp(s.profiles[1].name, "p2") == 0, "profiles shifted down");
    CHECK(bg_schedule_valid(&s), "valid after removal");
    bg_schedule_defaults(&s);
    CHECK(!bg_schedule_remove_profile(&s, 0), "the only profile stays");
}

/**
 * @brief Cycling a cell steps through the profiles and wraps; copying a day fills every day.
 * @version 1
 * @req REQ-BG-004
 */
static void test_cycle_and_copy(void)
{
    bg_schedule_t s;
    winter_schedule(&s);
    bg_schedule_cycle_cell(&s, MON, 3);
    CHECK(s.grid[MON][3] == 0, "profile 3 wraps to 0");
    bg_schedule_cycle_cell(&s, MON, 3);
    CHECK(s.grid[MON][3] == 1, "then 1");
    bg_schedule_copy_day(&s, MON);
    CHECK(s.grid[SUN][3] == 1 && s.grid[SAT][3] == 1, "copied to every day");
    CHECK(s.grid[SAT][0] == 0, "other cells copied too");
}

/**
 * @brief Run every check and report the tally.
 * v2: also the editor's edits (slots, profiles, cells).
 * @version 2
 * @req REQ-BG-004
 * @return 0 if every check passed, 1 otherwise.
 */
int main(void)
{
    test_defaults();
    test_slot_boundaries();
    test_wrap_uses_previous_day();
    test_validation();
    test_add_slot();
    test_remove_slot();
    test_set_slot_start();
    test_add_profile();
    test_remove_profile();
    test_cycle_and_copy();
    printf("bg_schedule: %d/%d checks passed\n", s_checks - s_failures, s_checks);
    return s_failures ? 1 : 0;
}
