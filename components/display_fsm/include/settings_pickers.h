/**
 * @file settings_pickers.h
 * v2: no Global brightness.
 * @version 2
 * @brief The schedule editor's pickers, each a pushed settings screen with Done: a time (a
 *        clock time, or sunrise/sunset with an offset), a date range (month and day, plus the
 *        year for a one-off), and a brightness (or the global one). Pure LVGL. Caller holds
 *        the LVGL lock.
 */
#pragma once

#include "lvgl.h"
#include "sched_rule.h"

#ifdef __cplusplus
extern "C" {
#endif

/** A picked time. */
typedef void (*settings_time_cb_t)(sched_time_t t, void *ctx);
/** A picked date range. */
typedef void (*settings_dates_cb_t)(sched_date_t from, sched_date_t to, void *ctx);
/** A picked brightness (SCHED_INHERIT: the global one). */
typedef void (*settings_percent_cb_t)(int pct, void *ctx);

/**
 * @brief Push a time picker titled @p title starting at @p t; Done calls @p cb.
 * @version 1
 * @req REQ-BG-004
 */
void settings_pick_time(const char *title, sched_time_t t, settings_time_cb_t cb, void *ctx);

/**
 * @brief Push a date range picker (with years when @p with_year) starting at @p from - @p to;
 *        Done calls @p cb.
 * @version 1
 * @req REQ-BG-004
 */
void settings_pick_dates(sched_date_t from, sched_date_t to, bool with_year,
                         settings_dates_cb_t cb, void *ctx);

/**
 * @brief Push a brightness picker starting at @p pct (out of range: 60 %); Done calls @p cb
 *        with the value.
 * v2: no Global.
 * @version 2
 * @req REQ-BG-004
 */
void settings_pick_percent(int pct, settings_percent_cb_t cb, void *ctx);

#ifdef __cplusplus
}
#endif
