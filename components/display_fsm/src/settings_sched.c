/**
 * @file settings_sched.c
 * @version 1
 * @brief The schedule in the settings (see settings_sched.h): list states from the engine,
 *        the editor's working copy, pickers, overlap check, save and delete.
 */
#include "settings_sched.h"
#include "settings_app.h"
#include "settings_pickers.h"
#include "sched_ui.h"
#include "sched_json.h"

#include <stdio.h>
#include <string.h>

#define CHECK_DAYS 366

static sched_rule_t      s_rules[SCHED_MAX_PROFILES];
static sched_row_state_t s_states[SCHED_MAX_PROFILES];
static int               s_n;
static sched_rule_t      s_edit;          // the editor's working copy
static int               s_edit_idx;      // its index in s_rules (-1: new)
static sched_rule_t      s_work[SCHED_MAX_PROFILES];   // the schedule as it would be saved

static void on_action(sched_ui_action_t a, int index, void *ctx);

// ---------------------------------------------------------------------------
// List
// ---------------------------------------------------------------------------

/**
 * @brief "today", "tomorrow" or "in N days" for an event @p days away.
 * @version 1
 * @utility
 */
static void fmt_days(int days, char *out, size_t n)
{
    if (days < 0) snprintf(out, n, "past");
    else if (days == 0) snprintf(out, n, "today");
    else if (days == 1) snprintf(out, n, "tomorrow");
    else snprintf(out, n, "in %d days", days);
}

/**
 * @brief Load the schedule and work out each profile's state now.
 * @version 1
 * @utility
 */
static void load(void)
{
    const settings_backend_t *be = settings_app_backend();
    sched_date_t day;
    int min;
    be->now(&day, &min);
    s_n = be->schedule_get(s_rules, SCHED_MAX_PROFILES);
    int active = sched_active(s_rules, s_n, day, min, be->sun());
    for (int i = 0; i < s_n; i++) {
        s_states[i].active = i == active;
        s_states[i].note[0] = '\0';
        if (s_rules[i].repeat != SCHED_EVERY_DAY) {
            fmt_days(sched_days_until(&s_rules[i], day), s_states[i].note, SCHED_NOTE_LEN);
        }
    }
}

/**
 * @brief The schedule list screen.
 * @version 1
 * @req REQ-BG-004
 * @return The frame.
 */
lv_obj_t *settings_sched_screen(lv_obj_t *parent, void *ctx)
{
    LV_UNUSED(ctx);
    load();
    return sched_ui_list(parent, s_rules, s_states, s_n, settings_app_back_cb, on_action, NULL);
}

// ---------------------------------------------------------------------------
// Editor
// ---------------------------------------------------------------------------

/**
 * @brief The schedule as it would be saved with the working copy (deleted when @p drop).
 * @version 1
 * @utility
 * @return Its size; the working copy's index is written to @p at (-1 when dropped).
 */
static int working_schedule(bool drop, int *at)
{
    int n = 0;
    *at = -1;
    for (int i = 0; i < s_n; i++) {
        if (i != s_edit_idx) s_work[n++] = s_rules[i];
        else if (!drop) s_work[*at = n++] = s_edit;
    }
    if (s_edit_idx < 0 && !drop && n < SCHED_MAX_PROFILES) s_work[*at = n++] = s_edit;
    return n;
}

/**
 * @brief Why the working copy cannot be saved (it overlaps another of its kind), or "".
 * @version 1
 * @utility
 */
static void problem(char *out, size_t len)
{
    const settings_backend_t *be = settings_app_backend();
    sched_date_t day;
    int min, at;
    be->now(&day, &min);
    int n = working_schedule(false, &at);
    sched_overlap_t o;
    out[0] = '\0';
    if (at >= 0 && sched_find_overlap(s_work, n, at, day, CHECK_DAYS, be->sun(), &o)) {
        sched_fmt_overlap(s_work, &o, out, len);
    }
}

/**
 * @brief The editor screen.
 * @version 1
 * @utility
 * @return The frame.
 */
