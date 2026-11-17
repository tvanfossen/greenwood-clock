/**
 * @file leaf_view.h
 * @version 1
 * @brief Falling-leaves clock effect: leaf_field physics drawn on the shared particle_view.
 *        All functions require the LVGL lock.
 */
#ifndef LEAF_VIEW_H
#define LEAF_VIEW_H

#include <stdbool.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct leaf_view_t leaf_view_t;

/**
 * @brief Create a hidden, paused falling-leaves layer over @p parent.
 * @version 1
 * @req REQ-ANIM-003
 * @return New view, or NULL on allocation failure (logged).
 */
leaf_view_t *leaf_view_create(lv_obj_t *parent, int leaf_count);

/**
 * @brief Show and animate (true) or hide and freeze (false).
 * @version 1
 * @req REQ-ANIM-003
 */
void leaf_view_set_running(leaf_view_t *v, bool running);

/**
 * @brief Destroy the view and free the field.
 * @version 1
 * @req REQ-ANIM-003
 */
void leaf_view_destroy(leaf_view_t *v);

#ifdef __cplusplus
}
#endif

#endif /* LEAF_VIEW_H */
