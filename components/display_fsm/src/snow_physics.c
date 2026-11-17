/**
 * @file snow_physics.c
 * @version 1
 * @brief Platform-independent snowfall physics. Sources: CITATIONS.md.
 *
 * Model, per flake:
 *   - size D (mm): exponential distribution, Gunn & Marshall (1958) form,
 *     truncated to [d_min, d_max].
 *   - fall speed V = 0.81 * D^0.16 m/s, Locatelli & Hobbs (1974) Fig. 20.
 *   - vertical speed jitter of a few percent and horizontal spiral sway with
 *     velocity std-dev 1-8 cm/s; ~80% of flakes sway, the rest fall steadily.
 *     Kajikawa (1982, 1989).
 *   - world -> screen by pinhole projection: image speed and image size both
 *     scale as focal/z, the image-plane relation of Langer et al. (2004), so
 *     depth produces parallax.
 */
#include "snow_physics.h"
#include "particle_rng.h"

#include <math.h>
#include <string.h>

#define TWO_PI 6.28318530718f
#define SQRT2  1.41421356237f

/**
 * @brief Clamp @p v to [lo, hi].
 * @version 1
 * @dg_internal
 * @return Clamped value.
 */
static float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

/**
 * @brief Default gentle-snowfall parameters; see snow_physics.h and CITATIONS.md.
 * @version 1
 * @req REQ-ANIM-002
 */
void snow_params_default(snow_params_t *p, int screen_w_px, int screen_h_px)
{
    memset(p, 0, sizeof(*p));
    p->screen_w_px        = screen_w_px;
    p->screen_h_px        = screen_h_px;
    p->focal_px           = 643.0f;   /* design: ~50 deg vertical FOV over 600 px */
    p->z_near_m           = 4.0f;     /* design */
    p->z_far_m            = 14.0f;    /* design */
    p->d_min_mm           = 1.0f;     /* within Locatelli & Hobbs' 1-14 mm aggregate range */
    p->d_max_mm           = 10.0f;
    p->lambda_per_mm      = 0.35f;    /* design: slope of the Gunn & Marshall exponential */
    p->wind_mps           = 0.15f;    /* design: barely-there breeze */
    p->sway_sigma_min_mps = 0.01f;    /* Kajikawa (1989): horizontal velocity sd 1-8 cm/s */
    p->sway_sigma_max_mps = 0.08f;
    p->sway_period_min_s  = 2.5f;     /* design */
    p->sway_period_max_s  = 5.0f;     /* design */
    p->steady_fraction    = 0.20f;    /* Kajikawa (1982): ~80% spiral/rotation */
    p->vjitter_rel        = 0.04f;    /* Kajikawa (1989): 3-5% of mean vertical speed */
    p->size_gain          = 10.0f;    /* design: real flakes are sub-pixel at these depths */
    p->radius_min_px      = 1.0f;
    p->radius_max_px      = 6.0f;
}

/**
 * @brief Locatelli & Hobbs (1974) Fig. 20 unrimed-aggregate fall speed.
 * @version 1
 * @req REQ-ANIM-002
 * @return Fall speed in m/s.
 */
float snow_fall_speed_mps(float d_mm)
{
    return SNOW_FALL_SPEED_A * powf(d_mm, SNOW_FALL_SPEED_B);
}

/**
 * @brief Truncated-exponential diameter sample (Gunn & Marshall 1958 form).
 * @version 1
 * @req REQ-ANIM-002
 * @return Maximum dimension in mm.
 */
float snow_sample_diameter_mm(uint32_t *rng, float lambda_per_mm, float d_min_mm, float d_max_mm)
{
    /* Inverse CDF of an exponential truncated to [d_min, d_max]. */
    float span = d_max_mm - d_min_mm;
    float tail = 1.0f - expf(-lambda_per_mm * span);
    float d = d_min_mm - logf(1.0f - prng_unit(rng) * tail) / lambda_per_mm;
    return clampf(d, d_min_mm, d_max_mm);
}

/**
 * @brief Pinhole projection scale at depth z.
 * @version 1
 * @req REQ-ANIM-002
 * @return Pixels per metre.
 */
float snow_px_per_m(const snow_params_t *p, float z_m)
{
    return p->focal_px / z_m;
}

/**
 * @brief Derive drawn radius and depth-faded opacity from a flake's size and depth.
 * @version 1
 * @dg_internal
 */
