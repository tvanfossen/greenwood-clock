/**
 * @file theme_editor.c
 * v2: an Alert tab for the fourth core.
 * v3: any screen: upright, the preview above the controls; sideways, the clock's layout;
 *     each scaled down to fit.
 * @version 3
 * @brief On-screen theme editor (see theme_editor.h): the design preview on the left; on the
 *        right, tabs for the four OKLCH cores (L, C, h sliders over their gradients), the text
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
#define PANEL_W       (1024 - PANEL_X - 16)   // the controls' own width
#define WIDE_W        1024                    // narrower screens stack and scale
#define NARROW_PANEL_H 470                    // the controls' own height when stacked
#define TAB_W         70
#define APPLY_MS      80     // apply at most this often while a slider is dragged
#define CONTRAST_MIN  30     // slider units: tenths of a WCAG ratio
#define CONTRAST_MAX  120
#define EDITED_NAME   "Custom"

static const char *const TAB_NAMES[THEME_EDITOR_TAB_COUNT] = {
    "Ink", "Surface", "Accent", "Alert", "Text", "Slots",
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
    lv_obj_t               *actions;       /**< Revert / Back / Save row. */
    lv_obj_t               *save_choice;   /**< All the time / <profile> only / Cancel row. */
    char                    profile[24];   /**< Active schedule profile ("" = none). */
    lv_timer_t             *apply_timer;
    theme_editor_save_cb_t  on_save;
    theme_editor_close_cb_t on_close;
} editor_t;

static editor_t s_ed;

// ---------------------------------------------------------------------------
// Applying the working theme
// ---------------------------------------------------------------------------

/**
 * @brief The working theme's core that tab @p tab edits (ink for any non-core tab).
 * @version 1
 * @utility
 * @return Pointer into the working theme.
 */
static ui_oklch_t *core_of(theme_editor_tab_t tab)
{
    ui_oklch_t *cores[4] = { &s_ed.work.ink, &s_ed.work.surface, &s_ed.work.accent,
                             &s_ed.work.alert };
    return cores[tab <= THEME_EDITOR_ALERT ? tab : THEME_EDITOR_INK];
}

/**
 * @brief The core the current tab edits (ink for any non-core tab).
 * v2: through core_of (four cores).
 * @version 2
 * @utility
 * @return Pointer into the working theme.
 */
static ui_oklch_t *edited_core(void)
{
    return core_of(s_ed.tab);
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
 * @brief Save as @p for_profile says: hand the edited theme to the caller and close.
 * @version 1
 * @utility
 */
static void do_save(bool for_profile)
{
    strncpy(s_ed.work.name, EDITED_NAME, sizeof(s_ed.work.name) - 1);
    s_ed.saved = true;
    if (s_ed.on_save) s_ed.on_save(&s_ed.work, for_profile);
    close_editor();
}

/**
 * @brief Save tapped: with an active profile, ask where; else save as the global theme.
 * v2: asks where to save when a profile is given.
 * @version 2
 * @callback
 */
static void save_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    theme_editor_press_save();
}

/**
 * @brief The where-to-save choice: all the time (@p user 0) or the profile only (1).
 * @version 1
 * @callback
 */
static void save_where_cb(lv_event_t *e)
{
    do_save(lv_event_get_user_data(e) != NULL);
}

/**
 * @brief Cancel the where-to-save choice.
 * @version 1
 * @callback
 */
static void save_cancel_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    lv_obj_add_flag(s_ed.save_choice, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_ed.actions, LV_OBJ_FLAG_HIDDEN);
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
 * @brief The control panel at @p x, @p y, @p h tall: tabs, the bodies, Revert / Back / Save.
 * v2: the where-to-save row.
 * v3: placed by the caller; returned (a narrow layout scales it).
 * @version 3
 * @utility
 * @return The panel.
 */
static lv_obj_t *build_panel(lv_obj_t *root, int32_t x, int32_t y, int32_t h)
{
    lv_obj_t *panel = lv_obj_create(root);
    lv_obj_set_pos(panel, x, y);
    lv_obj_set_size(panel, PANEL_W, h - 8);
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
    s_ed.actions = plain(panel, 0, by, 460, 40);
    button(s_ed.actions, "Revert", 0, 0, 140, revert_cb, 0);
    button(s_ed.actions, "Back", 160, 0, 140, back_cb, 0);
    button(s_ed.actions, "Save", 320, 0, 140, save_cb, 0);
    s_ed.save_choice = plain(panel, 0, by, 460, 40);
    char only[48];
    snprintf(only, sizeof(only), "%s only", s_ed.profile);
    button(s_ed.save_choice, "All the time", 0, 0, 170, save_where_cb, 0);
    lv_label_set_long_mode(lv_obj_get_child(button(s_ed.save_choice, only, 180, 0, 190,
                                                   save_where_cb, 1), 0),
                           LV_LABEL_LONG_MODE_DOTS);
    button(s_ed.save_choice, "Cancel", 380, 0, 80, save_cancel_cb, 0);
    lv_obj_add_flag(s_ed.save_choice, LV_OBJ_FLAG_HIDDEN);
    return panel;
}

/**
 * @brief Draw @p obj at @p scale (256: its own size) from its top-left corner; input follows.
 * @version 1
 * @utility
 */
