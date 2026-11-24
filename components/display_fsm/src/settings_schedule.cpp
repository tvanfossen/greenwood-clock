/**
 * @file settings_schedule.cpp
 * @version 1
 * @brief Settings: the background schedule editor. A week grid (days x time slots) whose cells
 *        name the profile shown then; slot times, profiles and each profile's settings
 *        (background image, theme, brightness, clock animation, each "same as Settings"
 *        when not set) are edited here. Every change is saved at once and applied.
 */
#include "settings_ui.h"
#include "display_fsm.h"
#include "ui_theme.h"
#include "theme_store.h"

#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <strings.h>
#include "esp_log.h"
#include "esp_lvgl_port.h"

extern "C" {
#include "bg_schedule.h"
#include "clock_anim_pref.h"
}

static const char *TAG = "settings_sched";

static const char *const DAY_NAMES[BG_SCHED_DAYS] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
static_assert(BG_SCHED_PROFILES <= UI_THEME_SWATCH_COUNT, "one distinct swatch per profile");
#define INHERIT_TEXT  "Same as Settings"
#define GRID_DAY_W    96
#define GRID_ADD_W    52
#define GRID_ROW_H    43
#define MAX_CHOICES   40
#define IMAGE_DIR     "/sdcard/backgrounds"

static bg_schedule_t s_edit;          // working copy: loaded on opening the editor, saved per edit
static int           s_profile;       // profile open in the profile screen
static lv_obj_t     *s_grid;          // week grid (rebuilt after each edit)
static lv_obj_t     *s_profile_list;  // profiles list (rebuilt when a profile changes)

// ============================================================================
// Saving
// ============================================================================

/**
 * @brief Save the working copy (NVS write off the LVGL lock) and have the clock re-apply it.
 * @version 1
 * @req REQ-BG-004
 */
static void sched_save(void)
{
    lvgl_port_unlock();
    esp_err_t err = bg_schedule_set(&s_edit);
    lvgl_port_lock(0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "schedule save failed: %s", esp_err_to_name(err));
        return;
    }
    display_event_t evt = {};
    evt.type = DISPLAY_EVT_REFRESH_BG;
    display_fsm_send_event(&evt);
}

// ============================================================================
// Shared widgets: modal panel, choice list
// ============================================================================

/**
 * @brief A modal panel over everything (dimmed backdrop on the top layer).
 * v2: colours from the theme roles (ui_theme), no literals.
 * @version 2
 * @utility
 * @return The panel; delete its parent (the backdrop) to close it.
 */
