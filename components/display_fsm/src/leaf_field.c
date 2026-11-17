/**
 * @file leaf_field.c
 * @version 1
 * @brief Falling-leaf field: leaf_physics per leaf + pinhole depth projection to the screen.
 */
#include "leaf_field.h"
#include "particle_rng.h"

#include <math.h>
#include <string.h>

/** Cap on RK4 substeps per frame so a long stall cannot spike the frame cost. */
#define LEAF_MAX_SUBSTEPS 240
/** Non-dimensional time a fresh leaf is pre-simulated so it starts in settled motion. */
#define LEAF_PREWARM_ND 30.0f
#define TWO_PI_F 6.28318530718f

/**
 * @brief Gentle autumn defaults.
 * @version 1
 * @req REQ-ANIM-003
 */
void leaf_params_default(leaf_params_t *p, int screen_w_px, int screen_h_px)
{
    memset(p, 0, sizeof(*p));
    p->screen_w_px  = screen_w_px;
    p->screen_h_px  = screen_h_px;
    p->focal_px     = 643.0f;  /* design: same camera as the snowfall */
    p->z_near_m     = 3.0f;    /* design */
    p->z_far_m      = 8.0f;    /* design */
    p->semi_chord_m = 0.03f;   /* a: 6 cm leaf */
    /* eq. 4.1, U = sqrt((rho_s/rho_f - 1) g b): dry leaf ~500 kg/m^3, air 1.2 kg/m^3, b = 0.2 mm */
    p->u_mps        = sqrtf((500.0f / 1.2f - 1.0f) * 9.81f * 0.0002f);
    p->i_star_min   = 0.9f;    /* design: flutter-dominated (I* = 1.1 flutters, 1.4 tumbles) */
    p->i_star_max   = 1.6f;
    p->size_gain    = 2.0f;    /* design: drawn size only */
    p->dt_nd        = 0.02f;   /* validated step (tests/host/test_leaf_physics.c) */
}

/**
 * @brief Give a leaf a new depth, I*, colour and a settled physical state at rest origin.
 * @version 1
 * @dg_internal
 */
static void leaf_randomise(leaf_field_t *f, leaf_t *lf)
{
    const leaf_params_t *p = &f->p;
    memset(&lf->s, 0, sizeof(lf->s));
    lf->s.theta = prng_range(&f->rng, -0.5f, 0.5f);
    lf->i_star = prng_range(&f->rng, p->i_star_min, p->i_star_max);
    lf->z_m = prng_range(&f->rng, p->z_near_m, p->z_far_m);
    lf->px_per_nd = p->semi_chord_m * p->focal_px / lf->z_m;
    lf->half_len_px = lf->px_per_nd * p->size_gain;
    float depth01 = (lf->z_m - p->z_near_m) / (p->z_far_m - p->z_near_m);
    lf->opa = (uint8_t)(255.0f - 90.0f * depth01);
    lf->color = (uint8_t)(prng_next(&f->rng) % LEAF_PALETTE_SIZE);
    for (float t = 0; t < LEAF_PREWARM_ND; t += p->dt_nd) leaf_step(&f->c, lf->i_star, &lf->s, p->dt_nd);
}

/**
 * @brief Place a leaf's physics origin so its current pose lands at screen (x, y).
 * @version 1
 * @dg_internal
 */
static void leaf_place(leaf_t *lf, float x_px, float y_px)
{
    lf->x0_px = x_px - lf->s.x * lf->px_per_nd;
    lf->y0_px = y_px + lf->s.y * lf->px_per_nd;  /* lab y is up, screen y is down */
}

/**
 * @brief Seed leaves in settled motion, spread across the screen.
 * @version 1
 * @req REQ-ANIM-003
 * @return Leaves created.
 */
int leaf_field_init(leaf_field_t *f, const leaf_params_t *p, int count, uint32_t seed)
{
    memset(f, 0, sizeof(*f));
    f->p = *p;
    leaf_coeffs_default(&f->c);
    f->rng = seed ? seed : 1u;
    f->count = count < 0 ? 0 : (count > LEAF_FIELD_MAX ? LEAF_FIELD_MAX : count);
    for (int i = 0; i < f->count; i++) {
        leaf_t *lf = &f->leaves[i];
        leaf_randomise(f, lf);
        leaf_place(lf, prng_range(&f->rng, 0.0f, (float)p->screen_w_px),
                   prng_range(&f->rng, 0.0f, (float)p->screen_h_px));
    }
    return f->count;
}

/**
 * @brief Screen centre and rotation of leaf @p i (screen angle is the lab angle mirrored).
 * @version 1
 * @req REQ-ANIM-003
 */
void leaf_screen_pose(const leaf_field_t *f, int i, float *x_px, float *y_px, float *angle_rad)
{
    const leaf_t *lf = &f->leaves[i];
    *x_px = lf->x0_px + lf->s.x * lf->px_per_nd;
    *y_px = lf->y0_px - lf->s.y * lf->px_per_nd;
    *angle_rad = -lf->s.theta;
}

/**
 * @brief Respawn below-screen leaves at the top; wrap horizontal exits to the other side.
 * @version 1
 * @dg_internal
 */
static void leaf_recycle(leaf_field_t *f, leaf_t *lf)
{
    float x, y, ang;
    leaf_screen_pose(f, (int)(lf - f->leaves), &x, &y, &ang);
    float w = (float)f->p.screen_w_px, margin = lf->half_len_px;
    if (y - margin > (float)f->p.screen_h_px) {
        leaf_randomise(f, lf);
        leaf_place(lf, prng_range(&f->rng, 0.0f, w),
                   -lf->half_len_px - prng_range(&f->rng, 0.0f, 0.25f * (float)f->p.screen_h_px));
    } else if (x < -margin) {
        lf->x0_px += w + 2.0f * margin;
    } else if (x > w + margin) {
        lf->x0_px -= w + 2.0f * margin;
    }
}

/**
 * @brief Integrate every leaf by real time @p dt_s (converted via L/U), then recycle.
 * @version 1
 * @req REQ-ANIM-003
 */
void leaf_field_step(leaf_field_t *f, float dt_s)
{
    float nd = dt_s * f->p.u_mps / f->p.semi_chord_m;
    int steps = (int)ceilf(nd / f->p.dt_nd);
    if (steps > LEAF_MAX_SUBSTEPS) steps = LEAF_MAX_SUBSTEPS;
    float h = steps > 0 ? nd / (float)steps : 0.0f;
    for (int i = 0; i < f->count; i++) {
        leaf_t *lf = &f->leaves[i];
        for (int k = 0; k < steps; k++) leaf_step(&f->c, lf->i_star, &lf->s, h);
        leaf_recycle(f, lf);
    }
}
