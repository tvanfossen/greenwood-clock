/**
 * @file sched_view.h
 * @version 1
 * @brief Schedule screens (prototype): the profile list (one scrollable column, every
 *        profile's when, look and priority, the one showing now marked, a profile hidden by a
 *        higher one warned) and one profile's editor (repeat, dates, time window from a clock
 *        time or the sun, priority, look; a save refused while the profile conflicts). They
 *        only draw what they are given, so the host preview renders them with sample data.
 *        Caller holds the LVGL lock.
 */
#pragma once

#include "lvgl.h"
#include "sched_rule.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SCHED_NOTE_LEN 72

/** How a profile stands right now (the schedule engine fills it). */
typedef struct {
    bool active;                  /**< Showing now. */
    char note[SCHED_NOTE_LEN];    /**< A warning, e.g. "Hidden on Dec 25 by Christmas (2)"; "". */
} sched_row_state_t;

/**
 * @brief The profile list in a settings frame on @p parent: @p n profiles with their states,
 *        an Add button, Back calling @p back_cb.
 * @version 1
 * @req REQ-BG-004
 * @return The frame.
 */
lv_obj_t *sched_list_create(lv_obj_t *parent, const sched_rule_t *rules,
                            const sched_row_state_t *states, int n, lv_event_cb_t back_cb);

/**
 * @brief One profile's editor in a settings frame on @p parent. With @p problem (a conflict,
 *        e.g. the same priority as an overlapping profile) a banner says why and Save is
 *        disabled.
 * @version 1
 * @req REQ-BG-004
 * @return The frame.
 */
lv_obj_t *sched_edit_create(lv_obj_t *parent, const sched_rule_t *r, const char *problem,
                            lv_event_cb_t back_cb);

#ifdef __cplusplus
}
#endif
