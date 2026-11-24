/**
 * @file theme_editor.c
 * @version 1
 * @brief On-screen theme editor (see theme_editor.h): the design preview on the left; on the
 *        right, tabs for the three OKLCH cores (L, C, h sliders over their gradients), the text
 *        contrast target, and the slot grid. Every edit is applied to the running theme
 *        (throttled while dragging), so the whole screen repaints with it.
 */

#include "theme_editor.h"
#include "theme_editor_priv.h"
#include "ui_theme_bind.h"
#include "ui_theme_lvgl.h"

#include <stdio.h>
#include <string.h>

#define PANE_W        500
#define PANEL_X       524
#define TAB_W         86
#define APPLY_MS      80     // apply at most this often while a slider is dragged
#define CONTRAST_MIN  30     // slider units: tenths of a WCAG ratio
#define CONTRAST_MAX  120
#define EDITED_NAME   "Custom"

static const char *const TAB_NAMES[THEME_EDITOR_TAB_COUNT] = {
    "Ink", "Surface", "Accent", "Text", "Slots",
};

/** The fill roles a slot cycles through when tapped. */
static const ui_role_t FILL_ROLES[] = {
    UI_ROLE_SURFACE, UI_ROLE_SURFACE_RAISED, UI_ROLE_SURFACE_INSET, UI_ROLE_BORDER,
    UI_ROLE_ACCENT, UI_ROLE_ACCENT_ALT, UI_ROLE_ERROR, UI_ROLE_ERROR_STRONG, UI_ROLE_WARN,
};
#define FILL_ROLE_COUNT (sizeof(FILL_ROLES) / sizeof(FILL_ROLES[0]))

/** Everything the open editor holds. */
typedef struct {
    ui_theme_t              work;          /**< The theme as edited. */
    ui_theme_t              orig;          /**< The theme the editor opened with. */
    bool                    saved;
    theme_editor_tab_t      tab;
    lv_obj_t               *root;
    lv_obj_t               *tabs[THEME_EDITOR_TAB_COUNT];
    lv_obj_t               *core_body;
    lv_obj_t               *contrast_body;
    lv_obj_t               *slots_body;
    te_track_t              tracks[TE_CHANNELS];
    lv_obj_t               *swatch;
    lv_obj_t               *swatch_label;
    lv_obj_t               *contrast_slider;
    lv_obj_t               *contrast_label;
    lv_obj_t               *slot_labels[UI_SLOT_COUNT];
    lv_timer_t             *apply_timer;
    theme_editor_save_cb_t  on_save;
    theme_editor_close_cb_t on_close;
} editor_t;

static editor_t s_ed;

// ---------------------------------------------------------------------------
// Applying the working theme
// ---------------------------------------------------------------------------

/**
 * @brief The core the current tab edits (ink for any non-core tab).
 * @version 1
 * @utility
 * @return Pointer into the working theme.
 */
static ui_oklch_t *edited_core(void)
{
    ui_oklch_t *cores[3] = { &s_ed.work.ink, &s_ed.work.surface, &s_ed.work.accent };
    return cores[s_ed.tab <= THEME_EDITOR_ACCENT ? s_ed.tab : THEME_EDITOR_INK];
}

/**
 * @brief Write the working theme's values into the visible controls.
 * @version 1
 * @utility
 */
static void show_values(void)
{
    ui_oklch_t core = *edited_core();
    for (int i = 0; i < TE_CHANNELS; i++) te_track_show(&s_ed.tracks[i], core);
    lv_color_t c = ui_oklch_to_color(core);
    lv_obj_set_style_bg_color(s_ed.swatch, c, 0);   // the edited colour itself: shown, not themed
    lv_label_set_text_fmt(s_ed.swatch_label, "#%02x%02x%02x", c.red, c.green, c.blue);
    lv_slider_set_value(s_ed.contrast_slider, (int32_t)(s_ed.work.contrast * 10.0f + 0.5f),
                        LV_ANIM_OFF);
    lv_label_set_text_fmt(s_ed.contrast_label, "Text %.1f:1   muted %.1f   hints %.1f",
                          (double)s_ed.work.contrast,
                          (double)ui_theme_target(UI_ROLE_TEXT_MUTED),
                          (double)ui_theme_target(UI_ROLE_TEXT_FAINT));
    for (int i = 0; i < UI_SLOT_COUNT; i++) {
        lv_label_set_text_fmt(s_ed.slot_labels[i], "%s\n%s", ui_theme_slot_name((ui_slot_t)i),
                              ui_theme_role_name(ui_theme_slot_role((ui_slot_t)i)));
    }
}