static lv_obj_t *modal_panel(const char *title, int32_t w, int32_t h)
{
    lv_obj_t *backdrop = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(backdrop);
    lv_obj_set_size(backdrop, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(backdrop, ui_theme_color(UI_ROLE_SCREEN), 0);
    lv_obj_set_style_bg_opa(backdrop, LV_OPA_60, 0);
    lv_obj_add_flag(backdrop, LV_OBJ_FLAG_CLICKABLE);   // swallow taps behind the panel

    lv_obj_t *panel = lv_obj_create(backdrop);
    lv_obj_set_size(panel, w, h);
    lv_obj_center(panel);
    lv_obj_set_style_bg_color(panel, ui_theme_color(UI_ROLE_SURFACE_RAISED), 0);
    lv_obj_set_style_border_color(panel, ui_theme_color(UI_ROLE_BORDER), 0);
    lv_obj_set_style_radius(panel, 16, 0);
    lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl = lv_label_create(panel);
    lv_label_set_text(lbl, title);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(lbl, ui_theme_color(UI_ROLE_TEXT), 0);
    lv_obj_align(lbl, LV_ALIGN_TOP_MID, 0, 0);
    return panel;
}

/**
 * @brief Close the modal that @p obj belongs to.
 * @version 1
 * @utility
 */
static void modal_close(lv_obj_t *obj)
{
    while (obj && lv_obj_get_parent(obj) != lv_layer_top()) obj = lv_obj_get_parent(obj);
    if (obj) lv_obj_delete_async(obj);
}

typedef void (*choice_cb_t)(int choice);

/**
 * @brief Choice list item clicked: report the choice and close the list.
 * @version 1
 * @callback
 */
static void choice_click_cb(lv_event_t *e)
{
    lv_obj_t *btn = (lv_obj_t *)lv_event_get_target(e);
    choice_cb_t cb = (choice_cb_t)lv_event_get_user_data(e);
    int choice = (int)(intptr_t)lv_obj_get_user_data(btn);
    modal_close(btn);
    cb(choice);
}

/**
 * @brief A modal list of @p n choices (@p current highlighted); @p cb gets the picked index.
 * @version 1
 * @utility
 */
static void choice_popup(const char *title, const char *const *labels, int n, int current,
                         choice_cb_t cb)
{
    lv_obj_t *panel = modal_panel(title, 560, 460);
    lv_obj_t *list = lv_list_create(panel);
    lv_obj_set_size(list, 520, 360);
    lv_obj_align(list, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_text_font(list, &lv_font_montserrat_20, 0);
    for (int i = 0; i < n; i++) {
        lv_obj_t *btn = lv_list_add_button(list, i == current ? LV_SYMBOL_OK : NULL, labels[i]);
        lv_obj_set_user_data(btn, (void *)(intptr_t)i);
        lv_obj_add_event_cb(btn, choice_click_cb, LV_EVENT_CLICKED, (void *)cb);
    }
}

// ============================================================================
// Week grid
// ============================================================================

static void grid_build(void);

/**
 * @brief lv_async_call adapter for grid_build.
 * @version 1
 * @callback
 */
static void grid_build_async(void *unused)
{
    LV_UNUSED(unused);
    grid_build();
}

/**
 * @brief Save, then rebuild the grid after the current event: the tapped button is part of it.
 * @version 1
 * @utility
 */
static void grid_changed(void)
{
    sched_save();
    lv_async_call(grid_build_async, NULL);
}

/**
 * @brief Grid cell tapped: show the next profile there.
 * @version 1
 * @callback
 */
static void cell_click_cb(lv_event_t *e)
{
    intptr_t v = (intptr_t)lv_event_get_user_data(e);
    bg_schedule_cycle_cell(&s_edit, (int)(v / BG_SCHED_SLOTS), (int)(v % BG_SCHED_SLOTS));
    grid_changed();
}

/**
 * @brief Day tapped: copy its row to every day.
 * @version 1
 * @callback
 */
static void day_click_cb(lv_event_t *e)
{
    int day = (int)(intptr_t)lv_event_get_user_data(e);
    bg_schedule_copy_day(&s_edit, day);
    ESP_LOGI(TAG, "copied %s to every day", DAY_NAMES[day]);
    grid_changed();
}

/**
 * @brief "+" tapped: add a time slot.
 * @version 1
 * @callback
 */
static void slot_add_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    if (!bg_schedule_add_slot(&s_edit)) return;
    grid_changed();
}

static void time_popup(int slot);

/**
 * @brief Slot time tapped: edit it.
 * @version 1
 * @callback
 */
static void slot_time_cb(lv_event_t *e)
{
    time_popup((int)(intptr_t)lv_event_get_user_data(e));
}

/**
 * @brief A grid button: @p text on @p fill (text legible on it), calling @p cb with @p data
 *        when tapped.
 * v2: the fill is a theme colour; the label is made legible on it.
 * @version 2
 * @utility
 * @return The button.
 */
static lv_obj_t *grid_button(int32_t x, int32_t y, int32_t w, const char *text, lv_color_t fill,
                             lv_event_cb_t cb, intptr_t data)
{
    lv_obj_t *btn = lv_button_create(s_grid);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_size(btn, w, GRID_ROW_H - 6);
    lv_obj_set_style_bg_color(btn, fill, 0);
    lv_obj_set_style_text_color(btn, ui_theme_text_on(UI_ROLE_TEXT, fill), 0);
    lv_obj_set_style_radius(btn, 8, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, (void *)data);
    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, text);
    lv_label_set_long_mode(lbl, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(lbl, w - 12);
    lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(lbl);
    return btn;
}

/**
 * @brief (Re)build the week grid: a row of slot start times, then one row of profile cells per
 *        day.
 * v2: colours from the theme roles (ui_theme), no literals.
 * @version 2
 * @req REQ-BG-004
 */
static void grid_build(void)
{
    if (!s_grid) return;
    lv_obj_clean(s_grid);
    int n = s_edit.slot_count;
    int32_t col_w = (lv_obj_get_content_width(s_grid) - GRID_DAY_W - GRID_ADD_W) / n;
    for (int i = 0; i < n; i++) {
        char hhmm[8];
        snprintf(hhmm, sizeof(hhmm), "%02u:%02u", s_edit.slot_start[i] / 60, s_edit.slot_start[i] % 60);
        grid_button(GRID_DAY_W + i * col_w, 0, col_w - 6, hhmm, ui_theme_color(UI_ROLE_SURFACE_INSET), slot_time_cb, i);
    }
    if (n < BG_SCHED_SLOTS) {
        grid_button(GRID_DAY_W + n * col_w, 0, GRID_ADD_W - 6, LV_SYMBOL_PLUS, ui_theme_color(UI_ROLE_SURFACE_INSET),
                    slot_add_cb, 0);
    }
    for (int d = 0; d < BG_SCHED_DAYS; d++) {
        int32_t y = (d + 1) * GRID_ROW_H;
        grid_button(0, y, GRID_DAY_W - 6, DAY_NAMES[d], ui_theme_color(UI_ROLE_SURFACE_INSET), day_click_cb, d);
        for (int i = 0; i < n; i++) {
            int p = s_edit.grid[d][i];
            grid_button(GRID_DAY_W + i * col_w, y, col_w - 6, s_edit.profiles[p].name,
                        ui_theme_swatch(p), cell_click_cb, d * BG_SCHED_SLOTS + i);
        }
    }
}

// ============================================================================
// Slot time popup
// ============================================================================

static int s_time_slot;

/**
 * @brief Time popup "Save": move the slot if it stays between its neighbours.
 * @version 1
 * @callback
 */
static void time_save_cb(lv_event_t *e)
{
    lv_obj_t *panel = (lv_obj_t *)lv_event_get_user_data(e);
    lv_obj_t *hours = lv_obj_get_child(panel, 1);
    lv_obj_t *mins  = lv_obj_get_child(panel, 2);
    lv_obj_t *note  = lv_obj_get_child(panel, 3);
    int minute = (int)lv_roller_get_selected(hours) * 60 + (int)lv_roller_get_selected(mins) * 5;
    if (!bg_schedule_set_slot_start(&s_edit, s_time_slot, minute)) {
        lv_label_set_text(note, "Must fall between the slots before and after it");
        return;
    }
    modal_close(panel);
    sched_save();
    grid_build();
}

/**
 * @brief Time popup "Remove slot".
 * @version 1
 * @callback
 */
static void time_remove_cb(lv_event_t *e)
{
    lv_obj_t *panel = (lv_obj_t *)lv_event_get_user_data(e);
    modal_close(panel);
    if (!bg_schedule_remove_slot(&s_edit, s_time_slot)) return;
    sched_save();
    grid_build();
}

/**
 * @brief Close the modal a button belongs to.
 * @version 1
 * @callback
 */
static void modal_cancel_cb(lv_event_t *e)
{
    modal_close((lv_obj_t *)lv_event_get_target(e));
}

/**
 * @brief A text button in a modal panel.
 * @version 1
 * @utility
 * @return The button.
 */
static lv_obj_t *panel_button(lv_obj_t *panel, const char *text, lv_align_t align, int32_t x,
                              lv_event_cb_t cb, void *data)
{
    lv_obj_t *btn = lv_button_create(panel);
    lv_obj_set_size(btn, 150, 50);
    lv_obj_align(btn, align, x, 0);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, data);
    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, text);
    lv_obj_center(lbl);
    return btn;
}

