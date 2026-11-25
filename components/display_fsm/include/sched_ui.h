/**
 * @file sched_ui.h
 * @version 1
 * @brief The schedule screens: the profile list (Daily, then Events, each with its when and
 *        look; the one showing now marked; events say how far off they are) and one
 *        profile's editor (a daily profile: name, time window, look; an event: name, every
 *        year or once, dates, all day or a window, look; Save refused while it overlaps
 *        another of its kind, with the reason). They draw what they are given and report
 *        every tap to one callback; settings_sched.c holds the working copy. Caller holds
 *        the LVGL lock.
 */
#pragma once

#include "lvgl.h"
#include "sched_rule.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SCHED_NOTE_LEN 72

/** How a profile stands right now (the engine works it out). */
typedef struct {
    bool active;                  /**< Showing now. */
    char note[SCHED_NOTE_LEN];    /**< A side note, e.g. "in 81 days"; "". */
} sched_row_state_t;

/** What was tapped. */
typedef enum {
    SCHED_UI_ROW = 0,       /**< A profile in the list (index: which). */
    SCHED_UI_ADD_DAILY,
    SCHED_UI_ADD_EVENT,
    SCHED_UI_NAME,
    SCHED_UI_YEARLY,
    SCHED_UI_ONCE,
    SCHED_UI_ALL_DAY,
    SCHED_UI_WINDOW,
    SCHED_UI_DATES,
    SCHED_UI_START,
    SCHED_UI_END,
    SCHED_UI_IMAGE,
    SCHED_UI_THEME,
    SCHED_UI_ANIM,
    SCHED_UI_BRIGHTNESS,
    SCHED_UI_SAVE,
    SCHED_UI_DELETE,
} sched_ui_action_t;

/** Receives every tap on the schedule screens. */
typedef void (*sched_ui_cb_t)(sched_ui_action_t action, int index, void *ctx);

/**
 * @brief The profile list in a settings frame on @p parent: @p n profiles with their states
 *        (daily under "Daily", events under "Events", each section with an Add), Back
 *        calling @p back_cb, every other tap @p cb.
 * @version 1
 * @req REQ-BG-004
 * @return The frame.
 */
lv_obj_t *sched_ui_list(lv_obj_t *parent, const sched_rule_t *rules, const sched_row_state_t *st,
                        int n, lv_event_cb_t back_cb, sched_ui_cb_t cb, void *ctx);

/**
 * @brief One profile's editor in a settings frame on @p parent. @p is_new: titled "New
 *        profile"/"New event" and without Delete. With @p problem (an overlap) a banner says
 *        why and Save is disabled.
 * @version 1
 * @req REQ-BG-004
 * @return The frame.
 */
lv_obj_t *sched_ui_edit(lv_obj_t *parent, const sched_rule_t *r, bool is_new, const char *problem,
                        lv_event_cb_t back_cb, sched_ui_cb_t cb, void *ctx);

#ifdef __cplusplus
}
#endif
