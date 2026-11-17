/**
 * @file leaf_field.h
 * @version 1
 * @brief A screen-wide field of falling leaves driven by leaf_physics (Andersen et al. 2005b).
 *
 * Maps each leaf's non-dimensional motion to screen pixels with the same pinhole
 * depth projection as the snowfall (Langer et al. 2004). Pure C, host-tested.
 */
#ifndef LEAF_FIELD_H
#define LEAF_FIELD_H

#include <stdint.h>
#include "leaf_physics.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LEAF_FIELD_MAX 32
#define LEAF_PALETTE_SIZE 5

/** Field configuration; fill with leaf_params_default(). Units noted per field. */
typedef struct {
    int   screen_w_px, screen_h_px;
    float focal_px;             /**< Pinhole focal length, px (design, same as snowfall). */
    float z_near_m, z_far_m;    /**< Depth range, m (design). */
    float semi_chord_m;         /**< a: half the leaf length, m (eq. 4.1 length scale). */
    float u_mps;                /**< U = sqrt((rho_s/rho_f - 1) g b), m/s (eq. 4.1). */
    float i_star_min, i_star_max;  /**< Per-leaf I* range (design: flutter-dominated). */
    float size_gain;            /**< Drawn-size exaggeration only; motion is unscaled. */
    float dt_nd;                /**< Integration step, non-dimensional time. */
} leaf_params_t;

/** One leaf: physics state plus its screen mapping. */
typedef struct {
    leaf_state_t s;
    float   i_star;
    float   z_m;
    float   px_per_nd;    /**< Screen pixels per non-dimensional length at this depth. */
    float   half_len_px;  /**< Drawn half-length. */
    float   x0_px, y0_px; /**< Screen position of the physics origin. */
    uint8_t opa;
    uint8_t color;        /**< Index into the palette (0..LEAF_PALETTE_SIZE-1). */
} leaf_t;

typedef struct {
    leaf_params_t p;
    leaf_coeffs_t c;
    uint32_t      rng;
    int           count;
    leaf_t        leaves[LEAF_FIELD_MAX];
} leaf_field_t;

/**
 * @brief Gentle autumn defaults for a W x H screen (see CITATIONS.md for sources).
 * @version 1
 * @req REQ-ANIM-003
 */
void leaf_params_default(leaf_params_t *p, int screen_w_px, int screen_h_px);

/**
 * @brief Seed @p count leaves in settled motion, spread over the screen.
 * @version 1
 * @req REQ-ANIM-003
 * @return Leaves created (clamped to LEAF_FIELD_MAX).
 */
int leaf_field_init(leaf_field_t *f, const leaf_params_t *p, int count, uint32_t seed);

/**
 * @brief Advance all leaves by @p dt_s seconds; respawn at the top, wrap horizontally.
 * @version 1
 * @req REQ-ANIM-003
 */
void leaf_field_step(leaf_field_t *f, float dt_s);

/**
 * @brief Screen centre and on-screen rotation of leaf @p i.
 * @version 1
 * @req REQ-ANIM-003
 */
void leaf_screen_pose(const leaf_field_t *f, int i, float *x_px, float *y_px, float *angle_rad);

#ifdef __cplusplus
}
#endif

#endif /* LEAF_FIELD_H */
