/**
 * @file settings_frame.h
 * @version 1
 * @brief Settings screen layout, kept free of the settings behaviour (and its ESP-IDF
 *        dependencies) so the host preview renders it too: the sub-screen frame (a centred
 *        dialog panel with a title and Back) and the menu grid. Caller holds the LVGL lock.
 */
#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Frame geometry: centred below the top-bar clock. */
#define SETTINGS_FRAME_Y 114
#define SETTINGS_FRAME_W 800
#define SETTINGS_FRAME_H 478

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
