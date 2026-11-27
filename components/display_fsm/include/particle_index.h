/**
 * @file particle_index.h
 * @version 1
 * @brief Which particles can touch a redraw area, without testing all of them: their boxes
 *        sorted into horizontal bands once a frame, then each area looks only at the bands it
 *        covers. Snowfall redraws ~126 small areas a frame and tested all ~140 flakes for each
 *        (~7.5 ms a frame on device). The result is the same particles in the same (index)
 *        order as a full scan, so drawing is unchanged. Pure C: host-tested.
 */
#pragma once

#include "lvgl.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PARTICLE_INDEX_MAX   192    /**< Particles indexed (PARTICLE_VIEW_MAX). */
#define PARTICLE_INDEX_BAND  32     /**< Band height, px. */
#define PARTICLE_INDEX_BANDS 20     /**< Bands: 640 px of screen (taller boxes are clamped). */
#define PARTICLE_INDEX_SLOTS (PARTICLE_INDEX_MAX * 4)   /**< Band memberships it holds. */

/** The bands: CSR lists of particle indices, and a per-query visit stamp. */
typedef struct {
    int      n;                                   /**< Particles indexed; -1: too many memberships (scan all). */
    int      count;                               /**< Particles given (what a scan covers). */
    uint16_t start[PARTICLE_INDEX_BANDS + 1];     /**< Band b's members are idx[start[b] .. start[b+1]). */
    uint8_t  idx[PARTICLE_INDEX_SLOTS];
    uint16_t seen[PARTICLE_INDEX_MAX];            /**< Last query that took particle i. */
    uint16_t stamp;
} particle_index_t;

/**
 * @brief Index @p n boxes (screen coordinates).
 * @version 1
 * @req REQ-ANIM-004
 */
void particle_index_build(particle_index_t *x, const lv_area_t *boxes, int n);

/**
 * @brief The particles whose box intersects @p clip, in ascending index order, into @p out
 *        (room for PARTICLE_INDEX_MAX).
 * @version 1
 * @req REQ-ANIM-004
 * @return How many.
 */
int particle_index_query(particle_index_t *x, const lv_area_t *boxes, const lv_area_t *clip, uint8_t *out);

#ifdef __cplusplus
}
#endif
