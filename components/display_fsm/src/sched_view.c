/**
 * @file sched_view.c
 * @version 1
 * @brief Schedule screens (prototype): the profile list and one profile's editor (see
 *        sched_view.h). Every colour is a theme role or slot.
 */
#include "sched_view.h"
#include "settings_frame.h"
#include "ui_theme_bind.h"

#include <stdio.h>

#define FRAME_PAD   20
#define TITLE_ROW   64
#define CONTENT_W   (SETTINGS_FRAME_W - 2 * FRAME_PAD - 2)
#define CONTENT_H   (SETTINGS_FRAME_H - 2 * FRAME_PAD - 2 - TITLE_ROW)
#define ROW_H       98
#define ROW_GAP     8
#define FIELD_H     46
#define LABEL_W     190
#define CHIP_H      30

// ---------------------------------------------------------------------------
// Shared pieces
// ---------------------------------------------------------------------------

/**
 * @brief A label on @p parent with @p font and text role @p role.
 * @version 1
 * @utility
 * @return The label.
 */
static lv_obj_t *text(lv_obj_t *parent, const char *s, const lv_font_t *font, ui_role_t role)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, s);
    lv_obj_set_style_text_font(l, font, 0);
    ui_bind_text(l, role);
    return l;
}

/**
 * @brief The scrolling column under the frame's title row.
 * @version 1
 * @utility
 * @return The column.
 */
static lv_obj_t *content_column(lv_obj_t *frame)
{
    lv_obj_t *col = lv_obj_create(frame);
    lv_obj_remove_style_all(col);
    lv_obj_set_size(col, CONTENT_W, CONTENT_H);
    lv_obj_align(col, LV_ALIGN_TOP_LEFT, 0, TITLE_ROW);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(col, ROW_GAP, 0);
    lv_obj_set_scroll_dir(col, LV_DIR_VER);
    return col;
}

/**
 * @brief A button at the top right of the frame's title row.
 * @version 1
 * @utility
 * @return The button.
 */
static lv_obj_t *title_button(lv_obj_t *frame, const char *s)
{
    lv_obj_t *b = lv_button_create(frame);
    lv_obj_set_size(b, 120, 50);
    lv_obj_align(b, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, s);
    lv_obj_center(l);
    return b;
}

/**
 * @brief A small rounded chip: @p role fill (or none for UI_ROLE_COUNT), @p s in it.
 * @version 1
 * @utility
 * @return The chip.
 */
static lv_obj_t *chip(lv_obj_t *parent, const char *s, ui_role_t role)
{
    lv_obj_t *c = lv_obj_create(parent);
    lv_obj_remove_style_all(c);
    lv_obj_set_size(c, LV_SIZE_CONTENT, CHIP_H);
    lv_obj_set_style_radius(c, CHIP_H / 2, 0);
    lv_obj_set_style_pad_hor(c, 12, 0);
    lv_obj_set_style_border_width(c, 1, 0);
    ui_bind_border(c, UI_ROLE_BORDER);
    if (role != UI_ROLE_COUNT) {
        lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
        ui_bind_fill_role(c, role);
    }
    lv_obj_t *l = text(c, s, &lv_font_montserrat_16, UI_ROLE_TEXT);
    lv_obj_center(l);
    return c;
}

// ---------------------------------------------------------------------------
// The list
// ---------------------------------------------------------------------------

/**
 * @brief One profile row: name, when, look; priority and "Now" chips; a warning note.
 * @version 1
 * @utility
 */
