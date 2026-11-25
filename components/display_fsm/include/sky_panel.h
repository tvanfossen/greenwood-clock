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
