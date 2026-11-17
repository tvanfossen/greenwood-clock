/**
 * @file info_panel.h
 * @version 1
 * @brief Shared builders for the dark information panel used by the astronomy and ambient screens.
 */
#pragma once

#include "lvgl.h"
#include "display_widgets.h"   // ALERT_BANNER_HEIGHT

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Normalize a minutes-of-day value into [0, 1440).
 * @version 1
 * @utility
 * @return Wrapped minutes.
 */
static inline int info_panel_wrap_day_minutes(int m)
{
    m %= 1440;
    if (m < 0) m += 1440;
    return m;
}

/**
 * @brief Create the dark, rounded, non-scrolling 556x470 panel below the alert-banner zone.
 * @version 1
 * @utility
 * @return The panel container.
 */
static inline lv_obj_t *info_panel_create(lv_obj_t *screen)
{
    // Container with dark semi-opaque backdrop for contrast.
    // Y=66: reserves 50px alert banner zone + 16px margin.
    lv_obj_t *c = lv_obj_create(screen);
    lv_obj_set_size(c, 556, 470);
    lv_obj_align(c, LV_ALIGN_TOP_LEFT, 16, ALERT_BANNER_HEIGHT + 16);
    lv_obj_set_style_bg_color(c, lv_color_hex(0x0a0a1e), 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_70, 0);
    lv_obj_set_style_radius(c, 12, 0);
    lv_obj_set_style_border_width(c, 0, 0);
    lv_obj_set_style_pad_all(c, 16, 0);
    lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    return c;
}

/**
 * @brief Create a top-left-aligned label in @p parent with the given font, colour and y offset.
 * @version 1
 * @utility
 * @return The new label.
 */
static inline lv_obj_t *info_panel_label(lv_obj_t *parent, const lv_font_t *font,
                                         lv_color_t color, int y)
{
    lv_obj_t *lbl = lv_label_create(parent);
    lv_obj_set_style_text_font(lbl, font, 0);
    lv_obj_set_style_text_color(lbl, color, 0);
    lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, 0, y);
    return lbl;
}

#ifdef __cplusplus
}
#endif