/**
 * @brief Make the working theme the running one: every bound object repaints.
 * @version 1
 * @utility
 */
static void apply_now(void)
{
    ui_theme_set(&s_ed.work);
    ui_theme_lvgl_refresh();
    ui_bind_refresh_all();
    show_values();
}

/**
 * @brief Throttle timer: apply the pending edit.
 * @version 1
 * @callback
 */
static void apply_timer_cb(lv_timer_t *t)
{
    lv_timer_pause(t);
    apply_now();
}

/**
 * @brief Apply soon (coalescing a slider drag into one repaint per APPLY_MS).
 * @version 1
 * @utility
 */
static void apply_soon(void)
{
    lv_timer_reset(s_ed.apply_timer);
    lv_timer_resume(s_ed.apply_timer);
}

// ---------------------------------------------------------------------------
// Control callbacks
// ---------------------------------------------------------------------------

/**
 * @brief An OKLCH slider moved: update the edited core.
 * @version 1
 * @callback
 */
static void track_cb(lv_event_t *e)
{
    const te_track_t *t = lv_event_get_user_data(e);
    *edited_core() = te_track_apply(t, *edited_core());
    apply_soon();
}

/**
 * @brief The contrast slider moved.
 * @version 1
 * @callback
 */
static void contrast_cb(lv_event_t *e)
{
    s_ed.work.contrast = (float)lv_slider_get_value(lv_event_get_target(e)) / 10.0f;
    apply_soon();
}

/**
 * @brief A slot cell tapped: move the slot to the next fill role.
 * @version 1
 * @callback
 */
static void slot_cb(lv_event_t *e)
{
    int slot = (int)(intptr_t)lv_event_get_user_data(e);
    size_t next = 0;
    for (size_t i = 0; i < FILL_ROLE_COUNT; i++) {
        if (FILL_ROLES[i] == s_ed.work.slots[slot]) next = (i + 1) % FILL_ROLE_COUNT;
    }
    s_ed.work.slots[slot] = (uint8_t)FILL_ROLES[next];
    apply_now();
}

/**
 * @brief A tab tapped.
 * @version 1
 * @callback
 */
static void tab_cb(lv_event_t *e)
{
    theme_editor_set_tab((theme_editor_tab_t)(intptr_t)lv_event_get_user_data(e));
}

/**
 * @brief Revert: back to the theme the editor opened with.
 * @version 1
 * @callback
 */
static void revert_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    s_ed.work = s_ed.orig;
    apply_now();
}

/**
 * @brief Close: the caller removes the editor (on_close), or it deletes itself without one.
 * @version 1
 * @utility
 */
static void close_editor(void)
{
    if (s_ed.on_close) s_ed.on_close();
    else lv_obj_delete(s_ed.root);
}

/**
 * @brief Save: hand the edited theme to the caller and close.
 * @version 1
 * @callback
 */
static void save_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    strncpy(s_ed.work.name, EDITED_NAME, sizeof(s_ed.work.name) - 1);
    s_ed.saved = true;
    if (s_ed.on_save) s_ed.on_save(&s_ed.work);
    close_editor();
}

/**
 * @brief Back: close without saving (the opening theme comes back).
 * @version 1
 * @callback
 */
static void back_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    close_editor();
}

/**
 * @brief The editor is going (closed, or its parent deleted): stop the timer, restore the
 *        opening theme unless saved.
 * @version 1
 * @callback
 */
static void root_delete_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    if (s_ed.apply_timer) lv_timer_delete(s_ed.apply_timer);
    s_ed.apply_timer = NULL;
    s_ed.root = NULL;
    if (!s_ed.saved) {
        ui_theme_set(&s_ed.orig);
        ui_theme_lvgl_refresh();
        ui_bind_refresh_all();
    }
}

// ---------------------------------------------------------------------------
// Building
// ---------------------------------------------------------------------------

