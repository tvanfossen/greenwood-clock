/**
 * @file sky_paint.h
 * v2: the sun's per-pixel glow and ray profiles precomputed: a frame is table lookups only.
 * v3: the sky's events: an aurora and meteors, randomised per night.
 * @version 3
 * @brief The sun and the moon as pixels (ARGB8888, straight alpha, d x d), for the sky dial
 *        and the clear-sky weather icon. The moon is the near side as it really looks (its
 *        maria and bright craters) lit from the sun's direction for its age, waxing lit on
 *        the right; its dark side shows a faint earthshine at night and nothing by day. The
 *        sun is a limb-darkened disc in a glow, with rays that turn: its disc is painted once
 *        and each frame composites only the rays and the glow, from per-pixel tables. Colours
 *        come in (the caller's theme roles); none are written here. Pure C.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** The moon's radius as a share of the square's side. */
#define SKY_MOON_R 0.46f

/** A colour. */
typedef struct {
    uint8_t r, g, b;
} sky_rgb_t;

/** A sun ready to animate: its tables (memory the caller gives, sky_sun_bytes long). */
typedef struct {
    int       d;
    sky_rgb_t core, light;
    uint32_t *disc;    /**< The disc alone (glow and rays left transparent). */
    uint16_t *ang;     /**< Each pixel's angle about the centre (65536 = a turn). */
    uint8_t  *glow;    /**< Each pixel's glow at full breath (255 = opaque). */
    uint8_t  *ray_l;   /**< How much of a long ray reaches each pixel (255: all). */
    uint8_t  *ray_s;   /**< ... of a short ray. */
    uint8_t  *narrow;  /**< How fast a ray narrows sideways there (its distance x 8 / spread). */
} sky_sun_t;

/**
 * @brief The moon at @p age (0 new, 0.5 full) into @p out (@p d x @p d), lit in @p lit, its
 *        dark side in @p dark at alpha @p dark_alpha (0 by day).
 * @version 1
 * @req REQ-ASTRO-001
 */
void sky_paint_moon(uint32_t *out, int d, float age, sky_rgb_t lit, sky_rgb_t dark,
                    uint8_t dark_alpha);

/**
 * @brief The memory a @p d x @p d sun's tables need.
 * @version 1
 * @req REQ-ASTRO-001
 * @return Bytes.
 */
size_t sky_sun_bytes(int d);

/**
 * @brief Prepare @p sun in @p mem (sky_sun_bytes(@p d)): its tables and disc, in @p core with
 *        highlights towards @p light.
 * @version 1
 * @req REQ-ASTRO-001
 */
void sky_sun_init(sky_sun_t *sun, void *mem, int d, sky_rgb_t core, sky_rgb_t light);

/**
 * @brief The sun at animation phase @p t (0..1: the rays turn one ray's spacing and the glow
 *        breathes once) into @p out (d x d).
 * @version 1
 * @req REQ-ASTRO-001
 */
void sky_sun_frame(const sky_sun_t *sun, float t, uint32_t *out);

#define SKY_AURORA_BANDS 3
#define SKY_METEORS      4

/** One aurora curtain: where its foot runs, how it folds, how it drifts. */
typedef struct {
    float base, sway, fold, drift;      /**< Foot height (share of h), its swing, fold freq, speed. */
    float phase, stripe, stripe_speed;  /**< Fold phase; the rays' spacing and their shimmer. */
    float height, bright;               /**< Its height (share of h) and brightness. */
} sky_band_t;

/** A meteor in flight. */
typedef struct {
    float    x, y;                      /**< Where it started. */
    uint32_t born_ms;                   /**< 0: none. */
} sky_meteor_t;

/** The sky's events over a w x h window: an aurora (randomised from the night's seed) and a
 *  meteor shower at its ZHR. */
typedef struct {
    int          w, h;
    uint32_t     rng;
    float        aurora;                /**< Strength 0..1 (0: none). */
    int          zhr;                   /**< 0: no meteors. */
    float        violet;                /**< How much of the tops turns violet (per night). */
    int          bands;
    sky_band_t   band[SKY_AURORA_BANDS];
    float        dir_x, dir_y;          /**< The meteors' direction (from the night's radiant). */
    sky_meteor_t meteor[SKY_METEORS];
    uint32_t     next_ms;               /**< When the next meteor starts. */
    sky_rgb_t    green, violet_c, white;
} sky_events_t;

/**
 * @brief Prepare the events for a @p w x @p h window: an aurora of @p aurora strength (0:
 *        none) and meteors at @p zhr (0: none), everything random drawn from @p seed (the
 *        night: the same night looks the same), in @p green / @p violet / @p white.
 * @version 1
 * @req REQ-ASTRO-001
 */
void sky_events_init(sky_events_t *e, int w, int h, uint32_t seed, float aurora, int zhr,
                     sky_rgb_t green, sky_rgb_t violet, sky_rgb_t white);

/**
 * @brief The events at @p ms (since they began) into @p out (w x h; transparent where none).
 * @version 1
 * @req REQ-ASTRO-001
 */
void sky_events_frame(sky_events_t *e, uint32_t ms, uint32_t *out);

#ifdef __cplusplus
}
#endif
