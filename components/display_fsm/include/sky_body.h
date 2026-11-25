/**
 * @file sky_body.h
 * v2: the sky's events layer (aurora, meteors).
 * @version 2
 * @brief The sun and the moon as LVGL objects, drawn natively (sky_paint) in the theme's
 *        roles: the sun in the accent with highlights towards the near-white, its rays turning
 *        and its glow breathing slowly (an 8 s loop at 10 frames a second); the moon in the
 *        near-white at its real phase, its dark side a dim tone of the theme. Used by the sky
 *        dial, the astronomy card and the clear-sky weather icon. Caller holds the LVGL lock.
 */
#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief An animated sun in a @p d px square (its disc 60 % of it; rays and glow round it).
 * @version 1
 * @req REQ-ASTRO-001
 * @return The object, or NULL when out of memory.
 */
lv_obj_t *sky_body_sun_create(lv_obj_t *parent, int32_t d);

/**
 * @brief The moon at @p age (0 new, 0.5 full; waxing lit on the right) in a @p d px square
 *        (its disc 92 % of it), its dark side at @p dark_opa (0 by day: only the lit part).
 * @version 1
 * @req REQ-ASTRO-001
 * @return The object, or NULL when out of memory.
 */
lv_obj_t *sky_body_moon_create(lv_obj_t *parent, int32_t d, float age, lv_opa_t dark_opa);

/**
 * @brief The sky's events over a @p w x @p h window: an aurora of @p aurora strength (0:
 *        none) and meteors at @p zhr (0: none), randomised from @p night (the same night
 *        looks the same), in the theme's aurora and meteor roles, at 15 frames a second.
 * @version 1
 * @req REQ-ASTRO-001
 * @return The layer, or NULL when there is nothing to show or no memory.
 */
lv_obj_t *sky_body_events_create(lv_obj_t *parent, int32_t w, int32_t h, unsigned night,
                                 float aurora, int zhr);

#ifdef __cplusplus
}
#endif
