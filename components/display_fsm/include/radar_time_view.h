/**
 * @file radar_time_view.h
 * @version 2
 * @brief How the radar screen tells time: which moment the overlay shows. Two looks
 *        (RADAR_TIME_STYLE picks one, so either is a one-line switch):
 *          - RADAR_TIME_CHIP: a text chip, "Radar 6:01 PM (40m ago)".
 *          - RADAR_TIME_CLOCK: the minimized clock rolls to each loop frame's time (up going
 *            forward, down when the loop wraps; the live time on the newest); the chip only
 *            carries status and the single-image time.
 *        All functions require the LVGL lock.
 */
#ifndef RADAR_TIME_VIEW_H
#define RADAR_TIME_VIEW_H

#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    RADAR_TIME_CHIP,
    RADAR_TIME_CLOCK,
} radar_time_style_t;

/** The look the radar screen uses. */
#define RADAR_TIME_STYLE RADAR_TIME_CLOCK

typedef struct radar_time_view_t radar_time_view_t;

/**
 * @brief Create the indicator in @p parent (the radar view's container).
 * @version 2
 * @req REQ-RADAR-003
 * @return The view, or NULL if allocation failed.
 */
radar_time_view_t *radar_time_view_create(lv_obj_t *parent, radar_time_style_t style);

/**
 * @brief Delete the indicator (and release the clock if it held a frame time). NULL-safe.
 * @version 2
 * @req REQ-RADAR-003
 */
void radar_time_view_destroy(radar_time_view_t *tv);

/**
 * @brief Show a status line instead of a time ("Radar loading...", "Radar data expired").
 * @version 2
 * @req REQ-RADAR-003
 */
void radar_time_view_message(radar_time_view_t *tv, const char *text);

/**
 * @brief Show a single radar image (no loop) taken at @p ts.
 * @version 2
 * @req REQ-RADAR-003
 */
void radar_time_view_latest(radar_time_view_t *tv, time_t ts);

/**
 * @brief Show the loop frame taken at @p ts, the clock look rolling to it after @p delay_ms.
 *        @p newest is the most recent frame.
 * @version 2
 * @req REQ-RADAR-003
 */
void radar_time_view_frame(radar_time_view_t *tv, time_t ts, bool newest, uint32_t delay_ms);

#ifdef __cplusplus
}
#endif

#endif /* RADAR_TIME_VIEW_H */
