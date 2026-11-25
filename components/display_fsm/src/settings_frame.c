/**
 * @file settings_frame.c
 * v2: settings_text.
 * v3: the settings kit; a settable frame top.
 * v4: the frame area per display; narrow frames stack labels over values.
 * @version 4
 * @brief Settings screen layout and widget kit (see settings_frame.h).
 */

#include "settings_frame.h"
#include "ui_theme_bind.h"

#define FRAME_PAD   20
#define TITLE_ROW   64     // title and Back button row, above the content
#define GRID_GAP    10
#define ITEM_H      62
#define ROW_GAP     8
#define LABEL_W     190
#define TITLE_BTN_W 120    // Back and the title-row button
#define TITLE_BTN_NARROW_W 96
#define BACK_NARROW_W      56   // the arrow alone

static int32_t s_frame_y = SETTINGS_FRAME_Y;
static int32_t s_frame_w = SETTINGS_FRAME_W;
static int32_t s_frame_h = SETTINGS_FRAME_H;

/**
 * @brief Where frames go.
 * @version 1
 * @req REQ-UI-001
 */
void settings_frame_set_area(int32_t y, int32_t w, int32_t h)
{
    s_frame_y = y;
    s_frame_w = w;
    s_frame_h = h;
}

/**
 * @brief The frames' top edge.
 * @version 1
 * @req REQ-UI-001
 * @return It.
 */
int32_t settings_frame_get_y(void)
{
    return s_frame_y;
}

/**
 * @brief The content width.
 * @version 1
 * @req REQ-UI-001
 * @return Pixels.
 */
int32_t settings_content_w(void)
{
    return s_frame_w - 2 * FRAME_PAD - 2;
}

/**
 * @brief The content height.
 * @version 1
 * @req REQ-UI-001
 * @return Pixels.
 */
int32_t settings_content_h(void)
{
    return s_frame_h - 2 * FRAME_PAD - 2 - TITLE_ROW;
}

/**
 * @brief Whether frames are narrow.
 * @version 1
 * @req REQ-UI-001
 * @return Whether.
 */
bool settings_narrow(void)
{
    return s_frame_w < SETTINGS_NARROW_W;
}

/**
 * @brief One line of @p label, @p w wide, dotted when longer.
 * @version 1
 * @req REQ-UI-001
 */
void settings_one_line(lv_obj_t *label, int32_t w)
{
    const lv_font_t *font = lv_obj_get_style_text_font(label, LV_PART_MAIN);
    lv_obj_set_size(label, w, lv_font_get_line_height(font));
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_DOTS);
}

/**
 * @brief The sub-screen frame: centred dialog panel, title, Back.
 * v2: placed at the settable top (settings_frame_set_y).
 * v3: no Back button when @p back_cb is NULL (nowhere to go back to).
 * v4: the area's size; narrow: a smaller title and an arrow-only Back.
 * @version 4
 * @req REQ-UI-001
 * @return The frame.
 */
