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
 * @brief Recolour the themed styles from the current ui_theme and restyle every object.
 * @version 1
 * @req REQ-UI-003
 */
void ui_theme_lvgl_refresh(void);

#ifdef __cplusplus
}
#endif
