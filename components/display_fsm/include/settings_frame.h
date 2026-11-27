/**
 * @file settings_frame.h
 * v2: settings_text, the themed label every settings screen uses.
 * v3: the settings kit: content column, title button, chip, field row, buttons, segmented
 *     choice, banner; the frame's top is settable (the phone panel centres it).
 * v4: the frame's area is set per display (settings_frame_set_area): any width; narrow
 *     frames (a phone held upright) stack each label over its value, one menu column.
 * @version 4
 * @brief Settings screen layout and widget kit, kept free of the settings behaviour (and its
 *        ESP-IDF dependencies) so the host preview and the phone panel build it too: the
 *        sub-screen frame (a centred dialog panel with a title and Back), the menu grid and the
 *        pieces every settings screen is made of. Caller holds the LVGL lock.
 */
#pragma once

#include "lvgl.h"
#include "ui_theme.h"

#ifdef __cplusplus
extern "C" {
#endif

/** The clock's frame area: centred below its top-bar clock (the default). */
#define SETTINGS_FRAME_Y 64   // under the top-bar clock (4 + 56); the banner is at the bottom
#define SETTINGS_FRAME_W 800
#define SETTINGS_FRAME_H 478
/** Frames narrower than this stack each field's label over its value. */
#define SETTINGS_NARROW_W 640
/** A field row's height (label left, value right). */
#define SETTINGS_FIELD_H 46

/** One menu entry: icon, label, and the click handler with its user data. */
typedef struct {
    const char   *icon;
    const char   *label;
    lv_event_cb_t cb;
    void         *user;
} settings_menu_item_t;

/**
 * @brief The sub-screen frame: a centred dialog panel with @p title and a Back button calling
 *        @p back_cb.
 * @version 1
 * @req REQ-UI-001
 * @return The frame (its content starts below the title row).
 */
lv_obj_t *settings_frame_create(lv_obj_t *parent, const char *title, lv_event_cb_t back_cb);

/**
 * @brief A label on @p parent showing @p s in @p font, its colour bound to text role @p role
 *        (legible over whatever it sits on).
 * @version 1
 * @req REQ-UI-001
 * @return The label.
 */
lv_obj_t *settings_text(lv_obj_t *parent, const char *s, const lv_font_t *font, ui_role_t role);

/**
 * @brief Keep @p label to one line @p w wide, ending in "..." when its text is longer.
 * @version 1
 * @req REQ-UI-001
 */
void settings_one_line(lv_obj_t *label, int32_t w);

/**
 * @brief Where frames go: top edge @p y, size @p w x @p h, centred across (the clock: under its
 *        top bar; the phone panel: the browser's viewport). Frames made later use it.
 * @version 1
 * @req REQ-UI-001
 */
void settings_frame_set_area(int32_t y, int32_t w, int32_t h);

/**
 * @brief The frames' top edge.
 * @version 1
 * @req REQ-UI-001
 * @return It.
 */
int32_t settings_frame_get_y(void);

/**
 * @brief The width of the content area under a frame's title row.
 * @version 1
 * @req REQ-UI-001
 * @return Pixels.
 */
int32_t settings_content_w(void);

/**
 * @brief The height of the content area under a frame's title row.
 * @version 1
 * @req REQ-UI-001
 * @return Pixels.
 */
int32_t settings_content_h(void);

/**
 * @brief Whether frames are narrow (under SETTINGS_NARROW_W): labels over values.
 * @version 1
 * @req REQ-UI-001
 * @return Whether.
 */
bool settings_narrow(void);

/**
 * @brief The scrolling content column under @p frame's title row (flex, top to bottom).
 * @version 1
 * @req REQ-UI-001
 * @return The column.
 */
lv_obj_t *settings_column(lv_obj_t *frame);

/**
 * @brief A button at the top right of @p parent (a frame's title row: Save, Add, Done).
 * @version 1
 * @req REQ-UI-001
 * @return The button.
 */
lv_obj_t *settings_title_button(lv_obj_t *parent, const char *s, lv_event_cb_t cb, void *user);

/**
 * @brief A small rounded chip showing @p s, filled with @p fill (UI_ROLE_COUNT: outline only).
 * @version 1
 * @req REQ-UI-001
 * @return The chip.
 */
lv_obj_t *settings_chip(lv_obj_t *parent, const char *s, ui_role_t fill);

/**
 * @brief A field row in @p col: @p label on the left.
 * @version 1
 * @req REQ-UI-001
 * @return The value area on the right (flex, left to right) for the field's controls.
 */
lv_obj_t *settings_field(lv_obj_t *col, const char *label);

/**
 * @brief A button showing @p s in a value area.
 * @version 1
 * @req REQ-UI-001
 * @return The button.
 */
lv_obj_t *settings_button(lv_obj_t *val, const char *s, lv_event_cb_t cb, void *user);

/**
 * @brief A tappable value: @p s with a chevron (it opens a picker).
 * @version 1
 * @req REQ-UI-001
 * @return The button.
 */
lv_obj_t *settings_value_button(lv_obj_t *val, const char *s, lv_event_cb_t cb, void *user);

/**
 * @brief A segmented choice of @p n options in a value area, @p sel filled with the accent;
 *        option i calls @p cb with user data @p base + i.
 * @version 1
 * @req REQ-UI-001
 */
void settings_segmented(lv_obj_t *val, const char *const *opts, int n, int sel,
                        lv_event_cb_t cb, uintptr_t base);

/**
 * @brief A full-width banner in @p col: @p msg wrapped on the @p slot fill (an alert slot
 *        for a problem).
 * @version 1
 * @req REQ-UI-001
 * @return The banner.
 */
lv_obj_t *settings_banner(lv_obj_t *col, const char *msg, ui_slot_t slot);

/**
 * @brief Lay @p n menu entries out in two columns below the frame's title row, every entry
 *        visible without scrolling.
 * @version 1
 * @req REQ-UI-001
 * @return The grid container.
 */
lv_obj_t *settings_menu_grid(lv_obj_t *frame, const settings_menu_item_t *items, int n);

#ifdef __cplusplus
}
#endif
