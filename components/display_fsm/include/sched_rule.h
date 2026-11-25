/**
 * @file sched_rule.h
 * v2: no priority: daily profiles (the clock's default state) may not overlap each other,
 *     events may not overlap each other, and an event always replaces the daily profile.
 * v3: every field explicit (sched_rule_fill fills old "global" ones once); sched_rule_gap.
 * @version 3
 * @brief Schedule profiles (prototype notation). Two kinds, one type: DAILY profiles repeat
 *        every day forever and are the clock's default state (morning, afternoon, evening,
 *        night); EVENTS repeat every year on a date or range, or happen once (Valentine's,
 *        Christmas, a birthday) and replace the daily profile while they run. Each has a
 *        time window (all day, or from/to at a clock time or sunrise/sunset with an offset)
 *        and WHAT it shows (background, theme, clock animation, brightness; empty = the
 *        global setting). Pure C: the text the editor shows for a profile, no LVGL.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** How a profile repeats. */
typedef enum {
    SCHED_EVERY_DAY = 0,   /**< Daily: every day, forever (the default state). */
    SCHED_YEARLY,          /**< Event: every year, on a date or a date range. */
    SCHED_ONCE,            /**< Event: one date or range, in one year. */
} sched_repeat_t;

/** What a window edge is measured from. */
typedef enum {
    SCHED_AT_CLOCK = 0,    /**< A time of day: minutes after midnight. */
    SCHED_AT_SUNRISE,      /**< Sunrise, plus an offset in minutes (negative = before). */
    SCHED_AT_SUNSET,       /**< Sunset, plus an offset. */
} sched_anchor_t;

/** One edge of a time window. */
typedef struct {
    sched_anchor_t anchor;
    int16_t        min;    /**< Minutes after midnight (clock), or the offset (sun). */
} sched_time_t;

/** A calendar date; year is used only by SCHED_ONCE. */
typedef struct {
    uint16_t year;
    uint8_t  month;        /**< 1..12 */
    uint8_t  day;          /**< 1..31 */
} sched_date_t;

#define SCHED_NAME_LEN  24
#define SCHED_IMAGE_LEN 96
#define SCHED_THEME_LEN 16
#define SCHED_ANIM_LEN  24
#define SCHED_INHERIT   (-1)   /**< Brightness: the global setting. */

/** One schedule profile. */
typedef struct {
    char           name[SCHED_NAME_LEN];
    sched_repeat_t repeat;
    sched_date_t   from, to;       /**< Yearly / once: the first and last day (equal: one day). */
    bool           all_day;
    sched_time_t   start, end;     /**< When not all day; end before start runs past midnight. */
    char           image[SCHED_IMAGE_LEN];   /**< LVGL path; "" = global. */
    char           theme[SCHED_THEME_LEN];   /**< Theme stem; "" = global. */
    char           anim[SCHED_ANIM_LEN];     /**< Clock animation name; "" = global. */
    int8_t         brightness;               /**< 0..100, or SCHED_INHERIT. */
} sched_rule_t;

/**
 * @brief A window edge as text: "7:00 AM", "Sunset", "Sunset + 1h", "Sunrise - 30m".
 * @version 1
 * @req REQ-BG-004
 */
void sched_fmt_time(const sched_time_t *t, char *out, size_t n);

/**
 * @brief When @p r runs, as one line: "7:00 AM - 12:00 PM" (daily: the window only),
 *        "Every year, Dec 24 - Dec 25, all day", "Once, Jun 3, 2027, all day".
 * v2: a daily profile is just its window (the list's Daily section says the rest).
 * @version 2
 * @req REQ-BG-004
 */
void sched_fmt_when(const sched_rule_t *r, char *out, size_t n);

/**
 * @brief The dates of @p r ("Feb 14", "Dec 24 - Dec 25", "Jun 3, 2027"); "" every day.
 * @version 1
 * @req REQ-BG-004
 */
void sched_fmt_dates(const sched_rule_t *r, char *out, size_t n);

/**
 * @brief What @p r shows, as one line: "winter_morning, Default, Snowfall, 60%", the
 *        fields it leaves to the global settings left out ("Global settings" when all are).
 * @version 1
 * @req REQ-BG-004
 */
void sched_fmt_look(const sched_rule_t *r, char *out, size_t n);

/**
 * @brief A theme's display label from its file stem ("default" -> "Default").
 * @version 1
 * @req REQ-BG-004
 */
void sched_theme_label(const char *stem, char *out, size_t n);

/**
 * @brief The theme file stem for a theme named @p name: lower-case letters and digits kept,
 *        each run of anything else one '-', no '-' at either end, at most SCHED_THEME_LEN - 1
 *        characters ("Rose Gold" -> "rose-gold").
 * @version 1
 * @req REQ-UI-004
 * @return false when nothing usable is left, or it is "default" (the built-in theme's file).
 */
bool sched_theme_stem(const char *name, char *out, size_t n);

/**
 * @brief The file name of an LVGL path without folder or extension
 *        ("A:/backgrounds/winter_morning.png" -> "winter_morning").
 * @version 1
 * @req REQ-BG-004
 */
void sched_image_label(const char *path, char *out, size_t n);

/**
 * @brief Give @p r's unset fields (empty image, theme or animation, inherited brightness)
 *        @p with's values: profiles set every field, and this fills the ones older
 *        schedules left to the global settings.
 * @version 1
 * @req REQ-BG-004
 * @return Whether any field was filled.
 */
bool sched_rule_fill(sched_rule_t *r, const sched_rule_t *with);

/**
 * @brief What a gap shows (no profile covers the moment): the built-in Default theme, no
 *        picture, no clock animation, the stored brightness. Only the look fields are set.
 * @version 1
 * @req REQ-BG-004
 */
void sched_rule_gap(sched_rule_t *out);

#ifdef __cplusplus
}
#endif
