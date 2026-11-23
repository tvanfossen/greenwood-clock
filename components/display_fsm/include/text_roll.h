/**
 * @file text_roll.h
 * @version 1
 * @brief Odometer-style text change: a set of objects slides out (up for forward, down for
 *        back) while fading, the caller swaps their text at the midpoint, and they slide back in
 *        from the other side. One roll at a time (the minimized clock is the only user).
 *        All functions require the LVGL lock.
 */
#ifndef TEXT_ROLL_H
#define TEXT_ROLL_H

#include <stdbool.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

#define TEXT_ROLL_MAX_OBJS 4

/** Called at the midpoint, with the objects back at rest: set their new text there. */
typedef void (*text_roll_swap_cb)(void *user);

/**
 * @brief Roll @p objs (up to TEXT_ROLL_MAX_OBJS) over @p ms: forward (@p back false) slides
 *        the old text up and the new in from below. A roll still running is finished first.
 * @version 1
 * @req REQ-RADAR-003
 */
void text_roll_start(lv_obj_t *const objs[], int n, bool back, uint32_t ms,
                     text_roll_swap_cb swap, void *user);

/**
 * @brief Jump a running roll to its end (swapping first if it has not yet). No-op when idle.
 * @version 1
 * @req REQ-RADAR-003
 */
void text_roll_finish(void);

/**
 * @brief True while a roll runs.
 * @version 1
 * @req REQ-RADAR-003
 * @return Whether a roll is in flight.
 */
bool text_roll_running(void);

#ifdef __cplusplus
}
#endif

#endif /* TEXT_ROLL_H */
