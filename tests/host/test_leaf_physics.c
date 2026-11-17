/**
 * @file test_leaf_physics.c
 * @brief Host tests: leaf_physics reproduces the regimes reported by
 *        Andersen, Pesavento & Wang (2005b), section 5.2 / figure 3:
 *        "periodic fluttering for I* = 1.1, period-one tumbling for I* = 1.4".
 */
#include "leaf_physics.h"

#include <math.h>
#include <stdio.h>

static int s_failures = 0;
static int s_checks   = 0;

#define CHECK(cond, ...)                                              \
    do {                                                              \
        s_checks++;                                                   \
        if (!(cond)) {                                                \
            s_failures++;                                             \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);               \
            printf(__VA_ARGS__);                                      \
            printf("\n");                                             \
        }                                                             \
    } while (0)

#define SETTLE_T  100.0f   /* non-dimensional time units */
#define MEASURE_T 200.0f

typedef struct {
    int   omega_sign_changes;
    float theta_min, theta_max;
    double mean_vy;
    int   finite;
} regime_t;

/**
 * @brief Simulate one card and summarise its settled motion.
 * @version 1
 * @dg_internal
 * @return Regime summary.
 */
static regime_t simulate(float i_star, float dt)
{
    leaf_coeffs_t c;
    leaf_coeffs_default(&c);
    leaf_state_t s = {.theta = 0.2f};
    for (float t = 0; t < SETTLE_T; t += dt) leaf_step(&c, i_star, &s, dt);
    regime_t r = {.theta_min = s.theta, .theta_max = s.theta, .finite = 1};
    float prev = s.omega;
    int n = 0;
    for (float t = 0; t < MEASURE_T; t += dt, n++) {
        leaf_step(&c, i_star, &s, dt);
        float vxl, vyl;
        leaf_lab_velocity(&s, &vxl, &vyl);
        r.mean_vy += vyl;
        r.omega_sign_changes += (prev * s.omega < 0.0f) ? 1 : 0;
        prev = s.omega;
        r.theta_min = fminf(r.theta_min, s.theta);
        r.theta_max = fmaxf(r.theta_max, s.theta);
        r.finite &= isfinite(s.theta) && isfinite(s.vx) && isfinite(s.vy);
    }
    r.mean_vy /= n;
    printf("I*=%.2f dt=%.3f: theta=[%.2f, %.2f] omega reversals=%d mean vy=%.3f\n", i_star, dt,
           r.theta_min, r.theta_max, r.omega_sign_changes, r.mean_vy);
    return r;
}

/**
 * @brief Coefficients equal the values stated in section 5.2.
 * @version 1
 * @req REQ-LEAF-001
 */
static void test_coefficients_match_paper(void)
{
    leaf_coeffs_t c;
    leaf_coeffs_default(&c);
    CHECK(fabsf(c.c_t - 1.2f) < 1e-6f && fabsf(c.c_r - 3.14159265f) < 1e-5f, "C_T/C_R");
    CHECK(fabsf(c.a - 1.4f) < 1e-6f && fabsf(c.b - 1.0f) < 1e-6f, "A/B");
    CHECK(fabsf(c.mu1 - 0.2f) < 1e-6f && fabsf(c.mu2 - 0.2f) < 1e-6f, "mu1/mu2");
}

/**
 * @brief I* = 1.1 flutters: bounded angle (< pi), repeated angular-velocity reversals, descends.
 * @version 1
 * @req REQ-LEAF-001
 */
static void test_flutter_at_1_1(void)
{
    regime_t r = simulate(1.1f, 0.01f);
    CHECK(r.finite, "non-finite state");
    CHECK(r.theta_max - r.theta_min < (float)M_PI, "theta range %.2f is tumbling",
          r.theta_max - r.theta_min);
    CHECK(r.omega_sign_changes >= 6, "only %d omega reversals: not fluttering",
          r.omega_sign_changes);
    CHECK(r.mean_vy < 0.0, "card must descend (mean vy %.3f)", r.mean_vy);
}

/**
 * @brief I* = 1.4 tumbles: the angle winds through more than a full turn.
 * @version 1
 * @req REQ-LEAF-001
 */
static void test_tumble_at_1_4(void)
{
    regime_t r = simulate(1.4f, 0.01f);
    CHECK(r.finite, "non-finite state");
    CHECK(r.theta_max - r.theta_min > 2.0f * (float)M_PI, "theta range %.2f: not tumbling",
          r.theta_max - r.theta_min);
    CHECK(r.mean_vy < 0.0, "card must descend (mean vy %.3f)", r.mean_vy);
}

/**
 * @brief The on-device step size (0.02) keeps the same regime as a fine step.
 * @version 1
 * @req REQ-LEAF-001
 */
static void test_device_step_size(void)
{
    regime_t coarse = simulate(1.1f, 0.02f);
    CHECK(coarse.finite, "non-finite at dt=0.02");
    CHECK(coarse.theta_max - coarse.theta_min < (float)M_PI && coarse.omega_sign_changes >= 6,
          "dt=0.02 changes the I*=1.1 regime");
}

/**
 * @brief Run all leaf tests.
 * @version 2
 * @return 0 when every check passed.
 */
int main(void)
{
    test_coefficients_match_paper();
    test_flutter_at_1_1();
    test_tumble_at_1_4();
    test_device_step_size();
    printf("%d/%d checks passed\n", s_checks - s_failures, s_checks);
    return s_failures == 0 ? 0 : 1;
}