/**
 * @brief Edit slot @p slot's start time (hour and 5-minute rollers), or remove the slot.
 * v2: colours from the theme roles (ui_theme), no literals.
 * @version 2
 * @req REQ-BG-004
 */
static void time_popup(int slot)
{
    s_time_slot = slot;
    char title[32];
    snprintf(title, sizeof(title), "Slot %d starts at", slot + 1);
    lv_obj_t *panel = modal_panel(title, 520, 400);   // child 0: the title

    char hours[24 * 3], mins[12 * 3];
    hours[0] = mins[0] = '\0';
    for (int h = 0; h < 24; h++) snprintf(hours + strlen(hours), 4, h ? "\n%02d" : "%02d", h);
    for (int m = 0; m < 60; m += 5) snprintf(mins + strlen(mins), 4, m ? "\n%02d" : "%02d", m);
    lv_obj_t *rh = lv_roller_create(panel);   // child 1
    lv_roller_set_options(rh, hours, LV_ROLLER_MODE_INFINITE);
    lv_roller_set_selected(rh, s_edit.slot_start[slot] / 60, LV_ANIM_OFF);
    lv_obj_align(rh, LV_ALIGN_TOP_MID, -70, 50);
    lv_obj_t *rm = lv_roller_create(panel);   // child 2
    lv_roller_set_options(rm, mins, LV_ROLLER_MODE_INFINITE);
    lv_roller_set_selected(rm, (s_edit.slot_start[slot] % 60) / 5, LV_ANIM_OFF);
    lv_obj_align(rm, LV_ALIGN_TOP_MID, 70, 50);
    lv_obj_t *note = lv_label_create(panel);  // child 3
    lv_label_set_text(note, "");
    lv_obj_set_style_text_color(note, ui_theme_color(UI_ROLE_ERROR), 0);
    lv_obj_align(note, LV_ALIGN_BOTTOM_MID, 0, -64);

    panel_button(panel, "Save", LV_ALIGN_BOTTOM_RIGHT, 0, time_save_cb, panel);
    panel_button(panel, "Cancel", LV_ALIGN_BOTTOM_MID, 0, modal_cancel_cb, NULL);
    if (s_edit.slot_count > 1) {
        panel_button(panel, "Remove slot", LV_ALIGN_BOTTOM_LEFT, 0, time_remove_cb, panel);
    }
}

