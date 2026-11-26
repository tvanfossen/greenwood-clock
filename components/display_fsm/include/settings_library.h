/**
 * @file settings_library.h
 * @version 1
 * @brief Settings > Library (REQ-LIB-001): what the clock holds by kind (backgrounds, the
 *        album, themes, clock animations): a grid of thumbnails or a list, the card's space,
 *        and each item's detail (who uses it, Use in a profile, Rename, Delete; a theme's
 *        Edit and Duplicate). Over the settings backend's library calls, on the clock and the
 *        phone alike. Caller holds the LVGL lock.
 */
#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief The Library's first screen: its four kinds (a settings screen, settings_app_push).
 * @version 1
 * @req REQ-LIB-001
 * @return The frame.
 */
lv_obj_t *settings_library_screen(lv_obj_t *parent, void *ctx);

#ifdef __cplusplus
}
#endif
