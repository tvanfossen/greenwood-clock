/**
 * @file settings_app.c
 * v2: the Upload screen (when the backend can upload: the phone).
 * v3: no global settings: the menu opens the showing profile ("Now: ..."); brightness,
 *     background, theme and animation are a profile's (settings_sched.c); Customize is in
 *     the theme picker (settings_theme.c).
 * @version 3
 * @brief The settings UI's screen stack, menu and generic screens (see settings_app.h): the
 *        choice list, text entry, toast, about and upload. The schedule and profile screens
 *        are settings_sched.c; theme customizing is settings_theme.c.
 */
#include "settings_app.h"
#include "settings_frame.h"
#include "settings_sched.h"
#include "settings_theme.h"
#include "ui_theme_bind.h"

#include <stdio.h>
#include <string.h>

#define STACK_MAX  8
#define LIST_MAX   32
#define TOAST_MS   1500

typedef struct {
    settings_screen_fn fn;
    void              *ctx;
    lv_obj_t          *obj;
} screen_t;

static lv_obj_t                 *s_root;
static const settings_backend_t *s_be;
static screen_t                  s_stack[STACK_MAX];
static int                       s_top = -1;

// ---------------------------------------------------------------------------
// The stack
// ---------------------------------------------------------------------------

/**
 * @brief The backend.
 * @version 1
 * @req REQ-UI-001
 * @return It.
 */
const settings_backend_t *settings_app_backend(void)
{
    return s_be;
}

/**
 * @brief Show a new screen over the current one.
 * @version 1
 * @req REQ-UI-001
 */
void settings_app_push(settings_screen_fn fn, void *ctx)
{
    if (s_top >= STACK_MAX - 1 || !s_root) return;
    if (s_top >= 0) lv_obj_add_flag(s_stack[s_top].obj, LV_OBJ_FLAG_HIDDEN);
    s_top++;
    s_stack[s_top] = (screen_t){ fn, ctx, NULL };
    s_stack[s_top].obj = fn(s_root, ctx);
}

/**
 * @brief Close the top screen, or leave at the menu.
 * @version 1
 * @req REQ-UI-001
 */
void settings_app_pop(void)
{
    if (s_top <= 0) {
        if (s_be && s_be->leave) s_be->leave();
        return;
    }
    lv_obj_delete(s_stack[s_top].obj);
    s_top--;
    lv_obj_remove_flag(s_stack[s_top].obj, LV_OBJ_FLAG_HIDDEN);
}

/**
 * @brief Rebuild the top screen.
 * @version 1
 * @req REQ-UI-001
 */
void settings_app_refresh(void)
{
    if (s_top < 0) return;
    screen_t *t = &s_stack[s_top];
    lv_obj_delete(t->obj);
    t->obj = t->fn(s_root, t->ctx);
}

/**
 * @brief Back: close the top screen.
 * @version 1
 * @callback
 */
void settings_app_back_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    settings_app_pop();
}

/**
 * @brief The toast ends: remove it, and close the top screen when asked.
 * v2: removes the pill.
 * @version 2
 * @callback
 */
static void toast_done_cb(lv_timer_t *t)
{
    lv_obj_t *pill = (lv_obj_t *)lv_timer_get_user_data(t);
    bool pop = lv_obj_get_user_data(pill) != NULL;
    lv_obj_delete(pill);
    if (pop) settings_app_pop();
}

/**
 * @brief A short message in a pill at the bottom of the screen that goes away by itself; it
 *        does not take taps (the screen stays usable).
 * v2: a pill at the bottom that takes no taps (was a modal message box).
 * @version 2
 * @req REQ-UI-001
 */