// ============================================================================
// Schedule screen
// ============================================================================

/**
 * @brief "Profiles" tapped.
 * @version 1
 * @callback
 */
static void profiles_btn_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    show_sub(SUB_SCHED_PROFILES);
}

/**
 * @brief The schedule screen went away: forget its grid.
 * @version 1
 * @callback
 */
static void schedule_delete_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    s_grid = nullptr;
}

/**
 * @brief Build the schedule screen: hint, "Profiles" button and the week grid.
 * v2: colours from the theme roles (ui_theme), no literals.
 * @version 2
 * @req REQ-BG-004
 * @req REQ-UI-001
 * @return The sub-screen container.
 */
lv_obj_t *create_sub_schedule(lv_obj_t *parent)
{
    bg_schedule_get(&s_edit);
    lv_obj_t *cont = make_sub_container(parent, "Background Schedule");
    lv_obj_add_event_cb(cont, schedule_delete_cb, LV_EVENT_DELETE, NULL);

    lv_obj_t *btn = lv_button_create(cont);
    lv_obj_set_size(btn, 140, 50);
    lv_obj_align(btn, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_add_event_cb(btn, profiles_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, "Profiles");
    lv_obj_center(lbl);

    lv_obj_t *hint = lv_label_create(cont);
    lv_label_set_text(hint, "Tap a cell for the next profile. Tap a day to copy it to every day.");
    lv_obj_set_style_text_color(hint, ui_theme_color(UI_ROLE_TEXT_MUTED), 0);
    lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 54);

    s_grid = lv_obj_create(cont);
    lv_obj_remove_style_all(s_grid);
    lv_obj_set_size(s_grid, LV_PCT(100), (BG_SCHED_DAYS + 1) * GRID_ROW_H);
    lv_obj_align(s_grid, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_remove_flag(s_grid, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_update_layout(s_grid);
    grid_build();
    return cont;
}

// ============================================================================
// Profiles list
// ============================================================================

/**
 * @brief Profile tapped: open it.
 * @version 1
 * @callback
 */
static void profile_open_cb(lv_event_t *e)
{
    s_profile = (int)(intptr_t)lv_event_get_user_data(e);
    show_sub(SUB_SCHED_PROFILE);
}

/**
 * @brief "Add profile" tapped: add one and open it.
 * @version 1
 * @callback
 */
static void profile_add_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    char name[24];
    snprintf(name, sizeof(name), "Profile %d", s_edit.profile_count + 1);
    int i = bg_schedule_add_profile(&s_edit, name);
    if (i < 0) return;
    sched_save();
    s_profile = i;
    show_sub(SUB_SCHED_PROFILE);
}

/**
 * @brief (Re)fill the profiles list.
 * v2: colours from the theme roles (ui_theme), no literals.
 * @version 2
 * @req REQ-BG-004
 */
static void profile_list_build(void)
{
    if (!s_profile_list) return;
    lv_obj_clean(s_profile_list);
    for (int i = 0; i < s_edit.profile_count; i++) {
        lv_obj_t *btn = lv_list_add_button(s_profile_list, LV_SYMBOL_IMAGE, s_edit.profiles[i].name);
        lv_color_t sw = ui_theme_swatch(i);
        lv_obj_set_style_bg_color(btn, sw, 0);
        lv_obj_set_style_text_color(btn, ui_theme_text_on(UI_ROLE_TEXT, sw), 0);
        lv_obj_add_event_cb(btn, profile_open_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }
    if (s_edit.profile_count < BG_SCHED_PROFILES) {
        lv_obj_t *btn = lv_list_add_button(s_profile_list, LV_SYMBOL_PLUS, "Add profile");
        lv_obj_add_event_cb(btn, profile_add_cb, LV_EVENT_CLICKED, NULL);
    }
}

/**
 * @brief The profiles screen went away: forget its list.
 * @version 1
 * @callback
 */
static void profiles_delete_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    s_profile_list = nullptr;
}

/**
 * @brief Build the profiles screen: one entry per profile and "Add profile".
 * @version 1
 * @req REQ-BG-004
 * @req REQ-UI-001
 * @return The sub-screen container.
 */
lv_obj_t *create_sub_sched_profiles(lv_obj_t *parent)
{
    lv_obj_t *cont = make_sub_container(parent, "Profiles");
    lv_obj_add_event_cb(cont, profiles_delete_cb, LV_EVENT_DELETE, NULL);
    s_profile_list = lv_list_create(cont);
    lv_obj_set_size(s_profile_list, 700, 380);
    lv_obj_align(s_profile_list, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_text_font(s_profile_list, &lv_font_montserrat_24, 0);
    profile_list_build();
    return cont;
}

// ============================================================================
// Profile screen
// ============================================================================

enum { ROW_IMAGE, ROW_THEME, ROW_BRIGHTNESS, ROW_ANIM, ROW_COUNT };
static lv_obj_t *s_row_value[ROW_COUNT];   // value buttons' labels (brightness: the slider)
static lv_obj_t *s_inherit_bright;         // "Same as Settings" switch for brightness
static char      s_images[MAX_CHOICES][64];
static int       s_image_count;

/**
 * @brief Write the profile's current settings into the value labels.
 * v2: themes replace the text colour (theme names, theme roles, theme events).
 * @version 2
 * @utility
 */
static void profile_rows_refresh(void)
{
    const bg_profile_t *p = &s_edit.profiles[s_profile];
    const char *img = strrchr(p->image, '/');
    lv_label_set_text(s_row_value[ROW_IMAGE], p->image[0] ? (img ? img + 1 : p->image) : INHERIT_TEXT);
    lv_label_set_text(s_row_value[ROW_THEME], p->theme[0] ? theme_store_name(p->theme) : INHERIT_TEXT);
    lv_label_set_text(s_row_value[ROW_ANIM],
                      p->anim == BG_INHERIT ? INHERIT_TEXT : clock_anim_name((clock_anim_t)p->anim));
    bool inherit = p->brightness == BG_INHERIT;
    lv_obj_set_state(s_inherit_bright, LV_STATE_CHECKED, inherit);
    lv_obj_set_state(s_row_value[ROW_BRIGHTNESS], LV_STATE_DISABLED, inherit);
    lv_slider_set_value(s_row_value[ROW_BRIGHTNESS], inherit ? 50 : p->brightness, LV_ANIM_OFF);
}

/**
 * @brief Save and show a profile edit.
 * @version 1
 * @utility
 */
static void profile_changed(void)
{
    sched_save();
    profile_rows_refresh();
    profile_list_build();
    grid_build();
}

/**
 * @brief Image picked (0 = same as Settings).
 * @version 1
 * @callback
 */
static void image_chosen(int choice)
{
    bg_profile_t *p = &s_edit.profiles[s_profile];
    if (choice == 0) p->image[0] = '\0';
    else snprintf(p->image, sizeof(p->image), "A:/backgrounds/%s", s_images[choice - 1]);
    profile_changed();
}

/**
 * @brief List the PNG backgrounds on the SD card into s_images.
 * @version 1
 * @utility
 */
static void images_scan(void)
{
    s_image_count = 0;
    DIR *dir = opendir(IMAGE_DIR);
    if (!dir) {
        ESP_LOGW(TAG, "cannot open %s", IMAGE_DIR);
        return;
    }
    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL && s_image_count < MAX_CHOICES - 1) {
        const char *ext = strrchr(ent->d_name, '.');
        if (ent->d_type == DT_DIR || !ext || strcasecmp(ext, ".png") != 0) continue;
        snprintf(s_images[s_image_count++], sizeof(s_images[0]), "%s", ent->d_name);
    }
    closedir(dir);
}

/**
 * @brief Image row tapped: pick from the SD backgrounds.
 * @version 1
 * @callback
 */
static void image_row_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    images_scan();
    const char *labels[MAX_CHOICES];
    labels[0] = INHERIT_TEXT;
    int current = 0;
    const char *now = strrchr(s_edit.profiles[s_profile].image, '/');
    for (int i = 0; i < s_image_count; i++) {
        labels[i + 1] = s_images[i];
        if (now && strcmp(now + 1, s_images[i]) == 0) current = i + 1;
    }
    choice_popup("Background", labels, s_image_count + 1, current, image_chosen);
}

