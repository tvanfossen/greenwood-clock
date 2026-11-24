/**
 * @file info_panel.h
 * v2: themed fill; info_panel_palette() gives the panel's legible text colours.
 * @version 2
 * @brief Shared builders for the information panel used by the astronomy and ambient screens.
 */
#pragma once

#include "lvgl.h"
#include "display_fsm.h"       // display_fsm_panel_backdrop
#include "display_widgets.h"   // ALERT_BANNER_HEIGHT
#include "ui_theme.h"

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

/** Text colours for one panel, each legible against the panel's real backdrop. */
typedef struct {
    lv_color_t text;         /**< Headings and primary values. */
    lv_color_t muted;        /**< Secondary lines. */
    lv_color_t accent;       /**< Sun times. */
    lv_color_t accent_alt;   /**< Aurora. */
} info_panel_palette_t;

/**
 * @brief Create the rounded, non-scrolling 556x470 surface panel below the alert-banner zone.
 * v2: fill from the theme's SURFACE role.
 * @version 2
 * @utility
 * @return The panel container.
 */
static inline lv_obj_t *info_panel_create(lv_obj_t *screen)
{
    // Semi-opaque surface panel. Y=66: reserves 50px alert banner zone + 16px margin.
    lv_obj_t *c = lv_obj_create(screen);
    lv_obj_set_size(c, 556, 470);
    lv_obj_align(c, LV_ALIGN_TOP_LEFT, 16, ALERT_BANNER_HEIGHT + 16);
    lv_obj_set_style_bg_color(c, ui_theme_color(UI_ROLE_SURFACE), 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_70, 0);
    lv_obj_set_style_radius(c, 12, 0);
    lv_obj_set_style_border_width(c, 0, 0);
    lv_obj_set_style_pad_all(c, 16, 0);
    lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    return c;
}

/**
 * @brief The theme's text colours for labels on @p panel, judged once against its real
 *        backdrop (fill over the picture under it).
 * @version 1
 * @utility
 * @return The palette.
 */
static inline info_panel_palette_t info_panel_palette(lv_obj_t *panel)
{
    lv_color_t bd = display_fsm_panel_backdrop(panel);
    info_panel_palette_t p = {
        ui_theme_text_on(UI_ROLE_TEXT, bd), ui_theme_text_on(UI_ROLE_TEXT_MUTED, bd),
        ui_theme_text_on(UI_ROLE_ACCENT, bd), ui_theme_text_on(UI_ROLE_ACCENT_ALT, bd),
    };
    return p;
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