static lv_obj_t *edit_screen(lv_obj_t *parent, void *ctx)
{
    LV_UNUSED(ctx);
    char why[128];
    problem(why, sizeof(why));
    return sched_ui_edit(parent, &s_edit, s_edit_idx < 0, why, settings_app_back_cb, on_action,
                         NULL);
}

/**
 * @brief Open the editor on a copy of @p r (@p idx -1: a new profile).
 * @version 1
 * @utility
 */
static void open_editor(const sched_rule_t *r, int idx)
{
    s_edit = *r;
    s_edit_idx = idx;
    settings_app_push(edit_screen, NULL);
}

/**
 * @brief A new daily profile or event to start the editor with.
 * @version 1
 * @utility
 * @return The profile.
 */
static sched_rule_t new_profile(bool event)
{
    sched_rule_t r;
    sched_date_t day;
    int min;
    settings_app_backend()->now(&day, &min);
    memset(&r, 0, sizeof(r));
    r.brightness = SCHED_INHERIT;
    if (event) {
        snprintf(r.name, sizeof(r.name), "New event");
        r.repeat = SCHED_YEARLY;
        r.all_day = true;
        r.from = r.to = (sched_date_t){ 0, day.month, day.day };
    } else {
        snprintf(r.name, sizeof(r.name), "New profile");
        r.start = (sched_time_t){ SCHED_AT_CLOCK, 12 * 60 };
        r.end = (sched_time_t){ SCHED_AT_CLOCK, 13 * 60 };
    }
    return r;
}

/**
 * @brief The save or delete finished: back to the list (rebuilt), or say why not.
 * @version 1
 * @callback
 */
static void saved_cb(bool ok, const char *why, void *ctx)
{
    LV_UNUSED(ctx);
    if (!ok) {
        settings_app_toast(why && why[0] ? why : "Could not save", false);
        return;
    }
    settings_app_pop();       // the editor
    settings_app_refresh();   // the list, with the change
    settings_app_toast("Saved", false);
}

/**
 * @brief Save the working copy, or delete the profile (@p drop).
 * @version 1
 * @utility
 */
static void commit(bool drop)
{
    int at;
    int n = working_schedule(drop, &at);
    settings_app_backend()->schedule_set(s_work, n, saved_cb, NULL);
}

// ---------------------------------------------------------------------------
// Pickers' answers: change the working copy, rebuild the editor
// ---------------------------------------------------------------------------

/**
 * @brief A name typed.
 * @version 1
 * @callback
 */
static void name_cb(const char *value, void *ctx)
{
    LV_UNUSED(ctx);
    snprintf(s_edit.name, sizeof(s_edit.name), "%s", value);
    settings_app_refresh();
}

/**
 * @brief A window edge picked (@p ctx non-NULL: the end).
 * @version 1
 * @callback
 */
static void time_cb(sched_time_t t, void *ctx)
{
    if (ctx) s_edit.end = t;
    else s_edit.start = t;
    settings_app_refresh();
}

/**
 * @brief Dates picked.
 * @version 1
 * @callback
 */
static void dates_cb(sched_date_t from, sched_date_t to, void *ctx)
{
    LV_UNUSED(ctx);
    s_edit.from = from;
    s_edit.to = to;
    settings_app_refresh();
}

/**
 * @brief A background, theme or animation picked (@p value NULL: global).
 * @version 1
 * @callback
 */
static void look_cb(const char *value, void *ctx)
{
    settings_list_t which = (settings_list_t)(intptr_t)ctx;
    char *field = which == SETTINGS_LIST_BACKGROUNDS ? s_edit.image
                  : which == SETTINGS_LIST_THEMES    ? s_edit.theme
                                                     : s_edit.anim;
    size_t len = which == SETTINGS_LIST_BACKGROUNDS ? sizeof(s_edit.image)
                 : which == SETTINGS_LIST_THEMES    ? sizeof(s_edit.theme)
                                                    : sizeof(s_edit.anim);
    snprintf(field, len, "%s", value ? value : "");
    settings_app_refresh();
}

