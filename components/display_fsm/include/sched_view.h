/**
 * @file sched_view.h
 * v2: no priority; the list in two sections (Daily, Events), each with its own Add.
 * @version 2
 * @brief Schedule screens (prototype): the profile list (one scrollable column: the Daily
 *        profiles, then the Events; each with its when and look, the one showing now marked)
 *        and one profile's editor (a daily one: its time window and look; an event: repeat,
 *        dates, time window, look; windows from a clock time or the sun; a save refused while
 *        it overlaps another of its kind). They
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
    char note[SCHED_NOTE_LEN];    /**< A side note, e.g. "in 81 days"; "". */
} sched_row_state_t;

/**
 * @brief The profile list in a settings frame on @p parent: @p n profiles with their states,
 *        the daily ones under "Daily" and the events under "Events", each section with an
 *        Add button; Back calling @p back_cb.
 * v2: two sections, no priority.
 * @version 2
 * @req REQ-BG-004
 * @return The frame.
 */
lv_obj_t *sched_list_create(lv_obj_t *parent, const sched_rule_t *rules,
                            const sched_row_state_t *states, int n, lv_event_cb_t back_cb);

/**
 * @brief One profile's editor in a settings frame on @p parent. With @p problem (an overlap
 *        with another profile of its kind) a banner says why and Save is disabled.
 * v2: daily profiles show only their window and look (no repeat, dates or priority).
 * @version 2
 * @req REQ-BG-004
 * @return The frame.
 */
lv_obj_t *sched_edit_create(lv_obj_t *parent, const sched_rule_t *r, const char *problem,
                            lv_event_cb_t back_cb);

#ifdef __cplusplus
}
#endif