void settings_app_toast(const char *msg, bool pop)
{
    lv_obj_t *pill = lv_obj_create(lv_layer_top());
    lv_obj_set_size(pill, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_radius(pill, 24, 0);
    lv_obj_set_style_pad_hor(pill, 24, 0);
    lv_obj_set_style_pad_ver(pill, 12, 0);
    lv_obj_set_style_border_width(pill, 1, 0);
    lv_obj_remove_flag(pill, LV_OBJ_FLAG_CLICKABLE);
    ui_bind_fill(pill, UI_SLOT_DIALOG);
    ui_bind_border(pill, UI_ROLE_ACCENT);
    settings_text(pill, msg, &lv_font_montserrat_20, UI_ROLE_TEXT);
    lv_obj_align(pill, LV_ALIGN_BOTTOM_MID, 0, -24);
    lv_obj_set_user_data(pill, pop ? (void *)1 : NULL);
    lv_timer_t *t = lv_timer_create(toast_done_cb, TOAST_MS, pill);
    lv_timer_set_repeat_count(t, 1);
}

// ---------------------------------------------------------------------------
// Choice list
// ---------------------------------------------------------------------------

typedef struct {
    const char         *title;
    settings_list_t     which;
    char                current[SETTINGS_ITEM_LEN];
    settings_pick_cb_t  cb;
    void               *ctx;
    settings_choice_t   items[LIST_MAX];
    int                 n;
} chooser_t;

static chooser_t s_chooser;   // one picker at a time

/**
 * @brief A choice tapped: close the picker and report it (index -1: Customize, which opens
 *        the theme editor and reports the new theme once it is saved).
 * v2: -1 is Customize (was Global).
 * @version 2
 * @callback
 */
static void choice_cb(lv_event_t *e)
{
    int i = (int)(intptr_t)lv_event_get_user_data(e);
    settings_pick_cb_t cb = s_chooser.cb;
    void *ctx = s_chooser.ctx;
    char value[SETTINGS_ITEM_LEN] = "";
    if (i >= 0) strcpy(value, s_chooser.items[i].value);
    settings_app_pop();
    if (i < 0) settings_theme_customize(cb, ctx);
    else if (cb) cb(value, ctx);
}

/**
 * @brief Add one choice to @p list, marked when it is the current one (Customize, index -1,
 *        with the edit symbol).
 * v2: Customize's symbol.
 * @version 2
 * @utility
 */
static void add_choice(lv_obj_t *list, const char *label, bool current, int i)
{
    const char *sym = i < 0 ? LV_SYMBOL_EDIT : current ? LV_SYMBOL_OK : LV_SYMBOL_BULLET;
    lv_obj_t *b = lv_list_add_button(list, sym, label);
    lv_obj_add_event_cb(b, choice_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    if (current) lv_obj_add_state(b, LV_STATE_CHECKED);
}

/**
 * @brief The picker screen.
 * v2: Customize first for themes (no Global).
 * @version 2
 * @utility
 * @return The frame.
 */
static lv_obj_t *chooser_screen(lv_obj_t *parent, void *ctx)
{
    LV_UNUSED(ctx);
    chooser_t *c = &s_chooser;
    lv_obj_t *frame = settings_frame_create(parent, c->title, settings_app_back_cb);
    lv_obj_t *list = lv_list_create(frame);
    lv_obj_set_size(list, SETTINGS_CONTENT_W, SETTINGS_CONTENT_H);
    lv_obj_align(list, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_text_font(list, &lv_font_montserrat_20, 0);
    if (c->which == SETTINGS_LIST_THEMES) add_choice(list, "Customize...", false, -1);
    for (int i = 0; i < c->n; i++) {
        add_choice(list, c->items[i].label, strcmp(c->items[i].value, c->current) == 0, i);
    }
    if (c->n == 0) lv_list_add_text(list, "Nothing to choose: add some on the SD card");
    return frame;
}

/**
 * @brief Push a picker.
 * v2: no Global choice.
 * @version 2
 * @req REQ-UI-001
 */
void settings_app_choose(const char *title, settings_list_t which, const char *current,
                         settings_pick_cb_t cb, void *ctx)
{
    chooser_t *c = &s_chooser;
    c->title = title;
    c->which = which;
    snprintf(c->current, sizeof(c->current), "%s", current ? current : "");
    c->cb = cb;
    c->ctx = ctx;
    c->n = s_be->list(which, c->items, LIST_MAX);
    settings_app_push(chooser_screen, NULL);
}

// ---------------------------------------------------------------------------
// Text entry
// ---------------------------------------------------------------------------

typedef struct {
    const char        *title;
    char               initial[SETTINGS_ITEM_LEN];
    settings_pick_cb_t cb;
    void              *ctx;
    lv_obj_t          *ta;
} text_entry_t;

static text_entry_t s_text;

/**
 * @brief Done: report the text and close the entry.
 * @version 1
 * @callback
 */
static void text_done_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    char value[SETTINGS_ITEM_LEN];
    snprintf(value, sizeof(value), "%s", lv_textarea_get_text(s_text.ta));
    settings_pick_cb_t cb = s_text.cb;
    void *ctx = s_text.ctx;
    settings_app_pop();
    if (cb && value[0]) cb(value, ctx);
}

/**
 * @brief The text entry screen: a one-line text area over a keyboard.
 * @version 1
 * @utility
 * @return The frame.
 */
static lv_obj_t *text_screen(lv_obj_t *parent, void *ctx)
{
    LV_UNUSED(ctx);
    lv_obj_t *frame = settings_frame_create(parent, s_text.title, settings_app_back_cb);
    settings_title_button(frame, LV_SYMBOL_OK " Done", text_done_cb, NULL);
    s_text.ta = lv_textarea_create(frame);
    lv_textarea_set_one_line(s_text.ta, true);
    lv_textarea_set_max_length(s_text.ta, SCHED_NAME_LEN - 1);
    lv_textarea_set_text(s_text.ta, s_text.initial);
    lv_obj_set_width(s_text.ta, SETTINGS_CONTENT_W);
    lv_obj_align(s_text.ta, LV_ALIGN_TOP_MID, 0, 70);
    lv_obj_t *kb = lv_keyboard_create(frame);
    lv_keyboard_set_textarea(kb, s_text.ta);
    lv_obj_set_size(kb, SETTINGS_CONTENT_W, 260);
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_event_cb(kb, text_done_cb, LV_EVENT_READY, NULL);
    return frame;
}

/**
 * @brief Push a text entry.
 * @version 1
 * @req REQ-UI-001
 */
void settings_app_enter_text(const char *title, const char *initial, settings_pick_cb_t cb,
                             void *ctx)
{
    s_text.title = title;
    snprintf(s_text.initial, sizeof(s_text.initial), "%s", initial ? initial : "");
    s_text.cb = cb;
    s_text.ctx = ctx;
    settings_app_push(text_screen, NULL);
}

// ---------------------------------------------------------------------------
// Menu entries, about, upload
// ---------------------------------------------------------------------------

/**
 * @brief A menu entry: push the screen in its user data.
 * @version 1
 * @callback
 */
static void menu_cb(lv_event_t *e)
{
    settings_screen_fn fn = (settings_screen_fn)lv_event_get_user_data(e);
    settings_app_push(fn, NULL);
}

/**
 * @brief "Now: ...": edit the showing profile (or make one for this hour in a gap).
 * @version 1
 * @callback
 */
static void now_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    settings_sched_edit_now();
}

/**
 * @brief The about screen: what the backend reports.
 * @version 1
 * @utility
 * @return The frame.
 */
static lv_obj_t *about_screen(lv_obj_t *parent, void *ctx)
{
    LV_UNUSED(ctx);
    char info[512];
    s_be->about(info, sizeof(info));
    lv_obj_t *frame = settings_frame_create(parent, "About", settings_app_back_cb);
    lv_obj_t *l = settings_text(frame, info, &lv_font_montserrat_20, UI_ROLE_TEXT);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(l, LV_ALIGN_CENTER, 0, 20);
    return frame;
}

/**
 * @brief An upload finished.
 * @version 1
 * @callback
 */
static void uploaded_cb(bool ok, const char *why, void *ctx)
{
    LV_UNUSED(ctx);
    settings_app_toast(ok ? why : (why && why[0] ? why : "Upload failed"), false);
}

/**
 * @brief Upload button: pick a picture on the phone and send it (the user data: the kind).
 * @version 1
 * @callback
 */
static void upload_cb(lv_event_t *e)
{
    settings_upload_t kind = (settings_upload_t)(uintptr_t)lv_event_get_user_data(e);
    s_be->upload(kind, uploaded_cb, NULL);
}

/**
 * @brief The Upload screen: send a background picture or an album photo from the phone.
 * @version 1
 * @utility
 * @return The frame.
 */
static lv_obj_t *upload_screen(lv_obj_t *parent, void *ctx)
{
    LV_UNUSED(ctx);
    lv_obj_t *frame = settings_frame_create(parent, "Upload", settings_app_back_cb);
    lv_obj_t *col = settings_column(frame);
    settings_text(col, "Send a picture from this phone to the clock.", &lv_font_montserrat_20,
                  UI_ROLE_TEXT);
    settings_button(settings_field(col, "Background"), LV_SYMBOL_IMAGE "  Choose a picture",
                    upload_cb, (void *)(uintptr_t)SETTINGS_UPLOAD_BACKGROUND);
    settings_text(col, "Cropped to fill the screen; it joins the backgrounds.",
                  &lv_font_montserrat_16, UI_ROLE_TEXT_FAINT);
    settings_button(settings_field(col, "Album photo"), LV_SYMBOL_IMAGE "  Choose a photo",
                    upload_cb, (void *)(uintptr_t)SETTINGS_UPLOAD_PHOTO);
    settings_text(col, "Fitted whole; it joins the photo slideshow.", &lv_font_montserrat_16,
                  UI_ROLE_TEXT_FAINT);
    return frame;
}

/**
 * @brief A device-only screen the backend builds (its creator is the user data).
 * @version 1
 * @utility
 * @return The frame.
 */
static lv_obj_t *device_screen(lv_obj_t *parent, void *ctx)
{
    lv_obj_t *(*create)(lv_obj_t *) = (lv_obj_t * (*)(lv_obj_t *)) ctx;
    return create(parent);
}

/**
 * @brief A device-only entry: push the backend's screen.
 * @version 1
 * @callback
 */
static void device_cb(lv_event_t *e)
{
    settings_app_push(device_screen, lv_event_get_user_data(e));
}

/**
 * @brief Reboot.
 * @version 1
 * @callback
 */
static void reboot_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    s_be->reboot();
}

