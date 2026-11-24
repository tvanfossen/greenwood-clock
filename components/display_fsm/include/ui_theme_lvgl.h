/**
 * @file ui_theme_lvgl.h
 * @version 1
 * @brief The OKLCH theme as an LVGL theme, so stock widgets draw with the theme's roles.
 *        Caller holds the LVGL lock.
 */
#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Install the themed LVGL theme on @p disp (once; later calls only recolour).
 * @version 1
 * @req REQ-UI-003
 */
void ui_theme_lvgl_init(lv_display_t *disp);

/**
 * @brief The colour text on @p panel actually sits on: its fill at its bg opacity over the
 *        mean of @p bg under it (the panel's screen colour without a picture).
 * @version 1
 * @req REQ-BG-002
 * @req REQ-UI-003
 * @return The composite backdrop colour.
 */
lv_color_t ui_theme_panel_backdrop(lv_obj_t *panel, const lv_image_dsc_t *bg);

/**
 * @brief Recolour the themed styles from the current ui_theme and restyle every object.
 * @version 1
 * @req REQ-UI-003
 */
void ui_theme_lvgl_refresh(void);

#ifdef __cplusplus
}
#endif
