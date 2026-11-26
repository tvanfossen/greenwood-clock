/**
 * @file settings_location.h
 * @version 1
 * @brief Settings > Location: where the clock is and its timezone. The place is set from an
 *        address (looked up once by the backend, confirmed before it is kept); the zone is any
 *        IANA zone, picked by region and place on two wheels. Pure LVGL over the settings
 *        backend. Caller holds the LVGL lock.
 */
#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief The Location screen (a settings_screen_fn).
 * @version 1
 * @req REQ-LOC-001
 * @return The frame.
 */
lv_obj_t *settings_location_screen(lv_obj_t *parent, void *ctx);

#ifdef __cplusplus
}
#endif
