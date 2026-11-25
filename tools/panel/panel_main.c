/**
 * @file panel_main.c
 * v2: the real settings UI (settings_app) over a backend; frames centred; the page's
 *     background follows the theme's screen colour.
 * v3: the clock's backend (settings_http) from the state the loader page fetched; the
 *     sample backend when there is none (?mock).
 * @version 3
 * @brief The phone panel's WebAssembly entry point: the clock's own LVGL settings screens in
 *        a browser canvas (SDL2 under Emscripten), touch and mouse as the pointer.
 */
#include "lvgl.h"
#include "settings_app.h"
#include "settings_frame.h"
#include "settings_mock.h"
#include "settings_http.h"
#include "ui_theme.h"
#include "ui_theme_lvgl.h"
#include "ui_theme_bind.h"

#include <emscripten.h>
#include <stdlib.h>

EM_JS_DEPS(panel, "$stringToNewUTF8");

/** The loader page's window.panelState / window.panelToken as heap strings ("" when absent). */
EM_JS(char *, js_panel_state, (void), { return stringToNewUTF8(window.panelState || ""); });
EM_JS(char *, js_panel_token, (void), { return stringToNewUTF8(window.panelToken || ""); });

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
 * @brief The backend: the clock's, from the state the loader fetched, or the sample one.
 * v2: without leave: the phone's menu has nothing to go back to, so it shows no Back.
 * @version 2
 * @utility
 * @return It (static).
 */
static const settings_backend_t *pick_backend(void)
{
    static settings_backend_t be;
    char *state = js_panel_state(), *token = js_panel_token();
    const settings_backend_t *src = state[0] ? settings_http_backend(state, token) : NULL;
    free(state);
    free(token);
    be = *(src ? src : settings_mock_backend());
    be.leave = NULL;
    return &be;
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
 * v3: over the clock's backend when the loader fetched its state.
 * @version 3
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
    settings_app_open(scr, pick_backend());
    emscripten_set_main_loop(frame_cb, 0, 1);
    return 0;
}
