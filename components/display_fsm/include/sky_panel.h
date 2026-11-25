/**
 * @file sky_panel.h
 * @version 1
 * @brief Sky screen (prototype): the astronomy and ambient screens merged. A sun card (the
 *        day as an arc from sunrise to sunset with the sun on it, daylight left, golden hour,
 *        aurora outlook, next season change and meteor shower) and a moon card (the phase
 *        drawn as lit and shadowed disc, illumination, days to full and new) under the
 *        minimised clock.
 *
 * sky_panel_create() only draws what it is given (sky_data.h computes it), so the host
 * preview renders it with any date. Caller holds the LVGL lock.
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
 * @brief The moon as it looks: a @p diameter px disc at (@p x, @p y) in @p parent, lit by
 *        @p d's illumination from the right while waxing (northern hemisphere), the shadowed
 *        part a deep tone, with a rim.
 * @version 1
 * @req REQ-ASTRO-001
 * @return The disc (a canvas; its buffer is freed with it), or NULL when out of memory.
 */
lv_obj_t *sky_moon_create(lv_obj_t *parent, const sky_data_t *d, int32_t diameter, int32_t x,
                          int32_t y);

/**
 * @brief Build the sky screen's cards on @p screen from @p d.
 * @version 1
 * @req REQ-ASTRO-001
 * @req REQ-AMB-001
 * @return A transparent holder of both cards (delete it to remove them).
 */
lv_obj_t *sky_panel_create(lv_obj_t *screen, const sky_data_t *d);

#ifdef __cplusplus
}
#endif
