/**
 * @file panel_main.c
 * v2: the real settings UI (settings_app) over a backend; frames centred; the page's
 *     background follows the theme's screen colour.
 * @version 2
 * @brief The phone panel's WebAssembly entry point: the clock's own LVGL settings screens in
 *        a browser canvas (SDL2 under Emscripten), touch and mouse as the pointer.
 */
#include "lvgl.h"
#include "settings_app.h"
#include "settings_frame.h"
#include "settings_mock.h"
#include "ui_theme.h"
#include "ui_theme_lvgl.h"
#include "ui_theme_bind.h"

#include <emscripten.h>

#define PANEL_W 1024
#define PANEL_H 600

/**
 * @brief Paint the page around the canvas with the theme's screen colour (no colour is
 *        written in the page itself).
 * @version 1
 * @utility
 */
static void page_background(void)
{
    lv_color_t c = ui_theme_color(UI_ROLE_SCREEN);
    EM_ASM({ document.body.style.background = 'rgb(' + $0 + ',' + $1 + ',' + $2 + ')'; },
           c.red, c.green, c.blue);
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
 * @brief Entry point: LVGL on an SDL window (the page's canvas), the base theme, the
 *        settings at the menu.
 * v2: settings_app over the sample backend.
 * @version 2
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
    page_background();
    settings_frame_set_y((PANEL_H - SETTINGS_FRAME_H) / 2);   // no clock above: centred
    settings_app_open(scr, settings_mock_backend());
    emscripten_set_main_loop(frame_cb, 0, 1);
    return 0;
}
