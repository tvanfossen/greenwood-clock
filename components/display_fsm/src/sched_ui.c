/**
 * @file sched_ui.c
 * @version 1
 * @brief The schedule screens (see sched_ui.h), built from the settings kit. Every colour is
 *        a theme role or slot.
 */
#include "sched_ui.h"
#include "settings_frame.h"
#include "ui_theme_bind.h"

#include <stdio.h>

#define ROW_H    98
#define HEADER_H 50

static sched_ui_cb_t s_cb;    // the one schedule screen showing reports here
static void         *s_ctx;

/**
 * @brief Any tap: decode the action and index from the user data and report them.
 * @version 1
 * @callback
 */
static void tap_cb(lv_event_t *e)
{
    uintptr_t u = (uintptr_t)lv_event_get_user_data(e);
    if (s_cb) s_cb((sched_ui_action_t)(u & 0xff), (int)(u >> 8), s_ctx);
}

/**
 * @brief The user data that tap_cb decodes.
 * @version 1
 * @utility
 * @return It.
 */
static void *tap(sched_ui_action_t a, int index)
{
    return (void *)(((uintptr_t)index << 8) | (uintptr_t)a);
}

// ---------------------------------------------------------------------------
// The list
// ---------------------------------------------------------------------------

/**
 * @brief One profile row: name, when, look; a "Now" chip; a side note. Tapping opens it.
 * v2: every line one line, dotted to the row's width (narrow screens).
 * @version 2
 * @utility
 */
