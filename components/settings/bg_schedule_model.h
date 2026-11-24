/**
 * @file bg_schedule_model.h
 * @version 1
 * @brief Background schedule model (pure C, host-testable): profiles that override global
 *        settings, chosen per weekday and time-of-day slot.
 *
 * A profile only overrides what it sets; every field left at its "inherit" value falls
 * through to the global settings. Night mode is not special: it is a profile that happens
 * to set a low brightness, no animation, a dark text colour, and so on.
 *
 * Slots share their start times across all days; each weekday picks a profile per slot.
 * A slot runs from its start until the next slot's start; the last slot runs past midnight
 * into the next day until the first slot's start (so before the first start, the PREVIOUS
 * day's last slot is still active).
 */
#ifndef BG_SCHEDULE_MODEL_H
#define BG_SCHEDULE_MODEL_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BG_SCHED_SLOTS     4            /**< Time-of-day slots (shared start times). */
#define BG_SCHED_PROFILES  8            /**< Profiles a schedule can reference. */
#define BG_SCHED_DAYS      7            /**< Weekdays, struct tm order (0 = Sunday). */
#define BG_SCHED_VERSION   2            /**< Stored layout version (2: theme replaced text_color). */
#define BG_INHERIT         (-1)         /**< Inherit marker for small integer fields. */
#define BG_THEME_LEN       16           /**< Theme name field: a file stem in /sdcard/themes. */
#define BG_MINUTES_PER_DAY (24 * 60)
#define BG_SCHED_ANIM_COUNT 4           /**< clock_anim_t values (asserted in bg_schedule.c). */

/** One background profile: each field overrides the global setting unless "inherit". */
typedef struct {
    char     name[24];     /**< Shown in the settings screen. */
    char     image[96];    /**< LVGL path ("A:/backgrounds/x.png"); "" = inherit. */
    char     theme[BG_THEME_LEN]; /**< Theme file stem ("greenwood-night"); "" = inherit. */
    int8_t   brightness;   /**< 0..100 %, or BG_INHERIT. */
    int8_t   anim;         /**< clock_anim_t value, or BG_INHERIT. */
} bg_profile_t;

/** The whole schedule (stored as one NVS blob). */
typedef struct {
    uint32_t     version;                              /**< BG_SCHED_VERSION. */
    uint8_t      slot_count;                           /**< 1..BG_SCHED_SLOTS. */
    uint8_t      profile_count;                        /**< 1..BG_SCHED_PROFILES. */
    uint16_t     slot_start[BG_SCHED_SLOTS];           /**< Minutes after midnight, ascending. */
    uint8_t      grid[BG_SCHED_DAYS][BG_SCHED_SLOTS];  /**< Profile index per weekday and slot. */
    bg_profile_t profiles[BG_SCHED_PROFILES];
} bg_schedule_t;

/**
 * @brief Whether @p name can name a theme file: 1..BG_THEME_LEN-1 characters, each a
 *        lower-case letter, a digit or '-' (so it can never leave the themes folder).
 * @version 1
 * @req REQ-UI-003
 * @return true if valid.
 */
bool bg_theme_name_valid(const char *name);

/**
 * @brief One all-day slot showing profile 0 ("Default"), which inherits everything, so the
 *        clock looks exactly as it does without a schedule.
 * @version 1
 * @req REQ-BG-004
 */
void bg_schedule_defaults(bg_schedule_t *s);

/**
 * @brief Whether @p s is safe to use: counts in range, slot starts strictly ascending and
 *        before 24:00, every grid entry a defined profile, strings terminated, brightness
 *        and animation inherit or in range.
 * @version 1
 * @req REQ-BG-004
 * @return true if valid.
 */
bool bg_schedule_valid(const bg_schedule_t *s);

/**
 * @brief Profile index active on weekday @p wday (0 = Sunday) at @p minute after midnight.
 * @version 1
 * @req REQ-BG-004
 * @return Profile index (0 for an invalid schedule).
 */
int bg_schedule_profile_at(const bg_schedule_t *s, int wday, int minute);

/**
 * @brief Append a slot halfway between the last start and midnight; each day's new cell copies
 *        its last slot.
 * @version 1
 * @req REQ-BG-004
 * @return false at BG_SCHED_SLOTS slots or if no minute is free after the last start.
 */
bool bg_schedule_add_slot(bg_schedule_t *s);

/**
 * @brief Remove slot @p i, closing the gap in the starts and every day's cells.
 * @version 1
 * @req REQ-BG-004
 * @return false for the only slot or an index out of range.
 */
bool bg_schedule_remove_slot(bg_schedule_t *s, int i);

/**
 * @brief Move slot @p i to start at @p minute, between its neighbours.
 * @version 1
 * @req REQ-BG-004
 * @return false (nothing changed) unless the starts stay strictly ascending within the day.
 */
bool bg_schedule_set_slot_start(bg_schedule_t *s, int i, int minute);

/**
 * @brief Append a profile named @p name that inherits every setting.
 * @version 1
 * @req REQ-BG-004
 * @return Its index, or -1 at BG_SCHED_PROFILES profiles.
 */
int bg_schedule_add_profile(bg_schedule_t *s, const char *name);

/**
 * @brief Remove profile @p i: its cells use profile 0, later profiles' cells are renumbered.
 * @version 1
 * @req REQ-BG-004
 * @return false for the only profile or an index out of range.
 */
bool bg_schedule_remove_profile(bg_schedule_t *s, int i);

/**
 * @brief Step cell (@p day, @p slot) to the next profile, wrapping to the first.
 * @version 1
 * @req REQ-BG-004
 */
void bg_schedule_cycle_cell(bg_schedule_t *s, int day, int slot);

/**
 * @brief Copy weekday @p day's cells to every day.
 * @version 1
 * @req REQ-BG-004
 */
void bg_schedule_copy_day(bg_schedule_t *s, int day);

#ifdef __cplusplus
}
#endif

#endif /* BG_SCHEDULE_MODEL_H */
