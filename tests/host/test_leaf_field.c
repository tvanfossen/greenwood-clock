/**
 * @file test_leaf_field.c
 * @version 1
 * @brief Host tests for the falling-leaf field (projection, respawn, gentleness, determinism).
 */
#include "leaf_field.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

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

static leaf_field_t s_a, s_b;

/**
 * @brief Defaults: U from eq. 4.1 for a 0.2 mm leaf in air, flutter-dominated I* range.
 * @version 1
 * @req REQ-ANIM-003
 */
static void test_defaults(void)
{
    leaf_params_t p;
    leaf_params_default(&p, 1024, 600);
    float u = sqrtf((500.0f / 1.2f - 1.0f) * 9.81f * 0.0002f);
    CHECK(fabsf(p.u_mps - u) < 0.02f, "U %.3f expected %.3f", p.u_mps, u);
    CHECK(p.i_star_min >= 0.9f && p.i_star_max <= 1.6f, "I* range [%.2f, %.2f]", p.i_star_min,
          p.i_star_max);
    CHECK(p.dt_nd <= 0.02f, "dt_nd %.3f exceeds the validated step", p.dt_nd);
}

/**
 * @brief Init places every leaf on screen, deterministic per seed, clamps the count.
 * @version 1
 * @req REQ-ANIM-003
 */
static void test_init(void)
{
    leaf_params_t p;
    leaf_params_default(&p, 1024, 600);
    int n = leaf_field_init(&s_a, &p, LEAF_FIELD_MAX + 5, 11u);
    CHECK(n == LEAF_FIELD_MAX, "count clamp %d", n);
    for (int i = 0; i < n; i++) {
        float x, y, ang;
        leaf_screen_pose(&s_a, i, &x, &y, &ang);
        CHECK(x >= 0.0f && x < 1024.0f && y >= 0.0f && y < 600.0f, "leaf %d at (%.0f, %.0f)", i, x, y);
        CHECK(s_a.leaves[i].i_star >= p.i_star_min && s_a.leaves[i].i_star <= p.i_star_max, "I*");
        CHECK(s_a.leaves[i].color < LEAF_PALETTE_SIZE, "color %u", s_a.leaves[i].color);
    }
    leaf_field_init(&s_b, &p, LEAF_FIELD_MAX, 11u);
    CHECK(memcmp(s_a.leaves, s_b.leaves, sizeof(s_a.leaves)) == 0, "same seed differs");
}

/**
 * @brief Two minutes of 30 Hz frames: leaves stay within the respawn envelope, count constant.
 * @version 1
 * @req REQ-ANIM-003
 */
static void test_long_run_bounded(void)
{
    leaf_params_t p;
    leaf_params_default(&p, 1024, 600);
    int n = leaf_field_init(&s_a, &p, 18, 5u);
    for (int k = 0; k < 30 * 120; k++) leaf_field_step(&s_a, 1.0f / 30.0f);
    for (int i = 0; i < n; i++) {
        float x, y, ang;
        leaf_screen_pose(&s_a, i, &x, &y, &ang);
        CHECK(isfinite(x) && isfinite(y) && isfinite(ang), "leaf %d non-finite", i);
        CHECK(y > -400.0f && y < 700.0f && x > -100.0f && x < 1124.0f, "leaf %d escaped (%.0f, %.0f)",
              i, x, y);
    }
    CHECK(s_a.count == n, "count changed");
}

/**
 * @brief Gentle: a mid-depth leaf takes several seconds to cross the screen height.
 * @version 1
 * @req REQ-ANIM-003
 */
static void test_gentle(void)
{
    leaf_params_t p;
    leaf_params_default(&p, 1024, 600);
    leaf_field_init(&s_a, &p, 1, 3u);
    leaf_t *lf = &s_a.leaves[0];
    lf->z_m = 0.5f * (p.z_near_m + p.z_far_m);
    lf->i_star = 1.1f;
    lf->px_per_nd = p.semi_chord_m * p.focal_px / lf->z_m;
    float x0, y0, x1, y1, ang;
    leaf_screen_pose(&s_a, 0, &x0, &y0, &ang);
    for (int k = 0; k < 30 * 4; k++) leaf_field_step(&s_a, 1.0f / 30.0f);  /* 4 s */
    leaf_screen_pose(&s_a, 0, &x1, &y1, &ang);
    float px_per_s = (y1 - y0) / 4.0f;
    float crossing = 600.0f / px_per_s;
    printf("mid-depth leaf: %.0f px/s down, crosses 600 px in %.1f s\n", px_per_s, crossing);
    CHECK(px_per_s > 0.0f, "leaf must fall (screen y grows)");
    CHECK(crossing >= 4.0f && crossing <= 25.0f, "crossing %.1f s not gentle", crossing);
}

/**
 * @brief Run all leaf-field tests.
 * @req REQ-ANIM-003
 * @req REQ-TOOL-004
 * @version 1
 * @return 0 when every check passed.
 */
int main(void)
{
    test_defaults();
    test_init();
    test_long_run_bounded();
    test_gentle();
    printf("%d/%d checks passed\n", s_checks - s_failures, s_checks);
    return s_failures == 0 ? 0 : 1;
}