/**
 * @brief Theme picked (0 = same as Settings, else an index + 1 into the theme cache).
 * @version 1
 * @callback
 */
static void theme_chosen(int choice)
{
    bg_profile_t *p = &s_edit.profiles[s_profile];
    const theme_entry_t *t = theme_store_entry(choice - 1);
    if (choice == 0 || !t) p->theme[0] = '\0';
    else strlcpy(p->theme, t->stem, sizeof(p->theme));
    profile_changed();
}

/**
 * @brief Theme row tapped: pick an installed theme.
 * @version 1
 * @callback
 */
static void theme_row_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    const char *labels[THEME_STORE_MAX + 1];
    labels[0] = INHERIT_TEXT;
    int current = 0, n = theme_store_count();
    for (int i = 0; i < n; i++) {
        labels[i + 1] = theme_store_entry(i)->name;
        if (strcmp(theme_store_entry(i)->stem, s_edit.profiles[s_profile].theme) == 0) current = i + 1;
    }
    choice_popup("Theme", labels, n + 1, current, theme_chosen);
}

/**
 * @brief Animation picked (0 = same as Settings).
 * @version 1
 * @callback
 */
static void anim_chosen(int choice)
{
    s_edit.profiles[s_profile].anim =
        (int8_t)(choice == 0 ? BG_INHERIT : CLOCK_ANIM_MENU_ORDER[choice - 1]);
    profile_changed();
}

