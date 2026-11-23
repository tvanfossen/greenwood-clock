/**
 * @file radar_time_view.h
 * @version 1
 * @brief How the radar screen tells time: which moment the overlay shows and where that sits in
 *        the loop. One interface, two looks (RADAR_TIME_STYLE picks one, so either is a
 *        one-line switch):
 *          - RADAR_TIME_CHIP: a text chip, "Radar 6:01 PM (40m ago)".
 *          - RADAR_TIME_DIAL: the minimized clock shows the frame's time and a ring beside it
 *            fills oldest -> newest, one dot per frame, its hand sweeping with the crossfade,
 *            the frame's age in the middle.
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
    RADAR_TIME_DIAL,
} radar_time_style_t;

/** The look the radar screen uses. */
#define RADAR_TIME_STYLE RADAR_TIME_DIAL

/** Loop positions are permille of the loop's time span (0 = oldest frame, 1000 = newest). */
#define RADAR_TIME_SPAN 1000

typedef struct radar_time_view_t radar_time_view_t;

/**
 * @brief Create the indicator in @p parent (the radar view's container).
 * @version 1
 * @req REQ-RADAR-003
 * @return The view, or NULL if allocation failed.
 */
radar_time_view_t *radar_time_view_create(lv_obj_t *parent, radar_time_style_t style);

/**
 * @brief Delete the indicator (and release the clock if it held a frame time). NULL-safe.
 * @version 1
 * @req REQ-RADAR-003
 */
void radar_time_view_destroy(radar_time_view_t *tv);

/**
 * @brief Show a status line instead of a time ("Radar loading...", "Radar data expired").
 * @version 1
 * @req REQ-RADAR-003
 */
void radar_time_view_message(radar_time_view_t *tv, const char *text);

/**
 * @brief Show a single radar image (no loop) taken at @p ts.
 * @version 1
 * @req REQ-RADAR-003
 */
void radar_time_view_latest(radar_time_view_t *tv, time_t ts);

/**
 * @brief Set the loop's frames: their positions (0..RADAR_TIME_SPAN, oldest first).
 * @version 1
 * @req REQ-RADAR-003
 */
void radar_time_view_frames(radar_time_view_t *tv, const int32_t *pos, int count);

/**
 * @brief Move to the loop frame taken at @p ts, at position @p pos, over @p fade_ms (the
 *        overlay crossfade's duration). @p newest is the most recent frame.
 * @version 1
 * @req REQ-RADAR-003
 */
void radar_time_view_frame(radar_time_view_t *tv, time_t ts, int32_t pos, uint32_t fade_ms,
                           bool newest);

#ifdef __cplusplus
}
#endif

#endif /* RADAR_TIME_VIEW_H */
