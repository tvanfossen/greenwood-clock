/**
 * @file panel_main.c
 * v2: the real settings UI (settings_app) over a backend; frames centred; the page's
 *     background follows the theme's screen colour.
 * v3: the clock's backend (settings_http) from the state the loader page fetched; the
 *     sample backend when there is none (?mock).
 * v4: the display is the browser's viewport (any phone, upright or sideways), laid out again
 *     when it changes (panel_resize).
 * @version 4
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

/** The browser's viewport, in CSS pixels (LVGL draws one pixel per CSS pixel). */
EM_JS(int, js_view_w, (void), { return window.innerWidth; });
EM_JS(int, js_view_h, (void), { return window.innerHeight; });

#define PANEL_MARGIN 8   // around the frames when the screen is smaller than the clock's

static lv_display_t *s_disp;

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
 * @brief Where the frames go on a @p w x @p h screen: the clock's size centred where it fits;
 *        a narrow screen gets the whole height (a phone held upright).
 * @version 1
 * @utility
 */
static void frame_area(int32_t w, int32_t h)
{
    int32_t fw = LV_MIN(SETTINGS_FRAME_W, w - 2 * PANEL_MARGIN);
    int32_t fh = LV_MIN(SETTINGS_FRAME_H, h - 2 * PANEL_MARGIN);
    if (fw < SETTINGS_NARROW_W) fh = h - 2 * PANEL_MARGIN;
    settings_frame_set_area((h - fh) / 2, fw, fh);
}

/**
 * @brief The viewport changed (the phone turned): resize the display and lay the open screens
 *        out again for it. Called by the page.
 * @version 1
 * @utility
 */
EMSCRIPTEN_KEEPALIVE void panel_resize(int w, int h)
{
    if (!s_disp || (w == lv_display_get_horizontal_resolution(s_disp) &&
                    h == lv_display_get_vertical_resolution(s_disp))) {
        return;
    }
    lv_display_set_resolution(s_disp, w, h);
    frame_area(w, h);
    settings_app_relayout();
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
 * v4: the viewport's size.
 * @version 4
 * @utility
 * @return 0.
 */
int main(void)
{
    lv_init();
    int32_t w = js_view_w(), h = js_view_h();
    s_disp = lv_sdl_window_create(w, h);
    lv_sdl_mouse_create();
    ui_theme_set(NULL);
    ui_theme_lvgl_init(s_disp);
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    ui_bind_fill_role(scr, UI_ROLE_SCREEN);
    page_background();
    frame_area(w, h);   // no clock above: centred
    settings_app_open(scr, pick_backend());
    emscripten_set_main_loop(frame_cb, 0, 1);
    return 0;
}