static void list_row(lv_obj_t *col, const sched_rule_t *r, const sched_row_state_t *st)
{
    char when[96], look[128], prio[20];
    sched_fmt_when(r, when, sizeof(when));
    sched_fmt_look(r, look, sizeof(look));
    snprintf(prio, sizeof(prio), "Priority %u", r->priority);

    lv_obj_t *row = lv_obj_create(col);
    lv_obj_set_size(row, CONTENT_W - 12, ROW_H);
    lv_obj_set_style_radius(row, 12, 0);
    lv_obj_set_style_pad_all(row, 10, 0);
    lv_obj_set_style_border_width(row, st->active ? 2 : 0, 0);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    ui_bind_fill(row, UI_SLOT_CONTROL);
    ui_bind_border(row, UI_ROLE_ACCENT);

    text(row, r->name, &lv_font_montserrat_24, UI_ROLE_TEXT);
    lv_obj_t *w = text(row, when, &lv_font_montserrat_20, UI_ROLE_TEXT_MUTED);
    lv_obj_set_pos(w, 0, 30);
    lv_obj_t *lk = text(row, look, &lv_font_montserrat_16, UI_ROLE_TEXT_FAINT);
    lv_obj_set_pos(lk, 0, 56);
    lv_obj_set_width(lk, st->note[0] ? 340 : CONTENT_W - 60);
    lv_label_set_long_mode(lk, LV_LABEL_LONG_MODE_DOTS);

    lv_obj_t *p = chip(row, prio, UI_ROLE_COUNT);
    lv_obj_align(p, LV_ALIGN_TOP_RIGHT, 0, 0);
    if (st->active) lv_obj_align(chip(row, "Now", UI_ROLE_ACCENT), LV_ALIGN_TOP_RIGHT, -130, 0);
    if (st->note[0]) {
        lv_obj_t *n = text(row, st->note, &lv_font_montserrat_16, UI_ROLE_WARN);
        lv_obj_align(n, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    }
}

/**
 * @brief The profile list.
 * @version 1
 * @req REQ-BG-004
 * @return The frame.
 */
lv_obj_t *sched_list_create(lv_obj_t *parent, const sched_rule_t *rules,
                            const sched_row_state_t *states, int n, lv_event_cb_t back_cb)
{
    lv_obj_t *frame = settings_frame_create(parent, "Schedule", back_cb);
    title_button(frame, LV_SYMBOL_PLUS " Add");
    lv_obj_t *col = content_column(frame);
    for (int i = 0; i < n; i++) list_row(col, &rules[i], &states[i]);
    return frame;
}

// ---------------------------------------------------------------------------
// The editor
// ---------------------------------------------------------------------------

/**
 * @brief A field row: the label on the left; returns the area for the value on the right.
 * @version 1
 * @utility
 * @return The value area (a transparent row, flex left to right).
 */
static lv_obj_t *field(lv_obj_t *col, const char *label)
{
    lv_obj_t *row = lv_obj_create(col);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, CONTENT_W - 12, FIELD_H);
    lv_obj_t *l = text(row, label, &lv_font_montserrat_20, UI_ROLE_TEXT_MUTED);
    lv_obj_align(l, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_t *val = lv_obj_create(row);
    lv_obj_remove_style_all(val);
    lv_obj_set_size(val, CONTENT_W - 12 - LABEL_W, FIELD_H);
    lv_obj_align(val, LV_ALIGN_LEFT_MID, LABEL_W, 0);
    lv_obj_set_flex_flow(val, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(val, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(val, 8, 0);
    return val;
}

/**
 * @brief A button in a value area showing @p s.
 * @version 1
 * @utility
 */
static void button(lv_obj_t *val, const char *s)
{
    lv_obj_t *b = lv_button_create(val);
    lv_obj_set_height(b, FIELD_H - 6);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, s);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_20, 0);
    lv_obj_center(l);
}

/**
 * @brief A tappable value: @p s and a chevron (opens a picker on the device).
 * @version 1
 * @utility
 */
static void value_button(lv_obj_t *val, const char *s)
{
    char t[SCHED_IMAGE_LEN + 8];
    snprintf(t, sizeof(t), "%s   " LV_SYMBOL_RIGHT, s);
    button(val, t);
}

/**
 * @brief A segmented choice: @p n options, @p sel filled with the accent.
 * @version 1
 * @utility
 */
static void segmented(lv_obj_t *val, const char *const *opts, int n, int sel)
{
    for (int i = 0; i < n; i++) {
        lv_obj_t *b = lv_button_create(val);
        lv_obj_set_height(b, FIELD_H - 6);
        if (i == sel) ui_bind_fill_role(b, UI_ROLE_ACCENT);
        else ui_bind_fill(b, UI_SLOT_CONTROL);
        lv_obj_t *l = text(b, opts[i], &lv_font_montserrat_20, UI_ROLE_TEXT);
        lv_obj_center(l);
    }
}

/**
 * @brief The conflict banner: the alert fill, the reason wrapped in it.
 * @version 1
 * @utility
 */
static void problem_banner(lv_obj_t *col, const char *problem)
{
    lv_obj_t *b = lv_obj_create(col);
    lv_obj_set_size(b, CONTENT_W - 12, LV_SIZE_CONTENT);
    lv_obj_set_style_radius(b, 10, 0);
    lv_obj_set_style_pad_all(b, 10, 0);
    lv_obj_set_style_border_width(b, 0, 0);
    ui_bind_fill(b, UI_SLOT_ALERT_SEVERE);
    lv_obj_t *l = text(b, problem, &lv_font_montserrat_20, UI_ROLE_TEXT);
    lv_obj_set_width(l, CONTENT_W - 40);
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
}

/**
 * @brief The WHEN fields: repeat, dates, time window, priority.
 * @version 1
 * @utility
 */
static void when_fields(lv_obj_t *col, const sched_rule_t *r)
{
    static const char *const REPEAT[] = { "Every day", "Every year", "Once" };
    static const char *const SPAN[] = { "All day", "Time window" };
    char s[48], a[24], b[24];
    segmented(field(col, "Repeats"), REPEAT, 3, (int)r->repeat);
    if (r->repeat != SCHED_EVERY_DAY) {
        sched_fmt_dates(r, s, sizeof(s));
        value_button(field(col, "Dates"), s);
    }
    segmented(field(col, "Time"), SPAN, 2, r->all_day ? 0 : 1);
    if (!r->all_day) {
        lv_obj_t *v = field(col, "");
        sched_fmt_time(&r->start, a, sizeof(a));
        sched_fmt_time(&r->end, b, sizeof(b));
        value_button(v, a);
        text(v, "to", &lv_font_montserrat_20, UI_ROLE_TEXT_MUTED);
        value_button(v, b);
    }
    lv_obj_t *p = field(col, "Priority");
    button(p, LV_SYMBOL_MINUS);
    snprintf(s, sizeof(s), "%u", r->priority);
    text(p, s, &lv_font_montserrat_24, UI_ROLE_TEXT);
    button(p, LV_SYMBOL_PLUS);
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
    value_button(field(col, "Background"), r->image[0] ? s : "Global");
    sched_theme_label(r->theme, s, sizeof(s));
    value_button(field(col, "Theme"), r->theme[0] ? s : "Global");
    value_button(field(col, "Clock animation"), r->anim[0] ? r->anim : "Global");
    snprintf(s, sizeof(s), "%d%%", r->brightness);
    value_button(field(col, "Brightness"), r->brightness != SCHED_INHERIT ? s : "Global");
}

/**
 * @brief One profile's editor.
 * @version 1
 * @req REQ-BG-004
 * @return The frame.
 */
lv_obj_t *sched_edit_create(lv_obj_t *parent, const sched_rule_t *r, const char *problem,
                            lv_event_cb_t back_cb)
{
    lv_obj_t *frame = settings_frame_create(parent, r->name, back_cb);
    lv_obj_t *save = title_button(frame, LV_SYMBOL_OK " Save");
    lv_obj_t *col = content_column(frame);
    if (problem && problem[0]) {
        problem_banner(col, problem);
        lv_obj_add_state(save, LV_STATE_DISABLED);
    }
    when_fields(col, r);
    look_fields(col, r);
    return frame;
}
