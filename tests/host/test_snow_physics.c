/**
 * @file test_snow_physics.c
 * @version 1
 * @brief Host unit tests for snow_physics (run: .venv/bin/inv test-host).
 *
 * Each test states the property it specifies. Physics constants are checked
 * against the published values cited in CITATIONS.md.
 */
#include "snow_physics.h"

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

static snow_field_t s_field;
static snow_field_t s_field_b;

/**
 * @brief Fall speed matches Locatelli & Hobbs (1974) Fig. 20 and their 0.5-1.4 m/s range.
 * @version 1
 * @req REQ-ANIM-002
 */
static void test_fall_speed_matches_locatelli_hobbs(void)
{
    CHECK(fabsf(snow_fall_speed_mps(1.0f) - 0.81f) < 1e-4f, "V(1mm)=%f", snow_fall_speed_mps(1.0f));
    float v10 = snow_fall_speed_mps(10.0f);
    CHECK(fabsf(v10 - 0.81f * powf(10.0f, 0.16f)) < 1e-4f, "V(10mm)=%f", v10);
    for (float d = 1.0f; d <= 14.0f; d += 0.5f) {
        float v = snow_fall_speed_mps(d);
        CHECK(v >= 0.5f && v <= 1.4f, "V(%.1fmm)=%f outside paper's 0.5-1.4 m/s", d, v);
    }
    CHECK(snow_fall_speed_mps(8.0f) > snow_fall_speed_mps(2.0f), "larger flakes must fall faster");
}

/**
 * @brief Sampled diameters stay in range and follow the truncated-exponential mean.
 * @version 1
 * @req REQ-ANIM-002
 */
static void test_diameter_distribution(void)
{
    uint32_t rng = 12345u;
    const float lam = 0.5f, lo = 1.0f, hi = 10.0f;
    const int n = 20000;
    double sum = 0.0;
    int below_mid = 0;
    for (int i = 0; i < n; i++) {
        float d = snow_sample_diameter_mm(&rng, lam, lo, hi);
        CHECK(d >= lo && d <= hi, "d=%f out of [%f,%f]", d, lo, hi);
        sum += d;
        below_mid += (d < 0.5f * (lo + hi)) ? 1 : 0;
    }
    /* Mean of an exponential truncated to [a,b]: a + 1/l - (b-a)e^{-l(b-a)}/(1-e^{-l(b-a)}) */
    double w = hi - lo, e = exp(-lam * w);
    double expected = lo + 1.0 / lam - w * e / (1.0 - e);
    CHECK(fabs(sum / n - expected) < 0.1, "mean %f expected %f", sum / n, expected);
    CHECK(below_mid > n / 2, "exponential must favour small flakes (%d/%d below midpoint)", below_mid, n);
}

/**
 * @brief Default parameters reproduce the cited sway and jitter ranges.
 * @version 1
 * @req REQ-ANIM-002
 */
static void test_defaults_cite_kajikawa(void)
{
    snow_params_t p;
    snow_params_default(&p, 1024, 600);
    CHECK(fabsf(p.sway_sigma_min_mps - 0.01f) < 1e-6f, "sigma min %f", p.sway_sigma_min_mps);
    CHECK(fabsf(p.sway_sigma_max_mps - 0.08f) < 1e-6f, "sigma max %f", p.sway_sigma_max_mps);
    CHECK(fabsf(p.steady_fraction - 0.20f) < 1e-6f, "steady fraction %f", p.steady_fraction);
    CHECK(p.vjitter_rel >= 0.03f && p.vjitter_rel <= 0.05f, "vjitter %f", p.vjitter_rel);
    CHECK(p.z_near_m > 0.0f && p.z_far_m > p.z_near_m, "depth range");
}

/**
 * @brief Init clamps the count, places flakes on screen, and is deterministic per seed.
 * @version 1
 * @req REQ-ANIM-002
 */