/**
 * @brief A plain transparent, non-scrolling container.
 * @version 1
 * @utility
 * @return The container.
 */
static lv_obj_t *plain(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

/**
 * @brief A button labelled @p text calling @p cb with @p data.
 * @version 1
 * @utility
 * @return The button.
 */
static lv_obj_t *button(lv_obj_t *parent, const char *text, int32_t x, int32_t y, int32_t w,
                        lv_event_cb_t cb, intptr_t data)
{
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_size(b, w, 40);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, (void *)data);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, text);
    lv_obj_center(l);
    return b;
}

/**
 * @brief The core body: L, C and h tracks and the colour swatch.
 * @version 1
 * @utility
 */
static void build_core_body(lv_obj_t *panel)
{
    s_ed.core_body = plain(panel, 0, 56, 470, 230);
    for (int i = 0; i < TE_CHANNELS; i++) {
        te_track_create(&s_ed.tracks[i], s_ed.core_body, (te_channel_t)i, i * 52, track_cb);
    }
    s_ed.swatch = lv_obj_create(s_ed.core_body);
    lv_obj_set_pos(s_ed.swatch, 110, 162);
    lv_obj_set_size(s_ed.swatch, 64, 48);
    lv_obj_set_style_bg_opa(s_ed.swatch, LV_OPA_COVER, 0);
    ui_bind_border(s_ed.swatch, UI_ROLE_BORDER);
    s_ed.swatch_label = lv_label_create(s_ed.core_body);
    lv_obj_set_pos(s_ed.swatch_label, 190, 174);
    lv_obj_set_style_text_font(s_ed.swatch_label, &lv_font_montserrat_20, 0);
    ui_bind_text(s_ed.swatch_label, UI_ROLE_TEXT_MUTED);
}

/**
 * @brief The contrast body: the target slider and the resulting tiers.
 * @version 1
 * @utility
 */
static void build_contrast_body(lv_obj_t *panel)
{
    s_ed.contrast_body = plain(panel, 0, 56, 470, 230);
    s_ed.contrast_label = lv_label_create(s_ed.contrast_body);
    lv_obj_set_pos(s_ed.contrast_label, 0, 0);
    lv_obj_set_style_text_font(s_ed.contrast_label, &lv_font_montserrat_20, 0);
    ui_bind_text(s_ed.contrast_label, UI_ROLE_TEXT);
    s_ed.contrast_slider = lv_slider_create(s_ed.contrast_body);
    lv_obj_set_pos(s_ed.contrast_slider, 10, 52);
    lv_obj_set_width(s_ed.contrast_slider, 440);
    lv_slider_set_range(s_ed.contrast_slider, CONTRAST_MIN, CONTRAST_MAX);
    lv_obj_add_event_cb(s_ed.contrast_slider, contrast_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_t *hint = lv_label_create(s_ed.contrast_body);
    lv_label_set_text(hint, "WCAG ratio primary text reaches against\nwhat is behind it. 4.5 = AA, 7 = AAA.");
    lv_obj_set_pos(hint, 0, 90);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_16, 0);
    ui_bind_text(hint, UI_ROLE_TEXT_MUTED);
}

/**
 * @brief The slot grid: one cell per slot, filled with its role; tap for the next role.
 * @version 1
 * @utility
 */