/**
 * @brief A brightness picked.
 * @version 1
 * @callback
 */
static void pct_cb(int pct, void *ctx)
{
    LV_UNUSED(ctx);
    s_edit.brightness = (int8_t)pct;
    settings_app_refresh();
}

/**
 * @brief Every year or once: a one-off takes this year.
 * @version 1
 * @utility
 */
static void set_repeat(sched_repeat_t rep)
{
    sched_date_t day;
    int min;
    settings_app_backend()->now(&day, &min);
    s_edit.repeat = rep;
    s_edit.from.year = s_edit.to.year = rep == SCHED_ONCE ? day.year : 0;
    settings_app_refresh();
}

/**
 * @brief All day or a window (a window starts as 6 PM - 11 PM).
 * @version 1
 * @utility
 */
static void set_all_day(bool all_day)
{
    s_edit.all_day = all_day;
    if (!all_day && memcmp(&s_edit.start, &s_edit.end, sizeof(s_edit.start)) == 0) {
        s_edit.start = (sched_time_t){ SCHED_AT_CLOCK, 18 * 60 };
        s_edit.end = (sched_time_t){ SCHED_AT_CLOCK, 23 * 60 };
    }
    settings_app_refresh();
}

/**
 * @brief Open the picker for a look field.
 * @version 1
 * @utility
 */
static void pick_look(settings_list_t which)
{
    static const char *const TITLES[] = { "Background", "Theme", "Clock Animation" };
    const char *cur = which == SETTINGS_LIST_BACKGROUNDS ? s_edit.image
                      : which == SETTINGS_LIST_THEMES    ? s_edit.theme
                                                         : s_edit.anim;
    settings_app_choose(TITLES[which], which, cur, true, look_cb, (void *)(intptr_t)which);
}

/**
 * @brief The editor's fields that open a picker.
 * @version 1
 * @utility
 */
static void open_picker(sched_ui_action_t a)
{
    if (a == SCHED_UI_NAME) settings_app_enter_text("Name", s_edit.name, name_cb, NULL);
    else if (a == SCHED_UI_START) settings_pick_time("Starts", s_edit.start, time_cb, NULL);
    else if (a == SCHED_UI_END) settings_pick_time("Ends", s_edit.end, time_cb, (void *)1);
    else if (a == SCHED_UI_DATES)
        settings_pick_dates(s_edit.from, s_edit.to, s_edit.repeat == SCHED_ONCE, dates_cb, NULL);
    else if (a == SCHED_UI_BRIGHTNESS) settings_pick_percent(s_edit.brightness, pct_cb, NULL);
    else pick_look(a == SCHED_UI_IMAGE ? SETTINGS_LIST_BACKGROUNDS
                   : a == SCHED_UI_THEME ? SETTINGS_LIST_THEMES
                                         : SETTINGS_LIST_ANIMS);
}

/**
 * @brief Every tap on the schedule screens.
 * @version 1
 * @callback
 */
static void on_action(sched_ui_action_t a, int index, void *ctx)
{
    LV_UNUSED(ctx);
    sched_rule_t fresh;
    switch (a) {
    case SCHED_UI_ROW: open_editor(&s_rules[index], index); break;
    case SCHED_UI_ADD_DAILY:
    case SCHED_UI_ADD_EVENT:
        fresh = new_profile(a == SCHED_UI_ADD_EVENT);
        open_editor(&fresh, -1);
        break;
    case SCHED_UI_YEARLY: set_repeat(SCHED_YEARLY); break;
    case SCHED_UI_ONCE: set_repeat(SCHED_ONCE); break;
    case SCHED_UI_ALL_DAY: set_all_day(true); break;
    case SCHED_UI_WINDOW: set_all_day(false); break;
    case SCHED_UI_SAVE: commit(false); break;
    case SCHED_UI_DELETE: commit(true); break;
    default: open_picker(a); break;
    }
}