/**
 * @brief Animation row tapped: pick one.
 * @version 1
 * @callback
 */
static void anim_row_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    const char *labels[CLOCK_ANIM_COUNT + 1];
    labels[0] = INHERIT_TEXT;
    int current = 0;
    for (int i = 0; i < CLOCK_ANIM_COUNT; i++) {
        labels[i + 1] = clock_anim_name(CLOCK_ANIM_MENU_ORDER[i]);
        if (CLOCK_ANIM_MENU_ORDER[i] == s_edit.profiles[s_profile].anim) current = i + 1;
    }
    choice_popup("Clock animation", labels, CLOCK_ANIM_COUNT + 1, current, anim_chosen);
}

/**
 * @brief Brightness slider released: set the profile's brightness.
 * @version 1
 * @callback
 */
static void bright_slider_cb(lv_event_t *e)
{
    lv_obj_t *slider = (lv_obj_t *)lv_event_get_target(e);
    s_edit.profiles[s_profile].brightness = (int8_t)lv_slider_get_value(slider);
    profile_changed();
}

/**
 * @brief Brightness "Same as Settings" toggled.
 * @version 1
 * @callback
 */
static void bright_inherit_cb(lv_event_t *e)
{
    lv_obj_t *sw = (lv_obj_t *)lv_event_get_target(e);
    bool inherit = lv_obj_has_state(sw, LV_STATE_CHECKED);
    s_edit.profiles[s_profile].brightness =
        (int8_t)(inherit ? BG_INHERIT : lv_slider_get_value(s_row_value[ROW_BRIGHTNESS]));
    profile_changed();
}

/**
 * @brief "Delete profile" tapped: remove it and go back to the list.
 * @version 1
 * @callback
 */
static void profile_delete_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    if (!bg_schedule_remove_profile(&s_edit, s_profile)) return;
    sched_save();
    s_profile = 0;
    pop_sub();
    profile_list_build();
    grid_build();
}

