/**
 * @file bg_schedule_model.c
 * @version 1
 * @brief Background schedule model: defaults, validation and slot/profile resolution.
 *        Pure C (no ESP-IDF), covered by tests/host/test_bg_schedule.c.
 */
#include "bg_schedule_model.h"

#include <string.h>

/**
 * @brief One all-day slot showing profile 0 ("Default"), which inherits everything, so the
 *        clock looks exactly as it does without a schedule.
 * @version 1
 * @req REQ-BG-004
 */
void bg_schedule_defaults(bg_schedule_t *s)
{
    memset(s, 0, sizeof(*s));
    s->version = BG_SCHED_VERSION;
    s->slot_count = 1;
    s->profile_count = 1;
    for (int i = 0; i < BG_SCHED_PROFILES; i++) {
        s->profiles[i].text_color = BG_INHERIT_COLOR;
        s->profiles[i].brightness = BG_INHERIT;
        s->profiles[i].anim = BG_INHERIT;
    }
    strcpy(s->profiles[0].name, "Default");
}

/**
 * @brief Whether a fixed-size char field holds a NUL-terminated string.
 * @version 1
 * @dg_internal
 * @return true if terminated within @p n bytes.
 */
static bool terminated(const char *str, size_t n)
{
    return memchr(str, '\0', n) != NULL;
}

/**
 * @brief Whether one profile's strings are terminated and its overrides are in range.
 * @version 1
 * @dg_internal
 * @return true if valid.
 */
static bool profile_valid(const bg_profile_t *p)
{
    bool ok = terminated(p->name, sizeof(p->name)) && terminated(p->image, sizeof(p->image));
    ok = ok && (p->brightness == BG_INHERIT || (p->brightness >= 0 && p->brightness <= 100));
    ok = ok && (p->anim == BG_INHERIT || (p->anim >= 0 && p->anim < BG_SCHED_ANIM_COUNT));
    return ok && (p->text_color == BG_INHERIT_COLOR || p->text_color <= 0xFFFFFFu);
}

/**
 * @brief Whether the slot starts are strictly ascending and before 24:00.
 * @version 1
 * @dg_internal
 * @return true if valid.
 */
static bool slots_valid(const bg_schedule_t *s)
{
    bool ok = true;
    for (int i = 0; i < s->slot_count; i++) {
        ok = ok && s->slot_start[i] < BG_MINUTES_PER_DAY;
        ok = ok && (i == 0 || s->slot_start[i] > s->slot_start[i - 1]);
    }
    return ok;
}

/**
 * @brief Whether every used grid entry names a defined profile.
 * @version 1
 * @dg_internal
 * @return true if valid.
 */
static bool grid_valid(const bg_schedule_t *s)
{
    bool ok = true;
    for (int d = 0; d < BG_SCHED_DAYS; d++) {
        for (int i = 0; i < s->slot_count; i++) ok = ok && s->grid[d][i] < s->profile_count;
    }
    return ok;
}

/**
 * @brief Whether @p s is safe to use: counts in range, slot starts strictly ascending and
 *        before 24:00, every grid entry a defined profile, strings terminated, brightness
 *        and animation inherit or in range.
 * @version 1
 * @req REQ-BG-004
 * @return true if valid.
 */
bool bg_schedule_valid(const bg_schedule_t *s)
{
    bool ok = s && s->version == BG_SCHED_VERSION;
    ok = ok && s->slot_count >= 1 && s->slot_count <= BG_SCHED_SLOTS;
    ok = ok && s->profile_count >= 1 && s->profile_count <= BG_SCHED_PROFILES;
    ok = ok && slots_valid(s) && grid_valid(s);
    for (int i = 0; ok && i < s->profile_count; i++) ok = profile_valid(&s->profiles[i]);
    return ok;
}

/**
 * @brief Profile index active on weekday @p wday (0 = Sunday) at @p minute after midnight.
 * @version 1
 * @req REQ-BG-004
 * @return Profile index (0 for an invalid schedule).
 */
int bg_schedule_profile_at(const bg_schedule_t *s, int wday, int minute)
{
    if (!bg_schedule_valid(s) || wday < 0 || wday >= BG_SCHED_DAYS) return 0;
    int slot = s->slot_count - 1;   // before the first start: yesterday's last slot runs on
    int day = (minute < s->slot_start[0]) ? (wday + BG_SCHED_DAYS - 1) % BG_SCHED_DAYS : wday;
    for (int i = 0; i < s->slot_count; i++) {
        if (s->slot_start[i] <= minute) slot = i;
    }
    return s->grid[day][slot];
}
