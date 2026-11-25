/**
 * @file sky_paint.h
 * @version 1
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
    uint8_t  *rad;     /**< Each pixel's distance from the centre (255 = the corner). */
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

#ifdef __cplusplus
}
#endif
