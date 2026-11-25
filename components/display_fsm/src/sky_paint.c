/**
 * @file sky_paint.c
 * v2: the sun's frame from precomputed tables (no transcendental per pixel per frame).
 * @version 2
 * @brief The sun and the moon as pixels (see sky_paint.h). Pure C.
 */
#include "sky_paint.h"

#include <math.h>
#include <string.h>

#define PI_F       3.14159265f
#define SUN_R      0.30f    // the disc's radius, as a share of the square's side
#define RAYS       16
#define RAY_IN     1.02f    // rays start at the rim (in disc radii) ...
#define RAY_LONG   1.66f    // ... and fade out this far (every other one ...
#define RAY_SHORT  1.40f    // ... this far)
#define RAY_HALF   0.060f   // a ray's angular spread at the rim, radians (narrower outwards)
#define GLOW_W     0.17f    // the glow's fall-off, as a share of the side
#define DIAG       1.4143f  // the corner's distance, in half sides

/** A soft patch of the moon's surface: where, how big, how much darker (or brighter). */
typedef struct {
    float x, y, r, albedo;
} patch_t;

/** The near side's maria (darker, overlapping into one irregular dark region as they really
 *  do) and its bright rayed craters, on the unit disc, y up. */
static const patch_t PATCHES[] = {
    { -0.32f, 0.40f, 0.34f, 0.74f },   // Mare Imbrium
    { -0.05f, 0.52f, 0.20f, 0.80f },   // Mare Frigoris's west end
    { 0.12f, 0.36f, 0.20f, 0.74f },    // Mare Serenitatis
    { 0.30f, 0.10f, 0.24f, 0.75f },    // Mare Tranquillitatis
    { 0.68f, 0.30f, 0.12f, 0.72f },    // Mare Crisium
    { 0.50f, -0.16f, 0.15f, 0.80f },   // Mare Fecunditatis
    { 0.24f, -0.16f, 0.11f, 0.82f },   // Mare Nectaris
    { -0.62f, 0.10f, 0.40f, 0.76f },   // Oceanus Procellarum
    { -0.48f, -0.24f, 0.20f, 0.80f },  // Mare Humorum and the ocean's south
    { -0.16f, -0.28f, 0.18f, 0.80f },  // Mare Nubium
    { 0.02f, 0.06f, 0.14f, 0.84f },    // Sinus Medii, Mare Vaporum
};

/** The bright craters: a bright floor and faint rays. */
static const patch_t CRATERS[] = {
    { -0.13f, -0.70f, 0.045f, 1.0f },  // Tycho
    { -0.30f, 0.14f, 0.040f, 0.6f },   // Copernicus
    { -0.58f, 0.22f, 0.030f, 0.5f },   // Kepler
    { -0.66f, 0.40f, 0.030f, 0.5f },   // Aristarchus
};

/**
 * @brief @p v clamped to 0..1.
 * @version 1
 * @utility
 * @return It.
 */
