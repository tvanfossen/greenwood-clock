/**
 * @file panel_main.c
 * @version 1
 * @brief The phone panel's WebAssembly entry point (proof stage): the clock's own LVGL
 *        settings screens in a browser canvas (SDL2 under Emscripten), touch and mouse as the
 *        pointer. Shows the settings menu; its Schedule entry opens the schedule list.
 *        Sample data until the HTTP backend exists.
 */
#include "lvgl.h"
#include "settings_frame.h"
#include "sched_view.h"
#include "ui_theme.h"
#include "ui_theme_lvgl.h"
#include "ui_theme_bind.h"

#include <emscripten.h>
#include <stdio.h>
#include <string.h>

#define PANEL_W 1024
#define PANEL_H 600

static lv_obj_t *s_frame;

/**
 * @brief A sample profile: every day from @p a to @p b.
 * @version 1
 * @utility
 * @return The profile.
 */
static sched_rule_t sample_daily(const char *name, int a, int b, const char *image)
{
    sched_rule_t r = { .repeat = SCHED_EVERY_DAY, .start = { SCHED_AT_CLOCK, (int16_t)a },
                       .end = { SCHED_AT_CLOCK, (int16_t)b }, .brightness = SCHED_INHERIT };
    snprintf(r.name, sizeof(r.name), "%s", name);
    snprintf(r.image, sizeof(r.image), "%s", image);
    snprintf(r.theme, sizeof(r.theme), "default");
    return r;
}

static void back_cb(lv_event_t *e);

/**
 * @brief Menu entry: open the schedule list (the only screen wired in the proof).
 * @version 1
 * @callback
 */
static void schedule_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    sched_rule_t r[4] = {
        sample_daily("Morning", 7 * 60, 12 * 60, "A:/backgrounds/winter_morning.png"),
        sample_daily("Afternoon", 12 * 60, 17 * 60, "A:/backgrounds/winter_afternoon.png"),
        sample_daily("Evening", 17 * 60, 22 * 60, ""),
        sample_daily("Night", 22 * 60, 7 * 60, "A:/backgrounds/night_winter.png"),
    };
    sched_row_state_t st[4];
    memset(st, 0, sizeof(st));
    st[1].active = true;
    lv_obj_delete(s_frame);
    s_frame = sched_list_create(lv_screen_active(), r, st, 4, back_cb);
}

/**
 * @brief A menu entry with nothing behind it yet.
 * @version 1
 * @callback
 */
static void noop_cb(lv_event_t *e)
{
    LV_UNUSED(e);
}

/**
 * @brief Show the settings menu.
 * @version 1
 * @utility
 */
static void show_menu(void)
{
    static const settings_menu_item_t ITEMS[] = {
        { LV_SYMBOL_IMAGE, "Brightness", noop_cb, NULL },
        { LV_SYMBOL_IMAGE, "Background", noop_cb, NULL },
        { LV_SYMBOL_LIST, "Schedule", schedule_cb, NULL },
        { LV_SYMBOL_EDIT, "Theme", noop_cb, NULL },
        { LV_SYMBOL_LOOP, "Clock Animation", noop_cb, NULL },
        { LV_SYMBOL_SETTINGS, "About", noop_cb, NULL },
    };
    s_frame = settings_frame_create(lv_screen_active(), "Settings", noop_cb);
    settings_menu_grid(s_frame, ITEMS, 6);
}

/**
 * @brief Back from a sub-screen: show the menu again.
 * @version 1
 * @callback
 */
static void back_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    lv_obj_delete(s_frame);
    show_menu();
}

/**
 * @brief One browser frame: run LVGL's timers.
 * @version 1
 * @callback
 */
static void frame_cb(void)
{
    lv_timer_handler();
}

/**
 * @brief Entry point: LVGL on an SDL window (the page's canvas), the base theme, the menu.
 * @version 1
 * @utility
 * @return 0.
 */
int main(void)
{
    lv_init();
    lv_display_t *disp = lv_sdl_window_create(PANEL_W, PANEL_H);
    lv_sdl_mouse_create();
    ui_theme_set(NULL);
    ui_theme_lvgl_init(disp);
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    ui_bind_fill_role(scr, UI_ROLE_SCREEN);
    show_menu();
    emscripten_set_main_loop(frame_cb, 0, 1);
    return 0;
}