static void scale_from_corner(lv_obj_t *obj, int32_t scale)
{
    lv_obj_set_style_transform_pivot_x(obj, 0, 0);
    lv_obj_set_style_transform_pivot_y(obj, 0, 0);
    lv_obj_set_style_transform_scale(obj, scale, 0);
}

/**
 * @brief A screen narrower than the clock's, @p w x @p h, held upright: the controls along
 *        the bottom and the preview above them, each at its own layout scaled to the width
 *        (never enlarged; the controls take at most 60 % of the height).
 * @version 1
 * @utility
 */
static void layout_stacked(int32_t w, int32_t h)
{
    int32_t k = LV_MIN((w - 16) * 256 / PANEL_W, h * 154 / NARROW_PANEL_H);   // 154/256: 60 %
    k = LV_MIN(k, 256);
    int32_t panel_h = NARROW_PANEL_H * k / 256;
    lv_obj_t *panel = build_panel(s_ed.root, 8, h - panel_h - 8, NARROW_PANEL_H + 8);
    scale_from_corner(panel, k);
    int32_t kp = LV_MIN((w - 16) * 256 / PANE_W, 256);
    int32_t pane_h = (h - panel_h - 24) * 256 / kp;
    lv_obj_t *pane = plain(s_ed.root, 8, 0, PANE_W, pane_h);
    te_preview_create(pane);   // transparent: the picture shows
    scale_from_corner(pane, kp);
}

/**
 * @brief The clock's side-by-side layout in a 1024-wide box of @p h, drawn at @p scale.
 * @version 1
 * @utility
 */
static void layout_side_by_side(int32_t h, int32_t scale)
{
    int32_t lh = h * 256 / scale;   // the box's own height
    lv_obj_t *box = plain(s_ed.root, 0, 0, WIDE_W, lh);
    te_preview_create(plain(box, 16, 0, PANE_W, lh));   // transparent: the picture shows
    build_panel(box, PANEL_X, 0, lh);
    if (scale < 256) scale_from_corner(box, scale);
}

/**
 * @brief Build the editor on @p parent from @p y down, starting from the current theme; Save
 *        asks global-or-@p profile when a profile is given.
 * v2: takes the active profile's name.
 * v3: the parent's size: upright screens stack and scale, wide ones scale the clock's
 *     layout down when narrower than it.
 * @version 3
 * @req REQ-UI-004
 * @return The editor's root object.
 */
lv_obj_t *theme_editor_create(lv_obj_t *parent, int32_t y, const char *profile,
                              theme_editor_save_cb_t on_save, theme_editor_close_cb_t on_close)
{
    memset(&s_ed, 0, sizeof(s_ed));
    if (profile) strncpy(s_ed.profile, profile, sizeof(s_ed.profile) - 1);
    s_ed.orig = *ui_theme_get();
    s_ed.work = s_ed.orig;
    s_ed.on_save = on_save;
    s_ed.on_close = on_close;
    int32_t w = lv_obj_get_width(parent), h = lv_obj_get_height(parent) - y;
    s_ed.root = plain(parent, 0, y, w, h);
    lv_obj_add_event_cb(s_ed.root, root_delete_cb, LV_EVENT_DELETE, NULL);
    if (w < h) layout_stacked(w, h);                             // a phone held upright
    else layout_side_by_side(h, LV_MIN(w * 256 / WIDE_W, 256));  // the clock; a phone sideways
    s_ed.apply_timer = lv_timer_create(apply_timer_cb, APPLY_MS, NULL);
    lv_timer_pause(s_ed.apply_timer);
    theme_editor_set_tab(THEME_EDITOR_INK);
    return s_ed.root;
}

/**
 * @brief Press Save: show the where-to-save choice with a profile, else save globally.
 * @version 1
 * @req REQ-UI-004
 */
void theme_editor_press_save(void)
{
    if (!s_ed.root) return;
    if (!s_ed.profile[0]) {
        do_save(false);
        return;
    }
    lv_obj_add_flag(s_ed.actions, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(s_ed.save_choice, LV_OBJ_FLAG_HIDDEN);
}

/**
 * @brief Show tab @p tab.
 * v2: the alert core shows the core body too.
 * @version 2
 * @req REQ-UI-004
 */
void theme_editor_set_tab(theme_editor_tab_t tab)
{
    if (!s_ed.root || (unsigned)tab >= THEME_EDITOR_TAB_COUNT) return;
    s_ed.tab = tab;
    for (int i = 0; i < THEME_EDITOR_TAB_COUNT; i++) {
        lv_obj_set_state(s_ed.tabs[i], LV_STATE_CHECKED, i == (int)tab);
    }
    lv_obj_set_flag(s_ed.core_body, LV_OBJ_FLAG_HIDDEN, tab > THEME_EDITOR_ALERT);
    lv_obj_set_flag(s_ed.contrast_body, LV_OBJ_FLAG_HIDDEN, tab != THEME_EDITOR_CONTRAST);
    lv_obj_set_flag(s_ed.slots_body, LV_OBJ_FLAG_HIDDEN, tab != THEME_EDITOR_SLOTS);
    show_values();
}

/**
 * @brief Set the edited theme's core @p tab to @p c and apply it.
 * v2: the alert core too.
 * @version 2
 * @req REQ-UI-004
 */
void theme_editor_set_core(theme_editor_tab_t tab, ui_oklch_t c)
{
    if (!s_ed.root || tab > THEME_EDITOR_ALERT) return;
    *core_of(tab) = c;
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