static float clamp01(float v)
{
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

/**
 * @brief Hermite step from @p a to @p b.
 * @version 1
 * @utility
 * @return 0..1.
 */
static float smooth(float a, float b, float v)
{
    float t = clamp01((v - a) / (b - a));
    return t * t * (3.0f - 2.0f * t);
}

/**
 * @brief @p a towards @p b by @p t, per channel.
 * @version 1
 * @utility
 * @return The colour.
 */
static sky_rgb_t mix(sky_rgb_t a, sky_rgb_t b, float t)
{
    return (sky_rgb_t){ (uint8_t)(a.r + (b.r - a.r) * t), (uint8_t)(a.g + (b.g - a.g) * t),
                        (uint8_t)(a.b + (b.b - a.b) * t) };
}

/**
 * @brief @p c scaled by @p k (0..1).
 * @version 1
 * @utility
 * @return The colour.
 */
static sky_rgb_t scale(sky_rgb_t c, float k)
{
    return (sky_rgb_t){ (uint8_t)(c.r * k), (uint8_t)(c.g * k), (uint8_t)(c.b * k) };
}

/**
 * @brief ARGB8888 of @p c at alpha @p a (0..1).
 * @version 1
 * @utility
 * @return The pixel.
 */
static uint32_t argb(sky_rgb_t c, float a)
{
    uint32_t al = (uint32_t)(clamp01(a) * 255.0f + 0.5f);
    return al << 24 | (uint32_t)c.r << 16 | (uint32_t)c.g << 8 | c.b;
}

/**
 * @brief A smooth wobble at (@p x, @p y): a few crossed waves, -1..1 (the edges of the maria
 *        and the mottling of the highlands).
 * @version 1
 * @utility
 * @return It.
 */
static float wobble(float x, float y, float f)
{
    return (sinf(x * f + 1.3f) * sinf(y * f * 0.83f - 0.7f) +
            0.6f * sinf((x + 0.6f * y) * f * 1.9f + 2.1f) * sinf((y - 0.4f * x) * f * 1.7f)) / 1.6f;
}

/**
 * @brief How much of a bright crater's floor and rays is at (@p x, @p y).
 * v2: skips points beyond its rays.
 * @version 2
 * @utility
 * @return 0..1.
 */
static float crater(const patch_t *c, float x, float y)
{
    float dx = x - c->x, dy = y - c->y, d2 = dx * dx + dy * dy;
    if (d2 > c->r * c->r * 81.0f) return 0.0f;   // beyond its rays
    float dist = sqrtf(d2);
    float floor_ = 1.0f - smooth(0.4f, 1.0f, dist / c->r);
    float spokes = powf(fabsf(sinf(atan2f(dy, dx) * 7.0f + c->x * 9.0f)), 12.0f);
    float rays = spokes * (1.0f - smooth(0.0f, c->r * 9.0f, dist)) * c->albedo;
    return clamp01(floor_ + 0.35f * rays);
}

/**
 * @brief The surface's brightness at unit-disc point (@p x, @p y): highlands 1, the maria
 *        about a quarter darker with ragged edges, bright craters and their rays, a fine
 *        mottling everywhere.
 * v2: ragged, merging maria (they were round spots); rayed craters.
 * v3: skips maria that cannot reach the point.
 * @version 3
 * @utility
 * @return The albedo.
 */
static float albedo(float x, float y)
{
    float wx = x + 0.10f * wobble(x, y, 7.0f), wy = y + 0.10f * wobble(y, x, 6.0f);
    float mare = 0.0f;
    for (size_t i = 0; i < sizeof(PATCHES) / sizeof(PATCHES[0]); i++) {
        float dx = wx - PATCHES[i].x, dy = wy - PATCHES[i].y, d2 = dx * dx + dy * dy;
        if (d2 >= PATCHES[i].r * PATCHES[i].r) continue;   // outside this mare
        float w = 1.0f - smooth(0.35f, 1.0f, sqrtf(d2) / PATCHES[i].r);
        mare = fmaxf(mare, w * (1.0f - PATCHES[i].albedo) / 0.26f);   // merge, don't stack
    }
    float a = 1.0f - 0.26f * clamp01(mare);
    for (size_t i = 0; i < sizeof(CRATERS) / sizeof(CRATERS[0]); i++) {
        a += 0.22f * crater(&CRATERS[i], x, y);
    }
    return a * (1.0f + 0.04f * wobble(x, y, 31.0f) + 0.025f * wobble(y, x, 67.0f));
}

/**
 * @brief One moon pixel at unit-disc point (@p x, @p y) lit from (@p lx, 0, @p lz).
 * @version 1
 * @utility
 * @return The pixel.
 */
static uint32_t moon_pixel(float x, float y, float lx, float lz, float edge, sky_rgb_t lit,
                           sky_rgb_t dark, float dark_a)
{
    float z = sqrtf(fmaxf(0.0f, 1.0f - x * x - y * y));   // towards us
    float mu0 = x * lx + z * lz;                           // the sun's height there
    float day = smooth(-0.04f, 0.06f, mu0);                // a soft terminator
    float shade = 0.45f + 0.55f * clamp01(2.0f * fmaxf(mu0, 0.0f) / (fmaxf(mu0, 0.0f) + z + 1e-3f));
    float alb = albedo(x, y);
    sky_rgb_t lit_c = scale(lit, clamp01(shade * alb));
    sky_rgb_t dark_c = mix(dark, lit, 0.10f * alb);        // earthshine shows the maria faintly
    return argb(mix(dark_c, lit_c, day), edge * (dark_a + (1.0f - dark_a) * day));
}

/**
 * @brief The moon.
 * @version 1
 * @req REQ-ASTRO-001
 */
void sky_paint_moon(uint32_t *out, int d, float age, sky_rgb_t lit, sky_rgb_t dark,
                    uint8_t dark_alpha)
{
    float e = age * 2.0f * PI_F;           // the sun's angle round from behind the moon
    float lx = sinf(e), lz = -cosf(e);     // waxing: lit from the right
    float r = d * SKY_MOON_R, c = d / 2.0f;
    for (int py = 0; py < d; py++) {
        for (int px = 0; px < d; px++) {
            float x = (px + 0.5f - c) / r, y = (c - py - 0.5f) / r;
            float rr = sqrtf(x * x + y * y);
            float edge = clamp01((1.0f - rr) * r + 0.5f);   // a one-pixel soft rim
            out[py * d + px] = edge > 0.0f
                                   ? moon_pixel(x, y, lx, lz, edge, lit, dark, dark_alpha / 255.0f)
                                   : 0;
        }
    }
}

/**
 * @brief Memory for a sun's tables.
 * v2: the glow and ray tables.
 * @version 2
 * @req REQ-ASTRO-001
 * @return Bytes.
 */
size_t sky_sun_bytes(int d)
{
    size_t n = (size_t)d * (size_t)d;
    return n * (sizeof(uint32_t) + sizeof(uint16_t) + 4 * sizeof(uint8_t)) + 8;
}

/**
 * @brief One pixel of the sun's disc at distance @p rr (disc radii) and point (@p x, @p y).
 * @version 1
 * @utility
 * @return The pixel (transparent outside).
 */
static uint32_t disc_pixel(const sky_sun_t *s, float rr, float x, float y, float edge)
{
    float limb = 1.0f - 0.18f * (1.0f - sqrtf(fmaxf(0.0f, 1.0f - rr * rr)));   // gently
    float grain = 1.0f + 0.03f * sinf(x * 37.0f) * sinf(y * 41.0f) * sinf((x - y) * 29.0f);
    sky_rgb_t c = mix(s->core, s->light, 0.35f * (1.0f - rr * rr));   // a hotter middle
    return argb(scale(c, clamp01(limb * grain)), edge);
}

/**
 * @brief How much of a ray of length @p len (disc radii) reaches @p rr: none inside the rim,
 *        fading out along it.
 * @version 1
 * @utility
 * @return 0..1.
 */
static float ray_reach(float rr, float len)
{
    float u = (rr - RAY_IN) / (len - RAY_IN);   // 0 at its root, 1 at its end
    return u < 0.0f || u > 1.0f ? 0.0f : powf(1.0f - u, 1.6f) * smooth(0.0f, 0.08f, u);
}

/**
 * @brief Fill pixel @p i's tables at (@p x, @p y) from the centre (pixels).
 * @version 1
 * @utility
 */
static void sun_tables_at(sky_sun_t *sun, size_t i, float x, float y)
{
    float half = sun->d / 2.0f, r = sun->d * SUN_R;
    float dist = sqrtf(x * x + y * y), rr = dist / r;
    float a = atan2f(y, x);
    sun->ang[i] = (uint16_t)((a < 0 ? a + 2 * PI_F : a) / (2 * PI_F) * 65535.0f);
    float fade = 1.0f - smooth(0.80f, 0.98f, dist / (half * DIAG) * DIAG);   // gone by the edge
    float glow = rr < 1.0f ? 0.0f : 0.45f * fade * expf(-powf((rr - 1.0f) * r / (sun->d * GLOW_W), 2.0f));
    sun->glow[i] = (uint8_t)(clamp01(glow) * 255.0f);
    sun->ray_l[i] = (uint8_t)(ray_reach(rr, RAY_LONG) * 255.0f);
    sun->ray_s[i] = (uint8_t)(ray_reach(rr, RAY_SHORT) * 255.0f);
    sun->narrow[i] = (uint8_t)fminf(255.0f, rr / RAY_HALF * 8.0f);
    float edge = clamp01(r - dist + 0.5f);
    sun->disc[i] = edge > 0.0f ? disc_pixel(sun, rr, x / r, y / r, edge) : 0;
}

/**
 * @brief Prepare a sun.
 * v2: the glow and ray profiles into tables (sun_tables_at).
 * @version 2
 * @req REQ-ASTRO-001
 */
void sky_sun_init(sky_sun_t *sun, void *mem, int d, sky_rgb_t core, sky_rgb_t light)
{
    size_t n = (size_t)d * (size_t)d;
    sun->d = d;
    sun->core = core;
    sun->light = light;
    sun->disc = (uint32_t *)mem;
    sun->ang = (uint16_t *)(sun->disc + n);
    sun->glow = (uint8_t *)(sun->ang + n);
    sun->ray_l = sun->glow + n;
    sun->ray_s = sun->ray_l + n;
    sun->narrow = sun->ray_s + n;
    float c = d / 2.0f;
    for (int py = 0; py < d; py++) {
        for (int px = 0; px < d; px++) {
            sun_tables_at(sun, (size_t)py * (size_t)d + (size_t)px, px + 0.5f - c, c - py - 0.5f);
        }
    }
}

/**
 * @brief Pixel @p top (straight alpha) over colour @p c at alpha @p a.
 * @version 1
 * @utility
 * @return The pixel.
 */
static uint32_t over(uint32_t top, sky_rgb_t c, float a)
{
    float ta = (top >> 24) / 255.0f, oa = ta + a * (1.0f - ta);
    if (oa <= 0.0f) return 0;
    float k = ta / oa;   // the top's share of the result
    sky_rgb_t t = { (uint8_t)(top >> 16), (uint8_t)(top >> 8), (uint8_t)top };
    return argb(mix(c, t, k), oa);
}

/** exp(-x^2) for x = i / 32: the rays' sideways profile. */
static float s_gauss[256];

/**
 * @brief Fill s_gauss once.
 * @version 1
 * @utility
 */
static void gauss_init(void)
{
    if (s_gauss[0] > 0.0f) return;
    for (int i = 0; i < 256; i++) s_gauss[i] = expf(-(i / 32.0f) * (i / 32.0f));
}

/**
 * @brief The sun at phase @p t.
 * v2: from the tables: an angle, a lookup and a few products per pixel.
 * @version 2
 * @req REQ-ASTRO-001
 */
void sky_sun_frame(const sky_sun_t *s, float t, uint32_t *out)
{
    gauss_init();
    size_t n = (size_t)s->d * (size_t)s->d;
    float breath = (0.80f + 0.20f * sinf(t * 2.0f * PI_F)) / 255.0f;
    float turn = t / RAYS;   // one ray's spacing per cycle: it loops seamlessly
    sky_rgb_t ray_c = mix(s->core, s->light, 0.25f);
    for (size_t i = 0; i < n; i++) {
        if (s->disc[i] >> 24 == 255 || (!s->glow[i] && !s->ray_l[i])) {
            out[i] = s->disc[i];
            continue;
        }
        float slot = (s->ang[i] / 65535.0f + turn) * RAYS;
        float k = floorf(slot + 0.5f);
        float off = fabsf(slot - k) * (2.0f * PI_F / RAYS) * s->narrow[i] * (32.0f / 8.0f);
        float reach = ((int)k & 1) ? s->ray_s[i] : s->ray_l[i];
        float ray = 0.70f / 255.0f * reach * s_gauss[off < 255.0f ? (int)off : 255];
        float glow = s->glow[i] * breath;
        float a = ray + glow * (1.0f - ray);
        sky_rgb_t c = mix(s->core, ray_c, ray / (a + 1e-3f));
        out[i] = over(s->disc[i], c, a);   // the disc's soft rim over the glow behind it
    }
}
