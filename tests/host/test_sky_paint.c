/**
 * @file test_sky_paint.c
 * @version 1
 * @brief Host tests for the sun and moon painter: the moon's lit share of the disc matches its
 *        phase (within 0.05) and is on the right while waxing; full is all lit, new nothing;
 *        the dark side shows only the asked earthshine, past a thin soft terminator; the sun is an opaque disc in a glow
 *        whose rays turn between frames; nothing is painted outside the square.
 */
#include "sky_paint.h"

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

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

#define D 120
static const sky_rgb_t LIT = { 240, 240, 236 }, DARK = { 30, 32, 52 };
static const sky_rgb_t CORE = { 238, 196, 100 };

/** What a painted moon shows. */
typedef struct {
    float lit;      /**< Share of the disc that is lit. */
    float lit_x;    /**< Mean x of the lit pixels, -1 (left) .. 1 (right). */
    float band;     /**< Share between lit and dark: the soft terminator. */
    int   max_dark_alpha;   /**< The most opaque pixel of the dark side (beyond the band). */
} moon_stats_t;

/** Running counts over the disc's pixels. */
typedef struct {
    int   disc, lit, band;
    float sx;
} tally_t;

/**
 * @brief Count one pixel of alpha @p a at disc x @p nx into @p t and @p s.
 * @version 1
 * @utility
 */
static void count(tally_t *t, moon_stats_t *s, int a, float nx, int dark_a)
{
    bool is_lit = a > (dark_a + 255) / 2;   // the lit side is opaque, the dark side not
    t->disc++;
    if (is_lit) {
        t->lit++;
        t->sx += nx;
    }
    if (a > dark_a + 8 && a < 247) t->band++;
    else if (!is_lit && a > s->max_dark_alpha) s->max_dark_alpha = a;
}

/**
 * @brief Measure a moon painted at @p age with dark-side alpha @p dark_a.
 * @version 1
 * @utility
 * @return What it shows.
 */
static moon_stats_t moon(float age, uint8_t dark_a)
{
    static uint32_t px[D * D];
    sky_paint_moon(px, D, age, LIT, DARK, dark_a);
    moon_stats_t s = { 0, 0, 0, 0 };
    tally_t t = { 0, 0, 0, 0 };
    const float r = D * SKY_MOON_R;
    for (int y = 0; y < D; y++) {
        for (int x = 0; x < D; x++) {
            float nx = (x + 0.5f - D / 2.0f) / r, ny = (y + 0.5f - D / 2.0f) / r;
            if (nx * nx + ny * ny <= 0.9f) count(&t, &s, (int)(px[y * D + x] >> 24), nx, dark_a);
        }
    }
    s.lit = (float)t.lit / (float)t.disc;
    s.lit_x = t.lit ? t.sx / (float)t.lit : 0;
    s.band = (float)t.band / (float)t.disc;
    return s;
}

/**
 * @brief The moon's phases: lit share, side, dark side.
 * @version 1
 * @utility
 */
static void test_moon(void)
{
    static const float AGES[] = { 0.10f, 0.25f, 0.40f, 0.60f, 0.75f, 0.90f };
    for (size_t i = 0; i < sizeof(AGES) / sizeof(AGES[0]); i++) {
        float want = (1.0f - cosf(AGES[i] * 2.0f * 3.14159265f)) / 2.0f;
        moon_stats_t s = moon(AGES[i], 40);
        CHECK(fabsf(s.lit - want) < 0.05f, "age %.2f: lit %.3f, phase %.3f", (double)AGES[i],
              (double)s.lit, (double)want);
        CHECK(AGES[i] < 0.5f ? s.lit_x > 0.05f : s.lit_x < -0.05f,
              "age %.2f: lit side x %.2f (waxing right, waning left)", (double)AGES[i],
              (double)s.lit_x);
        CHECK(s.max_dark_alpha <= 40 + 8, "age %.2f: dark side alpha %d above 40", (double)AGES[i],
              s.max_dark_alpha);
        CHECK(s.band < 0.08f, "age %.2f: terminator band %.3f of the disc (soft, not smeared)",
              (double)AGES[i], (double)s.band);
    }
    CHECK(moon(0.5f, 0).lit > 0.97f, "full: all lit");
    CHECK(moon(0.0f, 0).lit < 0.02f, "new: nothing lit");
    CHECK(moon(0.0f, 0).max_dark_alpha == 0, "by day the dark side is not drawn");
}

/**
 * @brief The sun: an opaque disc, a glow round it, rays that turn, nothing at the corners.
 * @version 1
 * @utility
 */
static void test_sun(void)
{
    static uint8_t mem[1 << 20];
    static uint32_t a[D * D], b[D * D];
    CHECK(sky_sun_bytes(D) <= sizeof(mem), "the sun's tables fit");
    sky_sun_t sun;
    sky_sun_init(&sun, mem, D, CORE, LIT);
    sky_sun_frame(&sun, 0.0f, a);
    sky_sun_frame(&sun, 0.5f, b);
    CHECK(a[(D / 2) * D + D / 2] >> 24 == 255, "the disc's centre is opaque");
    CHECK(a[0] >> 24 == 0 && a[D * D - 1] >> 24 == 0, "nothing at the corners");
    int glow = 0, moved = 0;
    for (int i = 0; i < D * D; i++) {
        int al = (int)(a[i] >> 24);
        glow += al > 0 && al < 255;
        moved += a[i] != b[i];
    }
    CHECK(glow > D * D / 10, "a glow round the disc (%d px)", glow);
    CHECK(moved > D, "the rays turn between frames (%d px changed)", moved);
}

/**
 * @brief Run the painter tests; exit status is the failure count.
 * @version 1
 * @utility
 * @return 0 when every check passed.
 */
int main(void)
{
    test_moon();
    test_sun();
    printf("sky_paint: %d/%d checks passed\n", s_checks - s_failures, s_checks);
    return s_failures;
}
