/**
 * @file snow_physics.h
 * @brief Platform-independent physics for the snowfall clock animation.
 *
 * Pure C, no LVGL / ESP-IDF dependency, so it builds and is unit-tested on
 * the host (tests/host/test_snow_physics.c). See CITATIONS.md for the sources
 * behind every constant below; parameters that are artistic choices rather
 * than measured physics are marked "design".
 */
#ifndef SNOW_PHYSICS_H
#define SNOW_PHYSICS_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Hard upper bound on flakes in one field (fixed storage, no heap per flake). */
#define SNOW_MAX_FLAKES 192

/**
 * Terminal fall speed fit for unrimed aggregates, V = A * D^B
 * (V in m/s, D = maximum dimension in mm). Locatelli & Hobbs (1974), Fig. 20.
 */
#define SNOW_FALL_SPEED_A 0.81f
#define SNOW_FALL_SPEED_B 0.16f

/** Configuration for a snow field. Fill with snow_params_default(). */
typedef struct {
    int   screen_w_px;       /**< Viewport width in pixels. */
    int   screen_h_px;       /**< Viewport height in pixels. */
    float focal_px;          /**< Pinhole focal length in pixels (design). */
    float z_near_m;          /**< Nearest flake depth in metres (design). */
    float z_far_m;           /**< Farthest flake depth in metres (design). */
    float d_min_mm;          /**< Smallest flake maximum dimension, mm. */
    float d_max_mm;          /**< Largest flake maximum dimension, mm. */
    float lambda_per_mm;     /**< Exponential size-distribution slope, 1/mm (design). */
    float wind_mps;          /**< Constant horizontal wind, m/s, +right (design). */
    float sway_sigma_min_mps;/**< Min horizontal-velocity std-dev, m/s (Kajikawa 1989). */
    float sway_sigma_max_mps;/**< Max horizontal-velocity std-dev, m/s (Kajikawa 1989). */
    float sway_period_min_s; /**< Min spiral period, s (design). */
    float sway_period_max_s; /**< Max spiral period, s (design). */
    float steady_fraction;   /**< Fraction of flakes falling without sway (Kajikawa 1982). */
    float vjitter_rel;       /**< Relative std-dev of vertical speed (Kajikawa 1989). */
    float size_gain;         /**< Visual exaggeration of flake size on screen (design). */
    float radius_min_px;     /**< Clamp: smallest drawn radius. */
    float radius_max_px;     /**< Clamp: largest drawn radius. */
} snow_params_t;

/** One flake. Positions are screen pixels of the flake centre. */
typedef struct {
    float x_px;
    float y_px;
    float z_m;            /**< Depth from the camera. */
    float d_mm;           /**< Maximum dimension. */
    float v_mps;          /**< Terminal fall speed from snow_fall_speed_mps(). */
    float sway_amp_mps;   /**< Peak horizontal sway velocity (0 = steady faller). */
    float sway_omega;     /**< Sway angular frequency, rad/s. */
    float sway_phase;     /**< Sway phase, rad. */
    float vjit_omega;     /**< Vertical-jitter angular frequency, rad/s. */
    float radius_px;      /**< Drawn radius. */
    uint8_t opa;          /**< Drawn opacity 0..255 (depth fade). */
} snow_flake_t;

/** A field of flakes plus its RNG and clock. */
typedef struct {
    snow_params_t p;
    uint32_t      rng;
    float         t_s;
    int           count;
    snow_flake_t  flakes[SNOW_MAX_FLAKES];
} snow_field_t;

/**
 * @brief Fill @p p with the default gentle-snowfall parameters for a W x H screen.
 * @version 1
 * @req REQ-SNOW-001
 */
void snow_params_default(snow_params_t *p, int screen_w_px, int screen_h_px);

/**
 * @brief Terminal fall speed of an unrimed aggregate (Locatelli & Hobbs 1974, Fig. 20).
 * @version 1
 * @req REQ-SNOW-001
 * @return Fall speed in m/s for maximum dimension @p d_mm (mm).
 */
float snow_fall_speed_mps(float d_mm);

/**
 * @brief Draw a diameter from an exponential distribution truncated to [d_min, d_max].
 * @version 1
 * @req REQ-SNOW-001
 * @return Maximum dimension in mm.
 */
float snow_sample_diameter_mm(uint32_t *rng, float lambda_per_mm, float d_min_mm, float d_max_mm);

/**
 * @brief Seed a field with @p count flakes spread over the whole screen.
 * @version 1
 * @req REQ-SNOW-001
 * @return Number of flakes actually created (clamped to SNOW_MAX_FLAKES).
 */
int snow_field_init(snow_field_t *f, const snow_params_t *p, int count, uint32_t seed);

/**
 * @brief Advance every flake by @p dt_s seconds; flakes leaving the bottom respawn at the top.
 * @version 1
 * @req REQ-SNOW-001
 */
void snow_field_step(snow_field_t *f, float dt_s);

/**
 * @brief Screen-space speed scale at depth @p z_m: pixels per metre (pinhole projection).
 * @version 1
 * @req REQ-SNOW-001
 * @return focal_px / z_m.
 */
float snow_px_per_m(const snow_params_t *p, float z_m);

#ifdef __cplusplus
}
#endif

#endif /* SNOW_PHYSICS_H */
