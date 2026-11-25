/**
 * @file settings_sched.h
 * v2: the showing profile from the menu (settings_sched_now_label / _edit_now).
 * @version 2
 * @brief The schedule in the settings: the list (which profile shows now, how far off each
 *        event is), and the editor's working copy, checked against the others for overlaps
 *        on every change and saved through the backend. Pure LVGL over settings_backend_t.
 */
#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief The schedule list screen (a settings_screen_fn).
 * @version 1
 * @req REQ-BG-004
 * @return The frame.
 */
lv_obj_t *settings_sched_screen(lv_obj_t *parent, void *ctx);

/**
 * @brief The menu entry for the showing profile: "Now: Winter afternoon", or "Now: no profile"
 *        in a gap.
 * @version 1
 * @req REQ-BG-004
 */
void settings_sched_now_label(char *out, size_t n);

/**
 * @brief Open the editor on the showing profile (saving changes it every time it runs), or in
 *        a gap on a new daily profile for the current hour.
 * @version 1
 * @req REQ-BG-004
 */
void settings_sched_edit_now(void);

#ifdef __cplusplus
}
#endif
