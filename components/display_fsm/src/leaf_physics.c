/**
 * @file leaf_physics.c
 * @version 1
 * @brief Thin falling card in air, Andersen, Pesavento & Wang (2005b), eqs. 5.1-5.3 + 4.7-4.9.
 */
#include "leaf_physics.h"

#include <math.h>

#define PI_F 3.14159265359f

/**
 * @brief Section 5.2 coefficients.
 * @version 2
 * @req REQ-ANIM-003
 */
void leaf_coeffs_default(leaf_coeffs_t *c)
{
    c->c_t = 1.2f;
    c->c_r = PI_F;
    c->a   = 1.4f;
    c->b   = 1.0f;
    c->mu1 = 0.2f;
    c->mu2 = 0.2f;
}

typedef struct {
    float x, y, theta, vx, vy, omega;
} leaf_vec_t;

/**
 * @brief Time derivative of the state (eqs. 5.1-5.3 with 4.7-4.9).
 * @version 1
 * @dg_internal
 * @return d(state)/dt.
 */
static leaf_vec_t leaf_deriv(const leaf_coeffs_t *k, float is, const leaf_vec_t *s)
{
    float c = cosf(s->theta), sn = sinf(s->theta);
    float v2 = s->vx * s->vx + s->vy * s->vy;
    float speed = sqrtf(v2);
    float inv2 = v2 > 1e-12f ? 1.0f / v2 : 0.0f;
    float gamma = (2.0f / PI_F) * (-k->c_t * s->vx * s->vy * sqrtf(inv2) + k->c_r * s->omega);  /* 4.7 */
    float drag = (1.0f / PI_F) * (k->a - k->b * (s->vx * s->vx - s->vy * s->vy) * inv2) * speed;  /* 4.8 */
    float tau = (k->mu1 + k->mu2 * fabsf(s->omega)) * s->omega;                                    /* 4.9 */
    leaf_vec_t d;
    d.vx = ((is + 1.0f) * s->omega * s->vy - gamma * s->vy - sn - drag * s->vx) / is;           /* 5.1 */
    d.vy = (-is * s->omega * s->vx + gamma * s->vx - c - drag * s->vy) / (is + 1.0f);            /* 5.2 */
    d.omega = (-s->vx * s->vy - tau) / (0.25f * (is + 0.5f));                                     /* 5.3 */
    d.x = s->vx * c - s->vy * sn;
    d.y = s->vx * sn + s->vy * c;
    d.theta = s->omega;
    return d;
}

/**
 * @brief s + k * d, component-wise.
 * @version 1
 * @dg_internal
 * @return The combined vector.
 */
static leaf_vec_t leaf_axpy(const leaf_vec_t *s, const leaf_vec_t *d, float k)
{
    leaf_vec_t r = {s->x + k * d->x,   s->y + k * d->y,   s->theta + k * d->theta,
                    s->vx + k * d->vx, s->vy + k * d->vy, s->omega + k * d->omega};
    return r;
}

/**
 * @brief One classical RK4 step.
 * @version 2
 * @req REQ-ANIM-003
 */
void leaf_step(const leaf_coeffs_t *c, float i_star, leaf_state_t *st, float dt)
{
    leaf_vec_t s = {st->x, st->y, st->theta, st->vx, st->vy, st->omega};
    leaf_vec_t k1 = leaf_deriv(c, i_star, &s);
    leaf_vec_t s2 = leaf_axpy(&s, &k1, 0.5f * dt);
    leaf_vec_t k2 = leaf_deriv(c, i_star, &s2);
    leaf_vec_t s3 = leaf_axpy(&s, &k2, 0.5f * dt);
    leaf_vec_t k3 = leaf_deriv(c, i_star, &s3);
    leaf_vec_t s4 = leaf_axpy(&s, &k3, dt);
    leaf_vec_t k4 = leaf_deriv(c, i_star, &s4);
    leaf_vec_t sum = leaf_axpy(&k1, &k2, 2.0f);
    sum = leaf_axpy(&sum, &k3, 2.0f);
    sum = leaf_axpy(&sum, &k4, 1.0f);
    s = leaf_axpy(&s, &sum, dt / 6.0f);
    st->x = s.x;
    st->y = s.y;
    st->theta = s.theta;
    st->vx = s.vx;
    st->vy = s.vy;
    st->omega = s.omega;
}

/**
 * @brief Rotate body-frame velocity into the laboratory frame.
 * @version 1
 * @req REQ-ANIM-003
 */
void leaf_lab_velocity(const leaf_state_t *s, float *vx_lab, float *vy_lab)
{
    float c = cosf(s->theta), sn = sinf(s->theta);
    *vx_lab = s->vx * c - s->vy * sn;
    *vy_lab = s->vx * sn + s->vy * c;
}