static void list_row(lv_obj_t *col, const sched_rule_t *r, const sched_row_state_t *st, int i)
{
    char when[96], look[128];
    sched_fmt_when(r, when, sizeof(when));
    sched_fmt_look(r, look, sizeof(look));

    lv_obj_t *row = lv_obj_create(col);
    lv_obj_set_size(row, settings_content_w() - 12, ROW_H);
    lv_obj_set_style_radius(row, 12, 0);
    lv_obj_set_style_pad_all(row, 10, 0);
    lv_obj_set_style_border_width(row, st->active ? 2 : 0, 0);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(row, tap_cb, LV_EVENT_CLICKED, tap(SCHED_UI_ROW, i));
    ui_bind_fill(row, UI_SLOT_CONTROL);
    ui_bind_border(row, UI_ROLE_ACCENT);

    int32_t inner = settings_content_w() - 12 - 20;   // the row less its padding
    int32_t side = st->note[0] ? 110 : 0;               // the note's room, bottom right
    settings_one_line(settings_text(row, r->name, &lv_font_montserrat_24, UI_ROLE_TEXT),
                      inner - (st->active ? 70 : 0));
    lv_obj_t *w = settings_text(row, when, &lv_font_montserrat_20, UI_ROLE_TEXT_MUTED);
    settings_one_line(w, inner);
    lv_obj_set_pos(w, 0, 30);
    lv_obj_t *lk = settings_text(row, look, &lv_font_montserrat_16, UI_ROLE_TEXT_FAINT);
    settings_one_line(lk, inner - (settings_narrow() ? side : LV_MAX(side, 160)));
    lv_obj_set_pos(lk, 0, 56);
    if (st->active) lv_obj_align(settings_chip(row, "Now", UI_ROLE_ACCENT), LV_ALIGN_TOP_RIGHT, 0, 0);
    if (st->note[0]) {
        lv_obj_t *n = settings_text(row, st->note, &lv_font_montserrat_16, UI_ROLE_TEXT_MUTED);
        lv_obj_align(n, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    }
}

/**
 * @brief A section header: @p title and an Add button reporting @p add.
 * v2: the frame area's width.
 * @version 2
 * @utility
 */
static void section_header(lv_obj_t *col, const char *title, sched_ui_action_t add)
{
    lv_obj_t *h = lv_obj_create(col);
    lv_obj_remove_style_all(h);
    lv_obj_set_size(h, settings_content_w() - 12, HEADER_H);
    lv_obj_align(settings_text(h, title, &lv_font_montserrat_24, UI_ROLE_ACCENT), LV_ALIGN_LEFT_MID,
                 4, 0);
    settings_title_button(h, LV_SYMBOL_PLUS " Add", tap_cb, tap(add, 0));
}

/**
 * @brief The profile list.
 * @version 1
 * @req REQ-BG-004
 * @return The frame.
 */
lv_obj_t *sched_ui_list(lv_obj_t *parent, const sched_rule_t *rules, const sched_row_state_t *st,
                        int n, lv_event_cb_t back_cb, sched_ui_cb_t cb, void *ctx)
{
    s_cb = cb;
    s_ctx = ctx;
    lv_obj_t *frame = settings_frame_create(parent, "Schedule", back_cb);
    lv_obj_t *col = settings_column(frame);
    section_header(col, "Daily", SCHED_UI_ADD_DAILY);
    for (int i = 0; i < n; i++) {
        if (rules[i].repeat == SCHED_EVERY_DAY) list_row(col, &rules[i], &st[i], i);
    }
    section_header(col, "Events", SCHED_UI_ADD_EVENT);
    for (int i = 0; i < n; i++) {
        if (rules[i].repeat != SCHED_EVERY_DAY) list_row(col, &rules[i], &st[i], i);
    }
    return frame;
}

// ---------------------------------------------------------------------------
// The editor
// ---------------------------------------------------------------------------

/**
 * @brief The time window row: from and to (a clock time or the sun), each a picker.
 * @version 1
 * @utility
 */
static void window_field(lv_obj_t *col, const char *label, const sched_rule_t *r)
{
    char a[24], b[24];
    lv_obj_t *v = settings_field(col, label);
    sched_fmt_time(&r->start, a, sizeof(a));
    sched_fmt_time(&r->end, b, sizeof(b));
    settings_value_button(v, a, tap_cb, tap(SCHED_UI_START, 0));
    settings_text(v, "to", &lv_font_montserrat_20, UI_ROLE_TEXT_MUTED);
    settings_value_button(v, b, tap_cb, tap(SCHED_UI_END, 0));
}

/**
 * @brief The WHEN fields. Daily: the window. Event: every year or once, dates, all day or a
 *        window.
 * @version 1
 * @utility
 */
static void when_fields(lv_obj_t *col, const sched_rule_t *r)
{
    static const char *const REPEAT[] = { "Every year", "Once" };
    static const char *const SPAN[] = { "All day", "Time window" };
    char s[48];
    if (r->repeat == SCHED_EVERY_DAY) {
        window_field(col, "Time", r);
    } else {
        settings_segmented(settings_field(col, "Repeats"), REPEAT, 2,
                           r->repeat == SCHED_ONCE ? 1 : 0, tap_cb, (uintptr_t)SCHED_UI_YEARLY);
        sched_fmt_dates(r, s, sizeof(s));
        settings_value_button(settings_field(col, "Dates"), s, tap_cb, tap(SCHED_UI_DATES, 0));
        settings_segmented(settings_field(col, "Time"), SPAN, 2, r->all_day ? 0 : 1, tap_cb,
                           (uintptr_t)SCHED_UI_ALL_DAY);
        if (!r->all_day) window_field(col, "", r);
    }
}

/**
 * @brief The LOOK fields: background, theme, clock animation, brightness ("Global" unset).
 * @version 1
 * @utility
 */
static void look_fields(lv_obj_t *col, const sched_rule_t *r)
{
    char s[SCHED_IMAGE_LEN];
    sched_image_label(r->image, s, sizeof(s));
    settings_value_button(settings_field(col, "Background"), r->image[0] ? s : "Global", tap_cb,
                          tap(SCHED_UI_IMAGE, 0));
    sched_theme_label(r->theme, s, sizeof(s));
    settings_value_button(settings_field(col, "Theme"), r->theme[0] ? s : "Global", tap_cb,
                          tap(SCHED_UI_THEME, 0));
    settings_value_button(settings_field(col, "Clock animation"), r->anim[0] ? r->anim : "Global",
                          tap_cb, tap(SCHED_UI_ANIM, 0));
    snprintf(s, sizeof(s), "%d%%", r->brightness);
    settings_value_button(settings_field(col, "Brightness"),
                          r->brightness != SCHED_INHERIT ? s : "Global", tap_cb,
                          tap(SCHED_UI_BRIGHTNESS, 0));
}

/**
 * @brief The Delete button at the end of the editor.
 * @version 1
 * @utility
 */
static void delete_button(lv_obj_t *col)
{
    lv_obj_t *v = settings_field(col, "");
    lv_obj_t *b = settings_button(v, LV_SYMBOL_TRASH "  Delete", tap_cb, tap(SCHED_UI_DELETE, 0));
    ui_bind_fill(b, UI_SLOT_ALERT_SEVERE);
}

/**
 * @brief One profile's editor.
 * @version 1
 * @req REQ-BG-004
 * @return The frame.
 */
lv_obj_t *sched_ui_edit(lv_obj_t *parent, const sched_rule_t *r, bool is_new, const char *problem,
                        lv_event_cb_t back_cb, sched_ui_cb_t cb, void *ctx)
{
    s_cb = cb;
    s_ctx = ctx;
    bool daily = r->repeat == SCHED_EVERY_DAY;
    const char *title = !is_new ? r->name : (daily ? "New profile" : "New event");
    lv_obj_t *frame = settings_frame_create(parent, title, back_cb);
    lv_obj_t *save = settings_title_button(frame, LV_SYMBOL_OK " Save", tap_cb, tap(SCHED_UI_SAVE, 0));
    lv_obj_t *col = settings_column(frame);
    if (problem && problem[0]) {
        settings_banner(col, problem, UI_SLOT_ALERT_SEVERE);
        lv_obj_add_state(save, LV_STATE_DISABLED);
    }
    settings_value_button(settings_field(col, "Name"), r->name, tap_cb, tap(SCHED_UI_NAME, 0));
    when_fields(col, r);
    look_fields(col, r);
    if (!is_new) delete_button(col);
    return frame;
}