/**
 * @brief Append the backend's optional entries to @p items from @p n.
 * v2: Upload.
 * @version 2
 * @utility
 * @return The new count.
 */
static int optional_items(settings_menu_item_t *items, int n)
{
    if (s_be->wifi_create) items[n++] = (settings_menu_item_t){ LV_SYMBOL_WIFI, "WiFi", device_cb, (void *)s_be->wifi_create };
    if (s_be->phone_create) items[n++] = (settings_menu_item_t){ LV_SYMBOL_CALL, "Phone", device_cb, (void *)s_be->phone_create };
    if (s_be->preview_create) items[n++] = (settings_menu_item_t){ LV_SYMBOL_PLAY, "Animation", device_cb, (void *)s_be->preview_create };
    if (s_be->upload) items[n++] = (settings_menu_item_t){ LV_SYMBOL_UPLOAD, "Upload", menu_cb, (void *)upload_screen };
    if (s_be->reboot) items[n++] = (settings_menu_item_t){ LV_SYMBOL_POWER, "Reboot", reboot_cb, NULL };
    return n;
}

/**
 * @brief The menu: the showing profile first, then the schedule and the rest.
 * v2: Back only when the backend has somewhere to leave to.
 * v3: "Now: <profile>" replaces the global brightness, background, theme and animation.
 * @version 3
 * @utility
 * @return The frame.
 */