static void test_init_bounds_and_determinism(void)
{
    snow_params_t p;
    snow_params_default(&p, 1024, 600);
    int n = snow_field_init(&s_field, &p, SNOW_MAX_FLAKES + 50, 7u);
    CHECK(n == SNOW_MAX_FLAKES, "count clamp %d", n);
    for (int i = 0; i < n; i++) {
        const snow_flake_t *fl = &s_field.flakes[i];
        CHECK(fl->x_px >= 0.0f && fl->x_px < 1024.0f, "x=%f", fl->x_px);
        CHECK(fl->y_px >= -p.radius_max_px && fl->y_px < 600.0f, "y=%f", fl->y_px);
        CHECK(fl->z_m >= p.z_near_m && fl->z_m <= p.z_far_m, "z=%f", fl->z_m);
        CHECK(fl->radius_px >= p.radius_min_px && fl->radius_px <= p.radius_max_px, "r=%f", fl->radius_px);
    }
    snow_field_init(&s_field_b, &p, SNOW_MAX_FLAKES, 7u);
    CHECK(memcmp(s_field.flakes, s_field_b.flakes, sizeof(s_field.flakes)) == 0, "same seed differs");
}

/**
 * @brief Roughly steady_fraction of flakes have no sway (Kajikawa 1982: ~80% spiral).
 * @version 1
 * @req REQ-ANIM-002
 */
static void test_steady_fraction(void)
{
    snow_params_t p;
    snow_params_default(&p, 1024, 600);
    int steady = 0, total = 0;
    for (uint32_t seed = 1; seed <= 20; seed++) {
        int n = snow_field_init(&s_field, &p, SNOW_MAX_FLAKES, seed);
        for (int i = 0; i < n; i++) steady += (s_field.flakes[i].sway_amp_mps == 0.0f) ? 1 : 0;
        total += n;
    }
    float frac = (float)steady / (float)total;
    CHECK(fabsf(frac - p.steady_fraction) < 0.03f, "steady fraction %f", frac);
}

/**
 * @brief Nearer flakes move more pixels per second than identical far ones (perspective parallax).
 * @version 1
 * @req REQ-ANIM-002
 */
static void test_parallax(void)
{
    snow_params_t p;
    snow_params_default(&p, 1024, 600);
    p.wind_mps = 0.0f;
    snow_field_init(&s_field, &p, 2, 3u);
    for (int i = 0; i < 2; i++) {
        snow_flake_t *fl = &s_field.flakes[i];
        fl->d_mm = 4.0f;
        fl->v_mps = snow_fall_speed_mps(4.0f);
        fl->sway_amp_mps = 0.0f;
        fl->y_px = 100.0f;
    }
    s_field.flakes[0].z_m = p.z_near_m;
    s_field.flakes[1].z_m = p.z_far_m;
    snow_field_step(&s_field, 0.1f);
    float dy_near = s_field.flakes[0].y_px - 100.0f;
    float dy_far  = s_field.flakes[1].y_px - 100.0f;
    CHECK(dy_near > 0.0f && dy_far > 0.0f, "both must fall (%f, %f)", dy_near, dy_far);
    CHECK(dy_near > dy_far * 1.5f, "near %f should exceed far %f", dy_near, dy_far);
    /* Vertical jitter is a few percent (Kajikawa 1989), so allow 6% of the projected speed. */
    float expect_far = snow_fall_speed_mps(4.0f) * snow_px_per_m(&p, p.z_far_m) * 0.1f;
    CHECK(fabsf(dy_far - expect_far) <= expect_far * 0.06f, "far dy %f expected ~%f", dy_far, expect_far);
}

/**
 * @brief Long simulation keeps every flake inside the respawn envelope; no flake is lost.
 * @version 1
 * @req REQ-ANIM-002
 */