lv_obj_t *settings_frame_create(lv_obj_t *parent, const char *title, lv_event_cb_t back_cb)
{
    lv_obj_t *cont = lv_obj_create(parent);
    lv_obj_set_size(cont, s_frame_w, s_frame_h);
    lv_obj_align(cont, LV_ALIGN_TOP_MID, 0, s_frame_y);   // below the top-bar clock, or centred
    lv_obj_set_style_bg_opa(cont, LV_OPA_90, 0);
    lv_obj_set_style_border_width(cont, 1, 0);
    lv_obj_set_style_radius(cont, 16, 0);
    lv_obj_set_style_pad_all(cont, FRAME_PAD, 0);
    lv_obj_remove_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
    ui_bind_fill(cont, UI_SLOT_DIALOG);
    ui_bind_border(cont, UI_ROLE_BORDER);

    bool narrow = settings_narrow();
    lv_obj_t *lbl = lv_label_create(cont);
    lv_label_set_text(lbl, title);
    lv_obj_set_style_text_font(lbl, narrow ? &lv_font_montserrat_20 : &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
    if (narrow) {   // between Back and the title-row button
        settings_one_line(lbl, settings_content_w() - BACK_NARROW_W - TITLE_BTN_NARROW_W - 16);
        lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, BACK_NARROW_W + 8, 14);
    } else {
        settings_one_line(lbl, settings_content_w() - 2 * TITLE_BTN_W - 8);
        lv_obj_align(lbl, LV_ALIGN_TOP_MID, 0, 0);
    }
    ui_bind_text(lbl, UI_ROLE_TEXT);

    if (!back_cb) return cont;
    lv_obj_t *btn = lv_button_create(cont);
    lv_obj_set_size(btn, narrow ? BACK_NARROW_W : TITLE_BTN_W, 50);
    lv_obj_align(btn, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_add_event_cb(btn, back_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *btn_lbl = lv_label_create(btn);
    lv_label_set_text(btn_lbl, narrow ? LV_SYMBOL_LEFT : LV_SYMBOL_LEFT " Back");
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
 * v2: the area's size; one column when narrow.
 * @version 2
 * @req REQ-UI-001
 * @return The grid container.
 */
lv_obj_t *settings_menu_grid(lv_obj_t *frame, const settings_menu_item_t *items, int n)
{
    int32_t inner_w = settings_content_w();
    int32_t inner_h = settings_content_h();
    int32_t item_w = settings_narrow() ? inner_w : (inner_w - GRID_GAP) / 2;
    lv_obj_t *grid = lv_obj_create(frame);
    lv_obj_remove_style_all(grid);
    lv_obj_set_size(grid, inner_w, inner_h);
    lv_obj_align(grid, LV_ALIGN_TOP_LEFT, 0, TITLE_ROW);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_row(grid, GRID_GAP, 0);
    lv_obj_set_style_pad_column(grid, GRID_GAP, 0);
    lv_obj_set_scroll_dir(grid, LV_DIR_VER);
    for (int i = 0; i < n; i++) menu_item(grid, &items[i], item_w);
    return grid;
}

/**
 * @brief The scrolling content column under a frame's title row.
 * v2: the area's size.
 * @version 2
 * @req REQ-UI-001
 * @return The column.
 */
lv_obj_t *settings_column(lv_obj_t *frame)
{
    lv_obj_t *col = lv_obj_create(frame);
    lv_obj_remove_style_all(col);
    lv_obj_set_size(col, settings_content_w(), settings_content_h());
    lv_obj_align(col, LV_ALIGN_TOP_LEFT, 0, TITLE_ROW);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(col, ROW_GAP, 0);
    lv_obj_set_scroll_dir(col, LV_DIR_VER);
    return col;
}

/**
 * @brief A button at the top right of @p parent.
 * v2: narrower when the frame is.
 * @version 2
 * @req REQ-UI-001
 * @return The button.
 */
lv_obj_t *settings_title_button(lv_obj_t *parent, const char *s, lv_event_cb_t cb, void *user)
{
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_set_size(b, settings_narrow() ? TITLE_BTN_NARROW_W : TITLE_BTN_W, 50);
    lv_obj_align(b, LV_ALIGN_TOP_RIGHT, 0, 0);
    if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, user);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, s);
    lv_obj_center(l);
    return b;
}

/**
 * @brief A small rounded chip.
 * @version 1
 * @req REQ-UI-001
 * @return The chip.
 */
lv_obj_t *settings_chip(lv_obj_t *parent, const char *s, ui_role_t fill)
{
    lv_obj_t *c = lv_obj_create(parent);
    lv_obj_remove_style_all(c);
    lv_obj_set_size(c, LV_SIZE_CONTENT, 30);
    lv_obj_set_style_radius(c, 15, 0);
    lv_obj_set_style_pad_hor(c, 12, 0);
    lv_obj_set_style_border_width(c, 1, 0);
    ui_bind_border(c, UI_ROLE_BORDER);
    if (fill != UI_ROLE_COUNT) {
        lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
        ui_bind_fill_role(c, fill);
    }
    lv_obj_center(settings_text(c, s, &lv_font_montserrat_16, UI_ROLE_TEXT));
    return c;
}

/**
 * @brief A field row: the label on the left (narrow: above), the value area returned.
 * v2: narrow frames stack the label over the value, which wraps.
 * @version 2
 * @req REQ-UI-001
 * @return The value area.
 */
lv_obj_t *settings_field(lv_obj_t *col, const char *label)
{
    bool narrow = settings_narrow();
    int32_t w = settings_content_w() - 12;
    lv_obj_t *row = lv_obj_create(col);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, w, narrow ? LV_SIZE_CONTENT : SETTINGS_FIELD_H);
    if (narrow) {
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_row(row, 2, 0);
    }
    lv_obj_t *l = settings_text(row, label, &lv_font_montserrat_20, UI_ROLE_TEXT_MUTED);
    if (!narrow) lv_obj_align(l, LV_ALIGN_LEFT_MID, 0, 0);
    if (narrow && !label[0]) lv_obj_add_flag(l, LV_OBJ_FLAG_HIDDEN);
    lv_obj_t *val = lv_obj_create(row);
    lv_obj_remove_style_all(val);
    lv_obj_set_size(val, narrow ? w : w - LABEL_W, narrow ? LV_SIZE_CONTENT : SETTINGS_FIELD_H);
    if (!narrow) lv_obj_align(val, LV_ALIGN_LEFT_MID, LABEL_W, 0);
    lv_obj_set_style_min_height(val, SETTINGS_FIELD_H, 0);
    lv_obj_set_flex_flow(val, narrow ? LV_FLEX_FLOW_ROW_WRAP : LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(val, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(val, 8, 0);
    lv_obj_set_style_pad_row(val, 6, 0);
    return val;
}

/**
 * @brief A button in a value area.
 * @version 1
 * @req REQ-UI-001
 * @return The button.
 */
lv_obj_t *settings_button(lv_obj_t *val, const char *s, lv_event_cb_t cb, void *user)
{
    lv_obj_t *b = lv_button_create(val);
    lv_obj_set_height(b, SETTINGS_FIELD_H - 6);
    if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, user);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, s);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_20, 0);
    lv_obj_center(l);
    return b;
}

