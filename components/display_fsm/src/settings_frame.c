/**
 * @file settings_frame.c
 * v2: settings_text.
 * @version 2
 * @brief Settings screen layout: the sub-screen frame and the menu grid (see settings_frame.h).
 */

#include "settings_frame.h"
#include "ui_theme_bind.h"

#define FRAME_PAD   20
#define TITLE_ROW   64     // title and Back button row, above the content
#define GRID_GAP    10
#define ITEM_H      62

/**
 * @brief The sub-screen frame: centred dialog panel, title, Back.
 * @version 1
 * @req REQ-UI-001
 * @return The frame.
 */
lv_obj_t *settings_frame_create(lv_obj_t *parent, const char *title, lv_event_cb_t back_cb)
{
    lv_obj_t *cont = lv_obj_create(parent);
    lv_obj_set_size(cont, SETTINGS_FRAME_W, SETTINGS_FRAME_H);
    lv_obj_align(cont, LV_ALIGN_TOP_MID, 0, SETTINGS_FRAME_Y);   // below the top-bar clock
    lv_obj_set_style_bg_opa(cont, LV_OPA_90, 0);
    lv_obj_set_style_border_width(cont, 1, 0);
    lv_obj_set_style_radius(cont, 16, 0);
    lv_obj_set_style_pad_all(cont, FRAME_PAD, 0);
    lv_obj_remove_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
    ui_bind_fill(cont, UI_SLOT_DIALOG);
    ui_bind_border(cont, UI_ROLE_BORDER);

    lv_obj_t *lbl = lv_label_create(cont);
    lv_label_set_text(lbl, title);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_32, 0);
    lv_obj_align(lbl, LV_ALIGN_TOP_MID, 0, 0);
    ui_bind_text(lbl, UI_ROLE_TEXT);

    lv_obj_t *btn = lv_button_create(cont);
    lv_obj_set_size(btn, 120, 50);
    lv_obj_align(btn, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_add_event_cb(btn, back_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *btn_lbl = lv_label_create(btn);
    lv_label_set_text(btn_lbl, LV_SYMBOL_LEFT " Back");
    lv_obj_center(btn_lbl);
    return cont;
}

/**
 * @brief A themed label.
 * @version 1
 * @req REQ-UI-001
 * @return The label.
 */
lv_obj_t *settings_text(lv_obj_t *parent, const char *s, const lv_font_t *font, ui_role_t role)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, s);
    lv_obj_set_style_text_font(l, font, 0);
    ui_bind_text(l, role);
    return l;
}

/**
 * @brief One menu entry: a button with the icon and label, left-aligned.
 * @version 1
 * @utility
 */
static void menu_item(lv_obj_t *grid, const settings_menu_item_t *it, int32_t w)
{
    lv_obj_t *b = lv_button_create(grid);
    lv_obj_set_size(b, w, ITEM_H);
    lv_obj_add_event_cb(b, it->cb, LV_EVENT_CLICKED, it->user);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text_fmt(l, "%s   %s", it->icon, it->label);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_24, 0);
    lv_obj_align(l, LV_ALIGN_LEFT_MID, 8, 0);
}

/**
 * @brief Lay the menu entries out in two columns below the title row.
 * @version 1
 * @req REQ-UI-001
 * @return The grid container.
 */
lv_obj_t *settings_menu_grid(lv_obj_t *frame, const settings_menu_item_t *items, int n)
{
    int32_t inner_w = SETTINGS_FRAME_W - 2 * FRAME_PAD - 2;
    int32_t inner_h = SETTINGS_FRAME_H - 2 * FRAME_PAD - 2 - TITLE_ROW;
    lv_obj_t *grid = lv_obj_create(frame);
    lv_obj_remove_style_all(grid);
    lv_obj_set_size(grid, inner_w, inner_h);
    lv_obj_align(grid, LV_ALIGN_TOP_LEFT, 0, TITLE_ROW);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_row(grid, GRID_GAP, 0);
    lv_obj_set_style_pad_column(grid, GRID_GAP, 0);
    lv_obj_remove_flag(grid, LV_OBJ_FLAG_SCROLLABLE);
    for (int i = 0; i < n; i++) menu_item(grid, &items[i], (inner_w - GRID_GAP) / 2);
    return grid;
}