/**
 * @brief One settings row: a caption and a full-width value button calling @p cb.
 * v2: colours from the theme roles (ui_theme), no literals.
 * @version 2
 * @utility
 * @return The value button's label.
 */
static lv_obj_t *profile_row(lv_obj_t *cont, int row, const char *caption, lv_event_cb_t cb)
{
    int32_t y = 66 + row * 70;
    lv_obj_t *cap = lv_label_create(cont);
    lv_label_set_text(cap, caption);
    lv_obj_set_style_text_font(cap, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(cap, ui_theme_color(UI_ROLE_TEXT), 0);
    lv_obj_align(cap, LV_ALIGN_TOP_LEFT, 0, y + 14);
    if (!cb) return cap;
    lv_obj_t *btn = lv_button_create(cont);
    lv_obj_set_size(btn, 460, 56);
    lv_obj_align(btn, LV_ALIGN_TOP_RIGHT, 0, y);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl = lv_label_create(btn);
    lv_obj_center(lbl);
    return lbl;
}

/**
 * @brief The brightness row: a slider and a "Same as Settings" switch.
 * v2: colours from the theme roles (ui_theme), no literals.
 * @version 2
 * @utility
 */
static void brightness_row(lv_obj_t *cont)
{
    profile_row(cont, ROW_BRIGHTNESS, "Brightness", NULL);
    int32_t y = 66 + ROW_BRIGHTNESS * 70;
    lv_obj_t *slider = lv_slider_create(cont);
    lv_slider_set_range(slider, 5, 100);
    lv_obj_set_width(slider, 220);
    lv_obj_align(slider, LV_ALIGN_TOP_RIGHT, -240, y + 20);
    lv_obj_add_event_cb(slider, bright_slider_cb, LV_EVENT_RELEASED, NULL);
    s_row_value[ROW_BRIGHTNESS] = slider;
    s_inherit_bright = lv_switch_create(cont);
    lv_obj_align(s_inherit_bright, LV_ALIGN_TOP_RIGHT, -150, y + 12);
    lv_obj_add_event_cb(s_inherit_bright, bright_inherit_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_t *lbl = lv_label_create(cont);
    lv_label_set_text(lbl, "Same as\nSettings");
    lv_obj_set_style_text_color(lbl, ui_theme_color(UI_ROLE_TEXT_MUTED), 0);
    lv_obj_align(lbl, LV_ALIGN_TOP_RIGHT, -40, y + 6);
}

/**
 * @brief Build the profile screen: background, theme, brightness and clock animation,
 *        each "Same as Settings" until set; "Delete profile" when it is not the last one.
 * v2: colours from the theme roles (ui_theme), no literals.
 * v3: a Theme row (was Text colour); colours from the theme roles.
 * @version 3
 * @req REQ-BG-004
 * @req REQ-UI-001
 * @return The sub-screen container.
 */
lv_obj_t *create_sub_sched_profile(lv_obj_t *parent)
{
    lv_obj_t *cont = make_sub_container(parent, s_edit.profiles[s_profile].name);
    s_row_value[ROW_IMAGE] = profile_row(cont, ROW_IMAGE, "Background", image_row_cb);
    s_row_value[ROW_THEME] = profile_row(cont, ROW_THEME, "Theme", theme_row_cb);
    brightness_row(cont);
    s_row_value[ROW_ANIM] = profile_row(cont, ROW_ANIM, "Clock animation", anim_row_cb);
    if (s_edit.profile_count > 1) {
        lv_obj_t *del = lv_button_create(cont);
        lv_obj_set_size(del, 220, 50);
        lv_obj_align(del, LV_ALIGN_BOTTOM_LEFT, 0, 0);
        lv_color_t danger = ui_theme_color(UI_ROLE_ERROR_STRONG);
        lv_obj_set_style_bg_color(del, danger, 0);
        lv_obj_set_style_text_color(del, ui_theme_text_on(UI_ROLE_TEXT, danger), 0);
        lv_obj_add_event_cb(del, profile_delete_cb, LV_EVENT_CLICKED, NULL);
        lv_obj_t *lbl = lv_label_create(del);
        lv_label_set_text(lbl, LV_SYMBOL_TRASH " Delete profile");
        lv_obj_center(lbl);
    }
    profile_rows_refresh();
    return cont;
}
