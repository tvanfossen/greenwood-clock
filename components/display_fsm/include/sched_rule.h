/**
 * @file sched_rule.h
 * @version 1
 * @brief Schedule profiles (prototype notation): one list where every entry is the same kind
 *        of thing — WHEN it repeats (every day, every year on a date or range, or once), the
 *        time window (all day, or from/to at a clock time or sunrise/sunset with an offset),
 *        its priority, and WHAT it shows (background, theme, clock animation, brightness;
 *        empty = the global setting). The morning/day/evening/night entries are profiles
 *        that repeat every day forever; Valentine's or a birthday repeats yearly. Pure C:
 *        the text the editor shows for a profile, no LVGL.
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
    SCHED_EVERY_DAY = 0,   /**< Every day, forever. */
    SCHED_YEARLY,          /**< Every year, on a date or a date range. */
    SCHED_ONCE,            /**< One date or range, in one year. */
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
    uint8_t        priority;       /**< Higher wins; equal priorities may not overlap. */
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
 * @brief When @p r runs, as one line: "Every day, 7:00 AM - 12:00 PM",
 *        "Every year, Dec 24 - Dec 25, all day", "Once, Jun 3, 2027, all day".
 * @version 1
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
 * @brief The file name of an LVGL path without folder or extension
 *        ("A:/backgrounds/winter_morning.png" -> "winter_morning").
 * @version 1
 * @req REQ-BG-004
 */
void sched_image_label(const char *path, char *out, size_t n);

#ifdef __cplusplus
}
#endif
