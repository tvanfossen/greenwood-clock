/**
 * @file sky_panel.h
 * v2: only the shared pieces (the astronomy screen is gone; the forecast's sky dial shows
 *     the sky).
 * @version 2
 * @brief The sky's shared pieces: a sky card and the moon as it looks. Caller holds the LVGL
 *        lock.
 */
#pragma once

#include "lvgl.h"
#include "sky_data.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A sky card on @p parent at (@p x, @p y), @p w x @p h: the card slot's fill at 70 %
 *        over the picture, rounded, padded 16, not scrolling.
 * @version 1
 * @req REQ-ASTRO-001
 * @return The card.
 */
lv_obj_t *sky_card_create(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h);

/**
 * @brief The moon as it looks: a @p diameter px square at (@p x, @p y) in @p parent, the
 *        moon at @p d's phase (waxing lit on the right), its dark side faint at night only.
 * v2: drawn natively (sky_body).
 * @version 2
 * @req REQ-ASTRO-001
 * @return The disc (a canvas; its buffer is freed with it), or NULL when out of memory.
 */
lv_obj_t *sky_moon_create(lv_obj_t *parent, const sky_data_t *d, int32_t diameter, int32_t x,
                          int32_t y);


#ifdef __cplusplus
}
#endif