/**
 * @brief A tappable value with a chevron.
 * @version 1
 * @req REQ-UI-001
 * @return The button.
 */
lv_obj_t *settings_value_button(lv_obj_t *val, const char *s, lv_event_cb_t cb, void *user)
{
    char t[128];
    lv_snprintf(t, sizeof(t), "%s   " LV_SYMBOL_RIGHT, s);
    return settings_button(val, t, cb, user);
}

/**
 * @brief A segmented choice.
 * @version 1
 * @req REQ-UI-001
 */
void settings_segmented(lv_obj_t *val, const char *const *opts, int n, int sel,
                        lv_event_cb_t cb, uintptr_t base)
{
    for (int i = 0; i < n; i++) {
        lv_obj_t *b = lv_button_create(val);
        lv_obj_set_height(b, SETTINGS_FIELD_H - 6);
        if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, (void *)(base + (uintptr_t)i));
        if (i == sel) ui_bind_fill_role(b, UI_ROLE_ACCENT);
        else ui_bind_fill(b, UI_SLOT_CONTROL);
        lv_obj_center(settings_text(b, opts[i], &lv_font_montserrat_20, UI_ROLE_TEXT));
    }
}

/**
 * @brief A full-width banner.
 * v2: the area's width.
 * @version 2
 * @req REQ-UI-001
 * @return The banner.
 */
lv_obj_t *settings_banner(lv_obj_t *col, const char *msg, ui_slot_t slot)
{
    lv_obj_t *b = lv_obj_create(col);
    lv_obj_set_size(b, settings_content_w() - 12, LV_SIZE_CONTENT);
    lv_obj_set_style_radius(b, 10, 0);
    lv_obj_set_style_pad_all(b, 10, 0);
    lv_obj_set_style_border_width(b, 0, 0);
    lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE);
    ui_bind_fill(b, slot);
    lv_obj_t *l = settings_text(b, msg, &lv_font_montserrat_20, UI_ROLE_TEXT);
    lv_obj_set_width(l, settings_content_w() - 40);
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
    return b;
}
