/**
 * @file theme_editor_preview.c
 * @version 1
 * @brief The theme editor's design preview: the clock, a card, a forecast strip, the four
 *        alert severities and stock controls, built from the same slots and roles as the real
 *        screens and every colour bound, so it repaints with each edit. The pane is
 *        transparent: the card sits on the real background picture, as on the weather screen.
 */

#include "theme_editor_priv.h"
#include "ui_theme_bind.h"

LV_FONT_DECLARE(nunito_48);

/**
 * @brief A bound label: @p text in @p font, coloured @p role, at (@p x, @p y) in @p parent.
 * @version 1
 * @utility
 * @return The label.
 */
static lv_obj_t *label(lv_obj_t *parent, const char *text, const lv_font_t *font,
                       ui_role_t role, int32_t x, int32_t y)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_pos(l, x, y);
    ui_bind_text(l, role);
    return l;
}

/**
 * @brief A panel filled by @p slot at @p opa, @p w x @p h at (@p x, @p y).
 * @version 1
 * @utility
 * @return The panel.
 */
static lv_obj_t *panel(lv_obj_t *parent, ui_slot_t slot, lv_opa_t opa, int32_t x, int32_t y,
                       int32_t w, int32_t h)
{
    lv_obj_t *p = lv_obj_create(parent);
    lv_obj_set_size(p, w, h);
    lv_obj_set_pos(p, x, y);
    lv_obj_set_style_bg_opa(p, opa, 0);
    lv_obj_set_style_border_width(p, 0, 0);
    lv_obj_set_style_radius(p, 10, 0);
    lv_obj_set_style_pad_all(p, 10, 0);
    lv_obj_remove_flag(p, LV_OBJ_FLAG_SCROLLABLE);
    ui_bind_fill(p, slot);
    return p;
}

/**
 * @brief The card and strip: what panel text looks like over the picture.
 * @version 1
 * @utility
 */
static void preview_panels(lv_obj_t *pane)
{
    label(pane, "12:09 PM", &nunito_48, UI_ROLE_TEXT, 250, 0);
    lv_obj_t *card = panel(pane, UI_SLOT_CARD, LV_OPA_70, 0, 64, 300, 150);
    label(card, "32F", &nunito_48, UI_ROLE_TEXT, 0, 0);
    label(card, "Partly Cloudy", &lv_font_montserrat_24, UI_ROLE_TEXT, 0, 58);
    label(card, "Feels like 27F  Wind SE 7", &lv_font_montserrat_16, UI_ROLE_TEXT_MUTED, 0, 92);
    label(card, "Sunrise 7:47", &lv_font_montserrat_16, UI_ROLE_ACCENT, 0, 112);
    label(card, "Aurora: low", &lv_font_montserrat_16, UI_ROLE_ACCENT_ALT, 140, 112);
    lv_obj_t *strip = panel(pane, UI_SLOT_STRIP, LV_OPA_80, 0, 222, 490, 56);
    static const char *const DAYS[] = { "Tue 39/23", "Wed 41/25", "Thu 43/27", "Fri 45/29" };
    for (int i = 0; i < 4; i++) {
        label(strip, DAYS[i], &lv_font_montserrat_16, UI_ROLE_TEXT, i * 118, 6);
    }
}

/**
 * @brief The four alert severities, each on its slot's fill.
 * @version 1
 * @utility
 */
static void preview_alerts(lv_obj_t *pane)
{
    static const ui_slot_t SLOTS[4] = { UI_SLOT_ALERT_EXTREME, UI_SLOT_ALERT_SEVERE,
                                        UI_SLOT_ALERT_MODERATE, UI_SLOT_ALERT_MINOR };
    static const char *const TEXT[4] = { "Extreme: tornado warning", "Severe: winter storm",
                                         "Moderate: wind advisory", "Minor: frost advisory" };
    for (int i = 0; i < 4; i++) {
        lv_obj_t *a = panel(pane, SLOTS[i], LV_OPA_90, 0, 288 + i * 36, 490, 32);
        lv_obj_set_style_radius(a, 4, 0);
        lv_obj_set_style_pad_ver(a, 4, 0);
        label(a, TEXT[i], &lv_font_montserrat_16, UI_ROLE_TEXT, 0, 0);
    }
}

/**
 * @brief Stock controls (themed by the LVGL theme) on a dialog panel.
 * @version 1
 * @utility
 */
static void preview_controls(lv_obj_t *pane)
{
    lv_obj_t *dlg = panel(pane, UI_SLOT_DIALOG, LV_OPA_90, 310, 64, 180, 150);
    lv_obj_t *btn = lv_button_create(dlg);
    lv_obj_set_pos(btn, 0, 0);
    lv_label_set_text(lv_label_create(btn), "Button");
    lv_obj_t *sw = lv_switch_create(dlg);
    lv_obj_set_pos(sw, 0, 48);
    lv_obj_add_state(sw, LV_STATE_CHECKED);
    lv_obj_t *sl = lv_slider_create(dlg);
    lv_obj_set_size(sl, 150, 10);
    lv_obj_set_pos(sl, 0, 100);
    lv_slider_set_value(sl, 60, LV_ANIM_OFF);
    label(dlg, "hint", &lv_font_montserrat_16, UI_ROLE_TEXT_FAINT, 80, 52);
}

/**
 * @brief Build the design preview in @p pane.
 * @version 1
 * @req REQ-UI-004
 */
void te_preview_create(lv_obj_t *pane)
{
    preview_panels(pane);
    preview_alerts(pane);
    preview_controls(pane);
}
