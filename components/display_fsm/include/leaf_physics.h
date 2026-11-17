/**
 * @file leaf_physics.h
 * @version 1
 * @brief Falling-card (leaf) dynamics in air: the non-dimensional quasi-steady model of
 *        Andersen, Pesavento & Wang (2005b), J. Fluid Mech. 541, 91-104,
 *        thin-card limit eqs. 5.1-5.3 with fluid terms 4.7-4.9.
 *
 * Non-dimensional units: length L = a (semi-major axis), velocity
 * U = sqrt((rho_s/rho_f - 1) g b), time L/U (eq. 4.1). The only free physical
 * parameter in the thin limit is the dimensionless moment of inertia I* (eq. 4.2).
 * Pure C, host-tested in tests/host/test_leaf_physics.c. Sources: CITATIONS.md.
 */
#ifndef LEAF_PHYSICS_H
#define LEAF_PHYSICS_H

#ifdef __cplusplus
extern "C" {
#endif

/** Fluid-force coefficients (section 5.2 of the paper). */
typedef struct {
    float c_t;   /**< Translational circulation, C_T = 1.2. */
    float c_r;   /**< Rotational circulation, C_R = pi. */
    float a;     /**< Drag constant A = 1.4. */
    float b;     /**< Drag constant B = 1.0. */
    float mu1;   /**< Dissipative torque mu_1 = 0.2. */
    float mu2;   /**< Dissipative torque mu_2 = 0.2. */
} leaf_coeffs_t;

/** Card state: lab position/angle and BODY-frame velocities, all non-dimensional. */
typedef struct {
    float x, y;    /**< Lab position (y up). */
    float theta;   /**< Card angle, rad. */
    float vx, vy;  /**< Body-frame velocity (x along the chord). */
    float omega;   /**< Angular velocity. */
} leaf_state_t;

/**
 * @brief The paper's coefficients (C_T 1.2, C_R pi, A 1.4, B 1.0, mu1 = mu2 = 0.2).
 * @version 2
 * @req REQ-ANIM-003
 */
void leaf_coeffs_default(leaf_coeffs_t *c);

/**
 * @brief Advance one card by non-dimensional time @p dt (RK4 on eqs. 5.1-5.3).
 * @version 2
 * @req REQ-ANIM-003
 */
void leaf_step(const leaf_coeffs_t *c, float i_star, leaf_state_t *s, float dt);

/**
 * @brief Laboratory-frame velocity of the card centre.
 * @version 1
 * @req REQ-ANIM-003
 */
void leaf_lab_velocity(const leaf_state_t *s, float *vx_lab, float *vy_lab);

#ifdef __cplusplus
}
#endif

#endif /* LEAF_PHYSICS_H */
