/**
 * @file particle_view.h
 * @version 1
 * @brief Shared LVGL renderer for physics particle fields (snowfall, falling leaves).
 *
 * The base owns everything that is not specific to one effect: the full-screen
 * click-through object, the frame timer, dt clamping, per-particle old∪new dirty
 * rectangles, clip-area culling, run/pause and perf logging. An effect supplies
 * only its model and four callbacks. All functions require the LVGL lock.
 */
#ifndef PARTICLE_VIEW_H
#define PARTICLE_VIEW_H

#include <stdbool.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Upper bound on particles per view (dirty-rect scratch is sized to this). */
#define PARTICLE_VIEW_MAX 192   /* = PARTICLE_INDEX_MAX (particle_index.h) */

/** Effect-specific behaviour; @p model is the effect's own state. */
typedef struct {
    const char *name;                                            /**< Log tag suffix. */
    int  (*count)(void *model);                                  /**< Live particles. */
    void (*step)(void *model, float dt_s);                       /**< Advance physics. */
    void (*bounds)(void *model, int i, lv_area_t *out);          /**< Screen bbox of particle i. */
    void (*draw)(void *model, int i, lv_layer_t *layer);         /**< Draw particle i. */
} particle_view_ops_t;

typedef struct particle_view_t particle_view_t;

/**
 * @brief Create a hidden, paused view over @p parent driving @p model through @p ops.
 * @version 1
 * @req REQ-ANIM-004
 * @return New view, or NULL on allocation failure (logged).
 */
particle_view_t *particle_view_create(lv_obj_t *parent, const particle_view_ops_t *ops, void *model);

/**
 * @brief Show and animate (true) or hide and freeze (false).
 * @version 1
 * @req REQ-ANIM-004
 */
void particle_view_set_running(particle_view_t *v, bool running);

/**
 * @brief Delete the object and timer and free the view (the model is the caller's).
 * @version 1
 * @req REQ-ANIM-004
 */
void particle_view_destroy(particle_view_t *v);

#ifdef __cplusplus
}
#endif

#endif /* PARTICLE_VIEW_H */
