/**
 * @file sky_dial.h
 * @version 1
 * @brief Sky dial: a small card with the sun and the moon on a wheel behind the horizon, each
 *        where it is in the sky now (sky_sun_angle / sky_moon_angle; the moon drawn in its
 *        phase), and the next sunrise and sunset beside it. Built for the weather screen's
 *        space under the minimised clock. Caller holds the LVGL lock.
 */
#pragma once

#include "lvgl.h"
#include "sky_data.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Build the dial card on @p parent at (@p x, @p y), @p w x @p h, from @p d.
 * @version 1
 * @req REQ-WX-007
 * @return The card (delete it to remove the dial).
 */
lv_obj_t *sky_dial_create(lv_obj_t *parent, const sky_data_t *d, int32_t x, int32_t y,
                          int32_t w, int32_t h);

#ifdef __cplusplus
}
#endif