static void test_recycling(void)
{
    snow_params_t p;
    snow_params_default(&p, 1024, 600);
    int n = snow_field_init(&s_field, &p, 150, 99u);
    for (int step = 0; step < 30 * 120; step++) snow_field_step(&s_field, 1.0f / 30.0f);
    float margin = p.radius_max_px * 4.0f;
    for (int i = 0; i < n; i++) {
        const snow_flake_t *fl = &s_field.flakes[i];
        CHECK(fl->y_px >= -margin - 600.0f && fl->y_px <= 600.0f + margin, "y=%f escaped", fl->y_px);
        CHECK(fl->x_px >= -margin && fl->x_px <= 1024.0f + margin, "x=%f escaped", fl->x_px);
    }
    CHECK(s_field.count == n, "count changed %d", s_field.count);
}

/**
 * @brief Gentle default: a mid-depth, mid-size flake takes several seconds to cross the screen.
 * @version 1
 * @req REQ-ANIM-002
 */
static void test_gentle_crossing_time(void)
{
    snow_params_t p;
    snow_params_default(&p, 1024, 600);
    float z_mid = 0.5f * (p.z_near_m + p.z_far_m);
    float px_per_s = snow_fall_speed_mps(4.0f) * snow_px_per_m(&p, z_mid);
    float crossing_s = 600.0f / px_per_s;
    CHECK(crossing_s >= 5.0f && crossing_s <= 20.0f, "crossing %fs not gentle", crossing_s);
}

/**
 * @brief Count sway reversals of one swaying flake over @p seconds, starting at field time @p t0_s.
 * @version 1
 * @dg_internal
 * @return Number of sign changes of the per-frame horizontal displacement (|dx| > 0.01 px).
 */
static int sway_reversals_from(double t0_s, float seconds)
{
    snow_params_t p;
    snow_params_default(&p, 1024, 600);
    p.wind_mps = 0.0f;
    p.screen_h_px = 1000000;  /* never respawn: respawning re-randomises the sway period */
    snow_field_init(&s_field, &p, 1, 5u);
    snow_flake_t *fl = &s_field.flakes[0];
    fl->x_px = 512.0f;
    fl->y_px = 0.0f;
    fl->sway_amp_mps = 0.08f;
    fl->sway_omega = 6.28318530718f / 4.0f;  /* 4 s sway period */
    s_field.t_s = t0_s;
    const float dt = 1.0f / 30.0f;
    int reversals = 0, prev_sign = 0;
    for (int k = 0; k < (int)(seconds / dt); k++) {
        float x_before = fl->x_px;
        snow_field_step(&s_field, dt);
        float dx = fl->x_px - x_before;
        int sign = dx > 0.01f ? 1 : (dx < -0.01f ? -1 : 0);
        if (sign != 0 && prev_sign != 0 && sign != prev_sign) reversals++;
        if (sign != 0) prev_sign = sign;
    }
    return reversals;
}

/**
 * @brief Sway keeps its rate after days of runtime: 40 s of a 4 s sway has ~20 reversals at any
 *        elapsed time. A float time accumulator doubles the rate past 2^19 s and freezes past 2^20 s.
 * @version 1
 * @req REQ-ANIM-002
 */
static void test_sway_survives_long_runtime(void)
{
    const double starts[] = {0.0, 524288.0 /* 2^19 s, ~6 days */, 1048576.0 /* 2^20 s, ~12 days */};
    for (int i = 0; i < 3; i++) {
        int n = sway_reversals_from(starts[i], 40.0f);
        CHECK(n >= 18 && n <= 22, "t0=%.0f s: %d sway reversals in 40 s, expected ~20", starts[i], n);
    }
}

/**
 * @brief Run all tests and report.
 * v2: adds the long-runtime sway test.
 * @req REQ-ANIM-002
 * @req REQ-TOOL-004
 * @version 2
 * @return 0 when every check passed, 1 otherwise.
 */
int main(void)
{
    test_fall_speed_matches_locatelli_hobbs();
    test_diameter_distribution();
    test_defaults_cite_kajikawa();
    test_init_bounds_and_determinism();
    test_steady_fraction();
    test_parallax();
    test_recycling();
    test_gentle_crossing_time();
    test_sway_survives_long_runtime();
    printf("%d/%d checks passed\n", s_checks - s_failures, s_checks);
    return s_failures == 0 ? 0 : 1;
}
