/**
 * @file settings_sched.h
 * @version 1
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

#ifdef __cplusplus
}
#endif
