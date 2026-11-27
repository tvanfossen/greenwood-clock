/**
 * @file info_panel.h
 * v2: themed fill; info_panel_palette() gives the panel's legible text colours.
 * v3: fill and label colours are theme bindings (labels take a role, not a colour).
 * @version 3
 * @brief Shared builders for the information panel used by the astronomy and ambient screens.
 */
#pragma once

#include "lvgl.h"
#include "display_widgets.h"   // ALERT_BANNER_HEIGHT
#include "ui_theme_bind.h"

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
 * @brief Create the rounded, non-scrolling 556x470 surface panel below the alert-banner zone.
 * v2: fill from the theme's SURFACE role.
 * v3: fill bound to the card slot.
 * v4: at the top (the banner moved to the bottom).
 * @version 4
 * @utility
 * @return The panel container.
 */
static inline lv_obj_t *info_panel_create(lv_obj_t *screen)
{
    // Semi-opaque surface panel at the top (the alert banner is at the bottom).
    lv_obj_t *c = lv_obj_create(screen);
    lv_obj_set_size(c, 556, 470);
    lv_obj_align(c, LV_ALIGN_TOP_LEFT, 16, 16);
    ui_bind_fill(c, UI_SLOT_CARD);
    lv_obj_set_style_bg_opa(c, LV_OPA_70, 0);
    lv_obj_set_style_radius(c, 12, 0);
    lv_obj_set_style_border_width(c, 0, 0);
    lv_obj_set_style_pad_all(c, 16, 0);
    lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    return c;
}

/**
 * @brief Create a top-left-aligned label in @p parent with the given font, text role (bound,
 *        legible against the panel) and y offset.
 * v2: takes a role (bound) instead of a colour.
 * @version 2
 * @utility
 * @return The new label.
 */
static inline lv_obj_t *info_panel_label(lv_obj_t *parent, const lv_font_t *font,
                                         ui_role_t role, int y)
{
    lv_obj_t *lbl = lv_label_create(parent);
    lv_obj_set_style_text_font(lbl, font, 0);
    lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, 0, y);
    ui_bind_text(lbl, role);
    return lbl;
}

#ifdef __cplusplus
}
#endif
