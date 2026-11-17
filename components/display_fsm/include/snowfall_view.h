/**
 * @file snowfall_view.h
 * @brief LVGL renderer for the physics-based snowfall (snow_physics.h).
 *
 * One full-screen, click-through object draws every flake in its DRAW_MAIN
 * event and invalidates only each flake's old∪new rectangle per frame.
 * All functions must be called with the LVGL lock held.
 */
#ifndef SNOWFALL_VIEW_H
#define SNOWFALL_VIEW_H

#include <stdbool.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct snowfall_view_t snowfall_view_t;

/**
 * @brief Create a snowfall layer covering @p parent; starts hidden and paused.
 * @version 1
 * @req REQ-SNOW-002
 * @return New view, or NULL on allocation failure (logged).
 */
snowfall_view_t *snowfall_view_create(lv_obj_t *parent, int flake_count);

/**
 * @brief Show and animate (true) or hide and freeze (false) the snowfall.
 * @version 1
 * @req REQ-SNOW-002
 */
void snowfall_view_set_running(snowfall_view_t *v, bool running);

/**
 * @brief Delete the LVGL object and timer and free the view.
 * @version 1
 * @req REQ-SNOW-002
 */
void snowfall_view_destroy(snowfall_view_t *v);

#ifdef __cplusplus
}
#endif

#endif /* SNOWFALL_VIEW_H */
