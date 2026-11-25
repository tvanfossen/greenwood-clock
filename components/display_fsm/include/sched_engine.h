/**
 * @file sched_engine.h
 * @version 1
 * @brief The schedule engine: which profile is showing at a local date and minute, and the
 *        overlap check that refuses a save. Daily profiles are the default state and may not
 *        overlap each other; events (yearly or once) may not overlap each other and replace
 *        the daily profile while they run; nothing showing means the global settings. A
 *        window past midnight belongs to the day it starts. Sun-anchored edges come from a
 *        caller-supplied sun (the device's location, or a fixed one in tests). Pure C, works
 *        in local calendar dates and minutes (no time_t, no time zone).
 */
#pragma once

#include "sched_rule.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Sunrise and sunset of one day, local minutes after midnight. */
typedef struct {
    int rise_min;
    int set_min;
} sched_sun_t;

/** The sun on local date @p d. */
typedef sched_sun_t (*sched_sun_fn)(sched_date_t d, void *ctx);

/** A sun source: the function and its context. */
typedef struct {
    sched_sun_fn fn;
    void        *ctx;
} sched_sun_src_t;

/** Where a candidate profile first collides with another of its kind. */
typedef struct {
    int          other;        /**< Index of the profile it collides with. */
    sched_date_t first, last;  /**< The first run of days it collides on. */
    int          from_min;     /**< The collision's minutes on the first day. */
    int          to_min;
    bool         every_day;    /**< It collides on every day checked. */
} sched_overlap_t;

/**
 * @brief The profile of @p r (@p n of them) showing on local date @p day at minute @p min:
 *        an event covering it, else a daily profile covering it.
 * @version 1
 * @req REQ-BG-004
 * @return Its index, or -1 when none (the global settings).
 */
int sched_active(const sched_rule_t *r, int n, sched_date_t day, int min,
                 const sched_sun_src_t *sun);

/**
 * @brief Whether profile @p cand collides with another profile of its kind (daily with
 *        daily, event with event) on any of the @p days days from @p from.
 * @version 1
 * @req REQ-BG-004
 * @return True with @p out set to the first collision.
 */
bool sched_find_overlap(const sched_rule_t *r, int n, int cand, sched_date_t from, int days,
                        const sched_sun_src_t *sun, sched_overlap_t *out);

/**
 * @brief The editor's reason for refusing a save: "Overlaps Christmas on Dec 24 - Dec 25";
 *        for daily profiles the times ("Overlaps Morning from 11:00 AM to 12:00 PM"), with
 *        the dates when it is not every day.
 * @version 1
 * @req REQ-BG-004
 */
void sched_fmt_overlap(const sched_rule_t *r, const sched_overlap_t *o, char *out, size_t n);

#ifdef __cplusplus
}
#endif
