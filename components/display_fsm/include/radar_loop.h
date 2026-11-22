/**
 * @file radar_loop.h
 * @version 1
 * @brief Radar loop: plays the SD radar history (past hour) on the radar screen.
 *
 * Threading: radar_loop_load_next() runs on the FSM task OFF the LVGL lock (SD read +
 * PNG decode); radar_loop_arm(), radar_loop_attach() and radar_loop_stop() run under the
 * LVGL lock. Frames decoded off-lock become visible to the playback timer only when
 * radar_loop_attach() publishes them under the lock.
 */
#ifndef RADAR_LOOP_H
#define RADAR_LOOP_H

#include <stdbool.h>
#include "display_widgets.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Start a loop session (no I/O): forget any previous frames' bookkeeping.
 * @version 1
 * @req REQ-RADAR-003
 */
void radar_loop_arm(void);

/**
 * @brief Off the LVGL lock: list the history on first call, then decode the next frame.
 * @version 1
 * @req REQ-RADAR-003
 * @return true if more frames remain to decode.
 */
bool radar_loop_load_next(void);

/**
 * @brief Under the lock: publish the decoded frames and start playback on @p rv once at
 *        least two frames are ready.
 * @version 1
 * @req REQ-RADAR-003
 */
void radar_loop_attach(radar_view_t *rv);

/**
 * @brief Under the lock: stop playback and free every frame. Call after the view that
 *        showed the frames is destroyed.
 * @version 1
 * @req REQ-RADAR-003
 */
void radar_loop_stop(void);

#ifdef __cplusplus
}
#endif

#endif /* RADAR_LOOP_H */
