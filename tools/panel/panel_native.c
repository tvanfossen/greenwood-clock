/**
 * @file panel_native.c
 * @version 1
 * @brief The phone panel's native helpers (see panel_native.h).
 */
#include "panel_native.h"
#include "display_widgets.h"
#include "settings_app.h"
#include "ui_theme.h"

#include <emscripten.h>
#include <string.h>
#include <time.h>

#define PIC_W 1024
#define PIC_H 600

/** Set CSS variable @p name to rgb(@p r, @p g, @p b) on the page. */
EM_JS(void, js_css_var, (const char *name, int r, int g, int b), {
    document.documentElement.style.setProperty(UTF8ToString(name), 'rgb(' + r + ',' + g + ',' + b + ')');
});

/** Open the page's text overlay (panel_ui.js). */
EM_JS(void, js_edit_text, (const char *title, const char *initial, int max), {
    window.panelText(UTF8ToString(title), UTF8ToString(initial), max);
});

/**
 * @brief Publish the theme as CSS variables.
 * @version 1
 * @utility
 */
void panel_native_theme_css(void)
{
    static const struct {
        const char *name;
        ui_role_t   role;
    } VARS[] = {
        { "--screen", UI_ROLE_SCREEN },       { "--surface", UI_ROLE_SURFACE },
        { "--raised", UI_ROLE_SURFACE_RAISED }, { "--inset", UI_ROLE_SURFACE_INSET },
        { "--border", UI_ROLE_BORDER },       { "--text", UI_ROLE_TEXT },
        { "--muted", UI_ROLE_TEXT_MUTED },    { "--faint", UI_ROLE_TEXT_FAINT },
        { "--accent", UI_ROLE_ACCENT },
    };
    for (size_t i = 0; i < sizeof(VARS) / sizeof(VARS[0]); i++) {
        lv_color_t c = ui_theme_color(VARS[i].role);
        js_css_var(VARS[i].name, c.red, c.green, c.blue);
    }
}

// ---------------------------------------------------------------------------
// Text with the phone's keyboard
// ---------------------------------------------------------------------------

static struct {
    void (*done)(const char *text, void *ctx);
    void *ctx;
} s_text;

/**
 * @brief Open the page's text overlay.
 * @version 1
 * @utility
 */
void panel_native_edit_text(const char *title, const char *initial, int max,
                            void (*done)(const char *text, void *ctx), void *ctx)
{
    s_text.done = done;
    s_text.ctx = ctx;
    js_edit_text(title, initial, max);
}

/**
 * @brief The overlay closed: @p ok with @p text (Done), or cancelled. Called by the page.
 * @version 1
 * @utility
 */
EMSCRIPTEN_KEEPALIVE void panel_text_done(int ok, const char *text)
{
    void (*done)(const char *, void *) = s_text.done;
    s_text.done = NULL;
    if (ok && done && text && text[0]) done(text, s_text.ctx);
}

// ---------------------------------------------------------------------------
// The real clock over a picture being cropped
// ---------------------------------------------------------------------------

static uint8_t s_picture[PIC_W * PIC_H * 4];   // RGBA from the page: the crop as it stands
static uint8_t s_bgra[PIC_W * PIC_H * 4];      // the same as an LVGL image (BGRA)
static uint8_t s_guide[PIC_W * PIC_H * 4];     // RGBA to the page: the clock alone
static clock_widget_t *s_clock;
static lv_obj_t       *s_holder;               // the clock's off-screen stage

/**
 * @brief Where the page writes the picture (PIC_W x PIC_H RGBA).
 * @version 1
 * @utility
 * @return The buffer.
 */
EMSCRIPTEN_KEEPALIVE uint8_t *panel_crop_picture(void)
{
    return s_picture;
}

/**
 * @brief The clock's stage off the screen, and the clock on it at the time now.
 * @version 1
 * @utility
 */
static void stage(void)
{
    if (!s_holder) {
        s_holder = lv_obj_create(lv_screen_active());
        lv_obj_remove_style_all(s_holder);
        lv_obj_set_size(s_holder, PIC_W, PIC_H);
        lv_obj_set_pos(s_holder, -2 * PIC_W, 0);   // never on the panel's screen
        s_clock = clock_widget_create(s_holder);
    }
    sched_date_t day;
    int min;
    settings_app_backend()->now(&day, &min);
    struct tm t = { .tm_year = day.year - 1900, .tm_mon = day.month - 1, .tm_mday = day.day,
                    .tm_hour = min / 60, .tm_min = min % 60, .tm_isdst = -1 };
    mktime(&t);   // the weekday
    clock_widget_update(s_clock, &t);
}

/**
 * @brief The clock as it would show over the picture: full size for a background (@p photo 0),
 *        minimised for an album photo, in the colour the clock would pick over it. Called by
 *        the page after it writes the picture.
 * @version 1
 * @utility
 * @return The clock alone (PIC_W x PIC_H RGBA, transparent around it), or NULL.
 */
EMSCRIPTEN_KEEPALIVE uint8_t *panel_crop_overlay(int photo)
{
    for (size_t i = 0; i < sizeof(s_picture); i += 4) {   // RGBA -> BGRA
        s_bgra[i] = s_picture[i + 2];
        s_bgra[i + 1] = s_picture[i + 1];
        s_bgra[i + 2] = s_picture[i];
        s_bgra[i + 3] = 255;
    }
    lv_image_dsc_t pic = { .header = { .magic = LV_IMAGE_HEADER_MAGIC, .cf = LV_COLOR_FORMAT_ARGB8888,
                                       .w = PIC_W, .h = PIC_H, .stride = PIC_W * 4 },
                           .data_size = sizeof(s_bgra), .data = s_bgra };
    stage();
    lv_color_t scr = ui_theme_color(UI_ROLE_SCREEN);
    lv_area_t a;
    clock_widget_set_user_color(s_clock, ui_oklch_to_color(ui_theme_get()->ink));
    clock_widget_full_area(&a);
    clock_widget_set_color(s_clock, ui_theme_ink_on_picture(&pic, &a, scr));
    clock_widget_min_area(&a);
    clock_widget_set_minimized_base_color(s_clock, ui_theme_ink_on_picture(&pic, &a, scr));
    clock_widget_snap_mode(s_clock, photo ? CLOCK_MODE_MINIMIZED : CLOCK_MODE_FULL);
    lv_obj_update_layout(s_holder);
    lv_draw_buf_t *snap = lv_snapshot_take(s_holder, LV_COLOR_FORMAT_ARGB8888);
    if (!snap) return NULL;
    for (int y = 0; y < PIC_H && y < (int)snap->header.h; y++) {   // BGRA -> RGBA, by row
        const uint8_t *row = snap->data + (size_t)y * snap->header.stride;
        uint8_t *out = s_guide + (size_t)y * PIC_W * 4;
        for (int x = 0; x < PIC_W && x < (int)snap->header.w; x++) {
            out[x * 4] = row[x * 4 + 2];
            out[x * 4 + 1] = row[x * 4 + 1];
            out[x * 4 + 2] = row[x * 4];
            out[x * 4 + 3] = row[x * 4 + 3];
        }
    }
    lv_draw_buf_destroy(snap);
    return s_guide;
}