static lv_obj_t *menu_screen(lv_obj_t *parent, void *ctx)
{
    LV_UNUSED(ctx);
    static char now[SCHED_NAME_LEN + 8];   // the entry's label is kept by pointer
    settings_sched_now_label(now, sizeof(now));
    settings_menu_item_t items[12] = {
        { LV_SYMBOL_IMAGE, now, now_cb, NULL },
        { LV_SYMBOL_LIST, "Schedule", menu_cb, (void *)settings_sched_screen },
        { LV_SYMBOL_SETTINGS, "About", menu_cb, (void *)about_screen },
    };
    int n = optional_items(items, 3);
    lv_obj_t *frame = settings_frame_create(parent, "Settings",
                                            s_be->leave ? settings_app_back_cb : NULL);
    settings_menu_grid(frame, items, n);
    return frame;
}

/**
 * @brief Open the settings at the menu.
 * @version 1
 * @req REQ-UI-001
 */
void settings_app_open(lv_obj_t *root, const settings_backend_t *backend)
{
    s_root = root;
    s_be = backend;
    s_top = -1;
    settings_app_push(menu_screen, NULL);
}

/**
 * @brief Delete every settings screen.
 * @version 1
 * @req REQ-UI-001
 */
void settings_app_close(void)
{
    for (; s_top >= 0; s_top--) lv_obj_delete(s_stack[s_top].obj);
    s_root = NULL;
}
