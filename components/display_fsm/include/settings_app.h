/**
 * @file settings_app.h
 * @version 1
 * @brief The settings UI, one code base for the clock's touch screen and the phone panel
 *        (WebAssembly): a stack of screens (menu, brightness, background, schedule, theme and
 *        its editor, clock animation, about, and whatever device-only screens the backend
 *        offers) over a settings_backend_t. Pure LVGL. Caller holds the LVGL lock.
 */
#pragma once

#include "lvgl.h"
#include "settings_backend.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Builds one screen on @p parent. */
typedef lv_obj_t *(*settings_screen_fn)(lv_obj_t *parent, void *ctx);

/** A choice made in a picker (NULL value: "Global", the setting left to the global one). */
typedef void (*settings_pick_cb_t)(const char *value, void *ctx);

/**
 * @brief Open the settings on @p root over @p backend, at the menu.
 * @version 1
 * @req REQ-UI-001
 */
void settings_app_open(lv_obj_t *root, const settings_backend_t *backend);

/**
 * @brief Delete every settings screen.
 * @version 1
 * @req REQ-UI-001
 */
void settings_app_close(void);

/**
 * @brief The backend the settings were opened with.
 * @version 1
 * @req REQ-UI-001
 * @return It.
 */
const settings_backend_t *settings_app_backend(void);

/**
 * @brief Show a new screen built by @p fn over the current one.
 * @version 1
 * @req REQ-UI-001
 */
void settings_app_push(settings_screen_fn fn, void *ctx);

/**
 * @brief Close the top screen (at the menu: leave the settings through the backend).
 * @version 1
 * @req REQ-UI-001
 */
void settings_app_pop(void);

/**
 * @brief Rebuild the top screen (after what it shows changed).
 * @version 1
 * @req REQ-UI-001
 */
void settings_app_refresh(void);

/**
 * @brief An LVGL event callback that closes the top screen (every frame's Back).
 * @version 1
 * @callback
 */
void settings_app_back_cb(lv_event_t *e);

/**
 * @brief A short message over the screens that goes away by itself; then pops the top
 *        screen when @p pop.
 * @version 1
 * @req REQ-UI-001
 */
void settings_app_toast(const char *msg, bool pop);

/**
 * @brief Build every open screen again (the display changed size): the same screens, laid out
 *        for the frame area now set (settings_frame_set_area); the top one shows.
 * @version 1
 * @req REQ-UI-001
 */
void settings_app_relayout(void);

/**
 * @brief Push a picker listing @p which with @p current marked; a choice calls @p cb and
 *        closes the picker. Themes list "Customize..." first: a new theme from the editor,
 *        whose stem @p cb gets once it is saved.
 * v2: no "Global" choice (profiles set every field); Customize for themes.
 * @version 2
 * @req REQ-UI-001
 */
void settings_app_choose(const char *title, settings_list_t which, const char *current,
                         settings_pick_cb_t cb, void *ctx);

/**
 * @brief Push a text entry (on-screen keyboard) starting from @p initial; Done calls @p cb
 *        with the text and closes it.
 * @version 1
 * @req REQ-UI-001
 */
void settings_app_enter_text(const char *title, const char *initial, settings_pick_cb_t cb,
                             void *ctx);

#ifdef __cplusplus
}
#endif