static void build_slots_body(lv_obj_t *panel)
{
    s_ed.slots_body = plain(panel, 0, 56, 470, 290);
    for (int i = 0; i < UI_SLOT_COUNT; i++) {
        lv_obj_t *b = lv_button_create(s_ed.slots_body);
        lv_obj_set_pos(b, (i % 2) * 236, (i / 2) * 56);
        lv_obj_set_size(b, 228, 50);
        lv_obj_add_event_cb(b, slot_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        ui_bind_fill(b, (ui_slot_t)i);
        s_ed.slot_labels[i] = lv_label_create(b);
        lv_obj_set_style_text_font(s_ed.slot_labels[i], &lv_font_montserrat_14, 0);
        lv_obj_center(s_ed.slot_labels[i]);
        ui_bind_text(s_ed.slot_labels[i], UI_ROLE_TEXT);
    }
}

/**
 * @brief The control panel: tabs, the three bodies, Revert / Back / Save.
 * @version 1
 * @utility
 */
static void build_panel(lv_obj_t *root, int32_t h)
{
    lv_obj_t *panel = lv_obj_create(root);
    lv_obj_set_pos(panel, PANEL_X, 0);
    lv_obj_set_size(panel, 1024 - PANEL_X - 16, h - 8);
    lv_obj_set_style_bg_opa(panel, LV_OPA_90, 0);
    lv_obj_set_style_pad_all(panel, 12, 0);
    lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    ui_bind_fill(panel, UI_SLOT_DIALOG);
    ui_bind_border(panel, UI_ROLE_BORDER);
    for (int i = 0; i < THEME_EDITOR_TAB_COUNT; i++) {
        s_ed.tabs[i] = button(panel, TAB_NAMES[i], i * (TAB_W + 6), 0, TAB_W, tab_cb, i);
    }
    build_core_body(panel);
    build_contrast_body(panel);
    build_slots_body(panel);
    int32_t by = h - 8 - 24 - 40;
    button(panel, "Revert", 0, by, 140, revert_cb, 0);
    button(panel, "Back", 160, by, 140, back_cb, 0);
    button(panel, "Save", 320, by, 140, save_cb, 0);
}

/**
 * @brief Build the editor on @p parent from @p y down, starting from the current theme.
 * @version 1
 * @req REQ-UI-004
 * @return The editor's root object.
 */
lv_obj_t *theme_editor_create(lv_obj_t *parent, int32_t y, theme_editor_save_cb_t on_save,
                              theme_editor_close_cb_t on_close)
{
    memset(&s_ed, 0, sizeof(s_ed));
    s_ed.orig = *ui_theme_get();
    s_ed.work = s_ed.orig;
    s_ed.on_save = on_save;
    s_ed.on_close = on_close;
    int32_t h = lv_obj_get_height(parent) - y;
    s_ed.root = plain(parent, 0, y, 1024, h);
    lv_obj_add_event_cb(s_ed.root, root_delete_cb, LV_EVENT_DELETE, NULL);
    te_preview_create(plain(s_ed.root, 16, 0, PANE_W, h));   // transparent: the picture shows
    build_panel(s_ed.root, h);
    s_ed.apply_timer = lv_timer_create(apply_timer_cb, APPLY_MS, NULL);
    lv_timer_pause(s_ed.apply_timer);
    theme_editor_set_tab(THEME_EDITOR_INK);
    return s_ed.root;
}

/**
 * @brief Show tab @p tab.
 * @version 1
 * @req REQ-UI-004
 */
void theme_editor_set_tab(theme_editor_tab_t tab)
{
    if (!s_ed.root || (unsigned)tab >= THEME_EDITOR_TAB_COUNT) return;
    s_ed.tab = tab;
    for (int i = 0; i < THEME_EDITOR_TAB_COUNT; i++) {
        lv_obj_set_state(s_ed.tabs[i], LV_STATE_CHECKED, i == (int)tab);
    }
    lv_obj_set_flag(s_ed.core_body, LV_OBJ_FLAG_HIDDEN, tab > THEME_EDITOR_ACCENT);
    lv_obj_set_flag(s_ed.contrast_body, LV_OBJ_FLAG_HIDDEN, tab != THEME_EDITOR_CONTRAST);
    lv_obj_set_flag(s_ed.slots_body, LV_OBJ_FLAG_HIDDEN, tab != THEME_EDITOR_SLOTS);
    show_values();
}

/**
 * @brief Set the edited theme's core @p tab to @p c and apply it.
 * @version 1
 * @req REQ-UI-004
 */
void theme_editor_set_core(theme_editor_tab_t tab, ui_oklch_t c)
{
    if (!s_ed.root || tab > THEME_EDITOR_ACCENT) return;
    ui_oklch_t *cores[3] = { &s_ed.work.ink, &s_ed.work.surface, &s_ed.work.accent };
    *cores[tab] = c;
    apply_now();
}

/**
 * @brief The theme as edited so far.
 * @version 1
 * @req REQ-UI-004
 * @return The working theme.
 */
const ui_theme_t *theme_editor_theme(void)
{
    return &s_ed.work;
}