static void flake_update_appearance(const snow_params_t *p, snow_flake_t *fl)
{
    float r = p->size_gain * fl->d_mm * 1e-3f * snow_px_per_m(p, fl->z_m);
    fl->radius_px = clampf(r, p->radius_min_px, p->radius_max_px);
    float depth01 = (fl->z_m - p->z_near_m) / (p->z_far_m - p->z_near_m);
    fl->opa = (uint8_t)(255.0f - 145.0f * clampf(depth01, 0.0f, 1.0f));  /* aerial-perspective fade */
}

/**
 * @brief Draw a fresh size, depth, speed and sway for a flake (position untouched).
 * @version 1
 * @dg_internal
 */
static void flake_randomise(snow_field_t *f, snow_flake_t *fl)
{
    const snow_params_t *p = &f->p;
    fl->z_m  = prng_range(&f->rng, p->z_near_m, p->z_far_m);
    fl->d_mm = snow_sample_diameter_mm(&f->rng, p->lambda_per_mm, p->d_min_mm, p->d_max_mm);
    fl->v_mps = snow_fall_speed_mps(fl->d_mm);
    bool steady = prng_unit(&f->rng) < p->steady_fraction;
    float sigma = prng_range(&f->rng, p->sway_sigma_min_mps, p->sway_sigma_max_mps);
    fl->sway_amp_mps = steady ? 0.0f : sigma * SQRT2;  /* sinusoid peak = sqrt(2) * std-dev */
    fl->sway_omega = TWO_PI / prng_range(&f->rng, p->sway_period_min_s, p->sway_period_max_s);
    fl->sway_phase = prng_range(&f->rng, 0.0f, TWO_PI);
    fl->vjit_omega = TWO_PI / prng_range(&f->rng, 1.5f, 3.0f);
    flake_update_appearance(p, fl);
}

/**
 * @brief Seed a field with flakes spread over the screen.
 * @version 1
 * @req REQ-ANIM-002
 * @return Number of flakes created.
 */
int snow_field_init(snow_field_t *f, const snow_params_t *p, int count, uint32_t seed)
{
    memset(f, 0, sizeof(*f));
    f->p = *p;
    f->rng = seed ? seed : 1u;
    f->count = count < 0 ? 0 : (count > SNOW_MAX_FLAKES ? SNOW_MAX_FLAKES : count);
    for (int i = 0; i < f->count; i++) {
        snow_flake_t *fl = &f->flakes[i];
        flake_randomise(f, fl);
        fl->x_px = prng_range(&f->rng, 0.0f, (float)p->screen_w_px);
        fl->y_px = prng_range(&f->rng, -fl->radius_px, (float)p->screen_h_px);
    }
    return f->count;
}

/**
 * @brief Respawn a flake above the top edge with new random properties.
 * @version 1
 * @dg_internal
 */
static void flake_respawn(snow_field_t *f, snow_flake_t *fl)
{
    flake_randomise(f, fl);
    fl->x_px = prng_range(&f->rng, 0.0f, (float)f->p.screen_w_px);
    fl->y_px = -fl->radius_px - prng_range(&f->rng, 0.0f, 0.2f * (float)f->p.screen_h_px);
}

/**
 * @brief Wrap a flake horizontally so wind never empties one side of the screen.
 * @version 1
 * @dg_internal
 */
static void flake_wrap_x(const snow_params_t *p, snow_flake_t *fl)
{
    float span = (float)p->screen_w_px + 2.0f * fl->radius_px;
    if (fl->x_px < -fl->radius_px) fl->x_px += span;
    else if (fl->x_px > (float)p->screen_w_px + fl->radius_px) fl->x_px -= span;
}

/**
 * @brief Integrate all flakes by dt; respawn and wrap at the edges.
 * @version 1
 * @req REQ-ANIM-002
 */
void snow_field_step(snow_field_t *f, float dt_s)
{
    f->t_s += dt_s;
    const float t = f->t_s;
    for (int i = 0; i < f->count; i++) {
        snow_flake_t *fl = &f->flakes[i];
        float k = snow_px_per_m(&f->p, fl->z_m);
        float u = f->p.wind_mps + fl->sway_amp_mps * sinf(fl->sway_omega * t + fl->sway_phase);
        float jitter = f->p.vjitter_rel * SQRT2 * sinf(fl->vjit_omega * t + fl->sway_phase);
        fl->x_px += u * k * dt_s;
        fl->y_px += fl->v_mps * (1.0f + jitter) * k * dt_s;
        if (fl->y_px - fl->radius_px > (float)f->p.screen_h_px) flake_respawn(f, fl);
        flake_wrap_x(&f->p, fl);
    }
}
