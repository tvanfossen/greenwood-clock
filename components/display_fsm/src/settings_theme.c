/**
 * @file settings_theme.c
 * v2: settings_theme_edit: an existing theme file (the Library's Edit).
 * @version 2
 * @brief Customize a theme (see settings_theme.h): editor, name, save as a theme file.
 */
#include "settings_theme.h"
#include "settings_frame.h"
#include "sched_rule.h"
#include "theme_editor.h"
#include "ui_theme_bind.h"
#include "ui_theme_lvgl.h"

#include <stdio.h>
#include <string.h>

/** The customization in progress. */
typedef struct {
    settings_pick_cb_t cb;
    void              *ctx;
    ui_theme_t         theme;   /**< The edited theme, kept when the editor saves. */
    bool               saved;   /**< The editor saved: ask its name once it has closed. */
    char               stem[SCHED_THEME_LEN];
    bool               keep;    /**< Editing a theme file: save under its stem and name. */
    ui_theme_t         prev;    /**< What showed before (restored on close when keep). */
} customize_t;

static customize_t s_cz;

/**
 * @brief The theme file was written (or not): hand its stem to the caller, or say why not.
 * @version 1
 * @callback
 */
static void written_cb(bool ok, const char *why, void *ctx)
{
    LV_UNUSED(ctx);
    if (!ok) {
        settings_app_toast(why && why[0] ? why : "Could not save the theme", false);
        return;
    }
    settings_app_toast("Theme saved", false);
    if (s_cz.cb) s_cz.cb(s_cz.stem, s_cz.ctx);
}

/**
 * @brief A name typed: keep the theme under its stem.
 * @version 1
 * @callback
 */
static void name_cb(const char *value, void *ctx)
{
    LV_UNUSED(ctx);
    if (!sched_theme_stem(value, s_cz.stem, sizeof(s_cz.stem))) {
        settings_app_toast("Name it with letters or digits (not \"Default\")", false);
        return;
    }
    snprintf(s_cz.theme.name, sizeof(s_cz.theme.name), "%s", value);
    settings_app_backend()->save_theme(&s_cz.theme, s_cz.stem, written_cb, NULL);
}

/**
 * @brief The editor's Save: keep the theme; its name is asked once the editor has closed.
 * v2: an edited theme file keeps its name.
 * @version 2
 * @callback
 */
static void editor_saved_cb(const ui_theme_t *t, bool for_profile)
{
    LV_UNUSED(for_profile);   // no where-to-save row: the caller's profile gets it
    char name[sizeof(s_cz.theme.name)];
    snprintf(name, sizeof(name), "%s", s_cz.theme.name);
    s_cz.theme = *t;
    if (s_cz.keep) snprintf(s_cz.theme.name, sizeof(s_cz.theme.name), "%s", name);   // its own name
    s_cz.saved = true;
}

/**
 * @brief The editor closed (after Save, or Back): close its screen, then ask the name.
 * v2: an edited theme file is kept under its stem, and what showed before comes back.
 * @version 2
 * @callback
 */
static void editor_closed_cb(void)
{
    settings_app_pop();
    if (s_cz.keep) {   // the theme that showed before comes back
        ui_theme_set(&s_cz.prev);
        ui_theme_lvgl_refresh();
        ui_bind_refresh_all();
    }
    if (s_cz.saved && s_cz.keep) settings_app_backend()->save_theme(&s_cz.theme, s_cz.stem, written_cb, NULL);
    else if (s_cz.saved) settings_app_enter_text("Theme name", "", name_cb, NULL);
    s_cz.saved = false;
}

/**
 * @brief The theme editor, full width.
 * @version 1
 * @utility
 * @return Its container.
 */
static lv_obj_t *editor_screen(lv_obj_t *parent, void *ctx)
{
    LV_UNUSED(ctx);
    lv_obj_t *cont = lv_obj_create(parent);
    lv_obj_remove_style_all(cont);
    lv_obj_set_size(cont, lv_pct(100), lv_pct(100));
    lv_obj_remove_flag(cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_update_layout(cont);
    theme_editor_create(cont, settings_frame_get_y(), "", editor_saved_cb, editor_closed_cb);
    return cont;
}

/**
 * @brief Open the editor for a theme that @p cb will get the stem of.
 * @version 1
 * @req REQ-UI-004
 */
void settings_theme_customize(settings_pick_cb_t cb, void *ctx)
{
    s_cz = (customize_t){ .cb = cb, .ctx = ctx };
    settings_app_push(editor_screen, NULL);
}

/**
 * @brief Open the editor on a theme file.
 * @version 1
 * @req REQ-LIB-001
 */
void settings_theme_edit(const ui_theme_t *start, const char *stem, settings_pick_cb_t cb, void *ctx)
{
    s_cz = (customize_t){ .cb = cb, .ctx = ctx, .theme = *start, .keep = true, .prev = *ui_theme_get() };
    snprintf(s_cz.stem, sizeof(s_cz.stem), "%s", stem);
    ui_theme_set(start);   // the editor starts from the current theme
    ui_theme_lvgl_refresh();
    ui_bind_refresh_all();
    settings_app_push(editor_screen, NULL);
}
