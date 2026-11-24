/**
 * @file preview.c
 * @version 1
 * @brief Host theme preview: renders the firmware's own widgets with the real LVGL fork (built
 *        from the device sdkconfig) and the real theme code, one PNG-ready frame per theme and
 *        scene, so a theme can be judged before anything goes to the device.
 *
 * Usage: theme_preview <out_dir> <background LVGL path, e.g. A:/backgrounds/x.png | -> <theme.json>...
 * Writes <out_dir>/<theme-stem>_<scene>.ppm (RGB888) for each theme and scene:
 *   weather  — the weather screen: card, forecast strip and a Severe alert over the background
 *   gallery  — every role: surfaces and text tiers, stock widgets, the alert severities, the
 *              accent palette (leaves, swatches)
 * `inv theme-preview` builds this and converts the frames to PNG.
 */

#include "lvgl.h"
#include "ui_theme.h"
#include "ui_theme_lvgl.h"
#include "display_widgets.h"
#include "info_panel.h"
#include "nws.h"
#include "src/libs/lodepng/lodepng.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define SCREEN_W 1024
#define SCREEN_H 600

static uint16_t       s_fb[SCREEN_W * SCREEN_H];   // RGB565, what the panel would show
static lv_image_dsc_t s_bg;                        // decoded background (RGB565), if any
static bool           s_have_bg = false;

// ---------------------------------------------------------------------------
// Platform seams the widgets call (the device implements them in display_fsm / image_decode)
// ---------------------------------------------------------------------------

/**
 * @brief The colour behind text on @p panel: the same computation the device uses.
 * @version 1
 * @utility
 * @return The backdrop colour.
 */
lv_color_t display_fsm_panel_backdrop(lv_obj_t *panel)
{
    return ui_theme_panel_backdrop(panel, s_have_bg ? &s_bg : NULL);
}

/**
 * @brief No Lottie on the host (ThorVG is the device's C++ backend).
 * @version 1
 * @utility
 * @return false.
 */
bool display_fsm_load_lottie(const lottie_load_job_t *job)
{
    (void)job;
    return false;
}

/**
 * @brief No forecast icon assets on the host: the strip falls back to its text.
 * @version 1
 * @utility
 * @return NULL.
 */
lv_image_dsc_t *image_decode_png_file(const char *path)
{
    (void)path;
    return NULL;
}

// ---------------------------------------------------------------------------
// Display, background, output
// ---------------------------------------------------------------------------

/**
 * @brief Flush callback: copy the rendered area into the RGB565 frame.
 * @version 1
 * @callback
 */
static void flush_cb(lv_display_t *disp, const lv_area_t *a, uint8_t *px)
{
    int32_t w = lv_area_get_width(a);
    for (int32_t y = a->y1; y <= a->y2; y++) {
        memcpy(&s_fb[y * SCREEN_W + a->x1], px + (size_t)(y - a->y1) * w * 2, (size_t)w * 2);
    }
    lv_display_flush_ready(disp);
}

/**
 * @brief Tick source for LVGL (monotonic milliseconds).
 * @version 1
 * @callback
 * @return Milliseconds.
 */
static uint32_t tick_cb(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

/**
 * @brief Decode @p path (an LVGL "A:/..." path, the device's) into s_bg as RGB565, the
 *        device's background format.
 * @version 1
 * @utility
 * @return Whether it decoded.
 */
static bool load_background(const char *path)
{
    unsigned char *out = NULL;
    unsigned w = 0, h = 0;
    if (strcmp(path, "-") == 0 || lodepng_decode32_file(&out, &w, &h, path) != 0) {
        if (strcmp(path, "-") != 0) fprintf(stderr, "background %s: decode failed\n", path);
        return false;
    }
    // LVGL's lodepng hands back an lv_draw_buf_t, not raw pixels; its bytes are R,G,B,A
    // (checked against the PNG: reading them as B,G,R swapped red and blue).
    lv_draw_buf_t *db = (lv_draw_buf_t *)out;
    uint16_t *px = malloc((size_t)w * h * 2);
    for (unsigned y = 0; y < h; y++) {
        const uint8_t *row = db->data + (size_t)y * db->header.stride;
        for (unsigned x = 0; x < w; x++) {
            const uint8_t *p = &row[x * 4];
            px[y * w + x] = (uint16_t)(((p[0] & 0xF8) << 8) | ((p[1] & 0xFC) << 3) | (p[2] >> 3));
        }
    }
    lv_draw_buf_destroy(db);
    memset(&s_bg, 0, sizeof(s_bg));
    s_bg.header.magic = LV_IMAGE_HEADER_MAGIC;
    s_bg.header.cf = LV_COLOR_FORMAT_RGB565;
    s_bg.header.w = w;
    s_bg.header.h = h;
    s_bg.header.stride = w * 2;
    s_bg.data = (const uint8_t *)px;
    s_bg.data_size = w * h * 2;
    return true;
}

/**
 * @brief Render the active screen and write the frame to @p path as binary PPM (RGB888).
 * @version 1
 * @utility
 */
static void write_frame(const char *path)
{
    lv_obj_invalidate(lv_screen_active());
    lv_refr_now(NULL);
    FILE *f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "cannot write %s\n", path);
        return;
    }
    fprintf(f, "P6\n%d %d\n255\n", SCREEN_W, SCREEN_H);
    for (int i = 0; i < SCREEN_W * SCREEN_H; i++) {
        uint16_t p = s_fb[i];
        unsigned char rgb[3] = { (unsigned char)(((p >> 11) & 0x1F) * 255 / 31),
                                 (unsigned char)(((p >> 5) & 0x3F) * 255 / 63),
                                 (unsigned char)((p & 0x1F) * 255 / 31) };
        fwrite(rgb, 1, 3, f);
    }
    fclose(f);
}

/**
 * @brief Start a fresh screen: SCREEN fill and the background picture, like the clock.
 * @version 1
 * @utility
 * @return The screen.
 */
static lv_obj_t *fresh_screen(bool with_bg)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, ui_theme_color(UI_ROLE_SCREEN), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    if (with_bg && s_have_bg) {
        lv_obj_t *img = lv_image_create(scr);
        lv_image_set_src(img, &s_bg);
        lv_obj_set_pos(img, 0, 0);
    }
    lv_screen_load(scr);
    return scr;
}

// ---------------------------------------------------------------------------
// Scenes
// ---------------------------------------------------------------------------

/**
 * @brief Fill @p c and @p fc with plausible weather (no network on the host).
 * @version 1
 * @utility
 */
static void fake_weather(nws_conditions_t *c, nws_forecast_t *fc)
{
    static const char *const DAYS[7] = { "Tue", "Wed", "Thu", "Fri", "Sat", "Sun", "Mon" };
    static const char *const SKY[7] = { "Partly Cloudy", "Cloudy", "Rain", "Rain", "Snow",
                                        "Cloudy", "Snow" };
    memset(c, 0, sizeof(*c));
    c->temp_c = 0.0f;
    c->feels_like_c = -3.0f;
    c->humidity = 64;
    c->wind_speed_kmh = 11.0f;
    strcpy(c->wind_dir_cardinal, "SE");
    strcpy(c->description, "Partly Cloudy");
    c->valid = true;
    memset(fc, 0, sizeof(*fc));
    for (int i = 0; i < 14; i++) {
        nws_forecast_period_t *p = &fc->periods[i];
        snprintf(p->name, sizeof(p->name), "%s%s", DAYS[i / 2], i % 2 ? " Night" : "");
        strcpy(p->short_forecast, SKY[i / 2]);
        p->temperature = i % 2 ? 22 + i : 39 + i;
        p->temp_unit = 'F';
        p->is_daytime = (i % 2) == 0;
    }
    fc->period_count = 14;
    fc->valid = true;
}

/**
 * @brief The weather screen: card, forecast strip and a Severe alert over the background.
 * @version 1
 * @utility
 */
static void scene_weather(void)
{
    lv_obj_t *scr = fresh_screen(true);
    nws_conditions_t c;
    nws_forecast_t fc;
    fake_weather(&c, &fc);
    weather_card_t *card = weather_card_create(scr);
    weather_card_update(card, &c);
    forecast_strip_t *strip = forecast_strip_create(scr);
    forecast_strip_update(strip, &fc);
    weather_card_set_contrast(card);
    forecast_strip_set_contrast(strip);
    alert_banner_t *ab = alert_banner_create(scr);
    nws_alert_t a;
    memset(&a, 0, sizeof(a));
    strcpy(a.severity, "Severe");
    strcpy(a.headline, "Winter Storm Warning until 7 PM: heavy snow, 6 to 10 inches");
    alert_banner_show(ab, &a);
}

/**
 * @brief One labelled row of text tiers on a panel of role @p fill.
 * @version 1
 * @utility
 */
static void tier_panel(lv_obj_t *scr, ui_role_t fill, int32_t x, int32_t y)
{
    lv_obj_t *p = lv_obj_create(scr);
    lv_obj_set_size(p, 300, 150);
    lv_obj_set_pos(p, x, y);
    lv_obj_set_style_bg_color(p, ui_theme_color(fill), 0);
    lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
    lv_obj_remove_flag(p, LV_OBJ_FLAG_SCROLLABLE);
    lv_color_t bd = ui_theme_color(fill);
    static const ui_role_t TIERS[] = { UI_ROLE_TEXT, UI_ROLE_TEXT_MUTED, UI_ROLE_TEXT_FAINT,
                                       UI_ROLE_ACCENT };
    for (int i = 0; i < 4; i++) {
        lv_obj_t *l = lv_label_create(p);
        lv_label_set_text_fmt(l, "%s on %s", ui_theme_role_name(TIERS[i]),
                              ui_theme_role_name(fill));
        lv_obj_set_style_text_color(l, ui_theme_text_on(TIERS[i], bd), 0);
        lv_obj_set_style_text_font(l, &lv_font_montserrat_20, 0);
        lv_obj_set_pos(l, 0, i * 30);
    }
}

/**
 * @brief Stock LVGL widgets, styled only by the themed LVGL theme.
 * @version 1
 * @utility
 */
static void stock_widgets(lv_obj_t *scr)
{
    lv_obj_t *btn = lv_button_create(scr);
    lv_obj_set_pos(btn, 660, 20);
    lv_label_set_text(lv_label_create(btn), "Button");
    lv_obj_t *chk = lv_button_create(scr);
    lv_obj_set_pos(chk, 800, 20);
    lv_obj_add_state(chk, LV_STATE_CHECKED);
    lv_label_set_text(lv_label_create(chk), "Checked");
    lv_obj_t *sl = lv_slider_create(scr);
    lv_obj_set_width(sl, 300);
    lv_obj_set_pos(sl, 670, 100);
    lv_slider_set_value(sl, 60, LV_ANIM_OFF);
    lv_obj_t *sw = lv_switch_create(scr);
    lv_obj_set_pos(sw, 670, 140);
    lv_obj_add_state(sw, LV_STATE_CHECKED);
    lv_obj_t *sw2 = lv_switch_create(scr);
    lv_obj_set_pos(sw2, 760, 140);
    lv_obj_t *list = lv_list_create(scr);
    lv_obj_set_size(list, 330, 190);
    lv_obj_set_pos(list, 660, 190);
    lv_list_add_text(list, "Settings");
    lv_list_add_button(list, LV_SYMBOL_WIFI, "WiFi Settings");
    lv_list_add_button(list, LV_SYMBOL_EDIT, "Theme");
    lv_obj_add_state(lv_list_add_button(list, LV_SYMBOL_IMAGE, "Background"), LV_STATE_PRESSED);
}

/**
 * @brief The four alert severities as banners, stacked.
 * @version 1
 * @utility
 */
static void alert_stack(lv_obj_t *scr)
{
    static const char *const SEV[4] = { "Extreme", "Severe", "Moderate", "Minor" };
    for (int i = 0; i < 4; i++) {
        lv_obj_t *holder = lv_obj_create(scr);
        lv_obj_remove_style_all(holder);
        lv_obj_set_size(holder, SCREEN_W, ALERT_BANNER_HEIGHT);
        lv_obj_set_pos(holder, 0, 400 + i * 50);
        alert_banner_t *ab = alert_banner_create(holder);
        nws_alert_t a;
        memset(&a, 0, sizeof(a));
        strcpy(a.severity, SEV[i]);
        snprintf(a.headline, sizeof(a.headline), "%s alert: example headline text", SEV[i]);
        alert_banner_show(ab, &a);
    }
}

/**
 * @brief Swatches of the accent palette: leaves and profile swatches.
 * @version 1
 * @utility
 */
static void palette_row(lv_obj_t *scr)
{
    for (int i = 0; i < UI_THEME_LEAF_COUNT + UI_THEME_SWATCH_COUNT; i++) {
        lv_obj_t *s = lv_obj_create(scr);
        lv_obj_remove_style_all(s);
        lv_obj_set_size(s, 40, 40);
        lv_obj_set_pos(s, 20 + i * 48, 340);
        lv_obj_set_style_bg_opa(s, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(s, i < UI_THEME_LEAF_COUNT ? 20 : 6, 0);
        lv_obj_set_style_bg_color(s, i < UI_THEME_LEAF_COUNT
                                         ? ui_theme_leaf(i)
                                         : ui_theme_swatch(i - UI_THEME_LEAF_COUNT), 0);
    }
}

/**
 * @brief The gallery: every role, stock widgets, alerts and palettes.
 * @version 1
 * @utility
 */
static void scene_gallery(void)
{
    lv_obj_t *scr = fresh_screen(false);
    tier_panel(scr, UI_ROLE_SURFACE, 20, 20);
    tier_panel(scr, UI_ROLE_SURFACE_RAISED, 330, 20);
    tier_panel(scr, UI_ROLE_SURFACE_INSET, 20, 180);
    tier_panel(scr, UI_ROLE_BORDER, 330, 180);
    stock_widgets(scr);
    palette_row(scr);
    alert_stack(scr);
}

/**
 * @brief The file stem of @p path ("sdcard/themes/sassafras.json" -> "sassafras").
 * @version 1
 * @utility
 */
static void stem_of(const char *path, char *out, size_t n)
{
    const char *b = strrchr(path, '/');
    b = b ? b + 1 : path;
    snprintf(out, n, "%s", b);
    char *dot = strrchr(out, '.');
    if (dot) *dot = '\0';
}

/**
 * @brief Read a whole file.
 * @version 1
 * @utility
 * @return Heap text, or NULL.
 */
static char *read_text(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = calloc(1, (size_t)n + 1);
    if (fread(buf, 1, (size_t)n, f) != (size_t)n) buf[0] = '\0';
    fclose(f);
    return buf;
}

/**
 * @brief Render every scene for the theme in @p path into @p out_dir.
 * @version 1
 * @utility
 * @return Whether the theme parsed.
 */
static bool render_theme(const char *out_dir, const char *path)
{
    char *json = read_text(path);
    ui_theme_t t;
    bool ok = json && ui_theme_parse(json, &t);
    free(json);
    if (!ok) {
        fprintf(stderr, "skip %s: not a theme file\n", path);
        return false;
    }
    ui_theme_set(&t);
    ui_theme_lvgl_init(lv_display_get_default());
    char stem[64], file[512];
    stem_of(path, stem, sizeof(stem));
    scene_weather();
    snprintf(file, sizeof(file), "%s/%s_weather.ppm", out_dir, stem);
    write_frame(file);
    scene_gallery();
    snprintf(file, sizeof(file), "%s/%s_gallery.ppm", out_dir, stem);
    write_frame(file);
    printf("%s: %s\n", stem, t.name);
    return true;
}

/**
 * @brief Entry point: set up LVGL with an RGB565 display, then render each theme.
 * @version 1
 * @utility
 * @return 0 when every theme rendered.
 */
int main(int argc, char **argv)
{
    if (argc < 4) {
        fprintf(stderr, "usage: %s <out_dir> <A:/background.png|-> <theme.json>...\n", argv[0]);
        return 2;
    }
    lv_init();
    lv_tick_set_cb(tick_cb);
    lv_display_t *disp = lv_display_create(SCREEN_W, SCREEN_H);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
    static uint8_t buf[SCREEN_W * 100 * 2];
    lv_display_set_buffers(disp, buf, NULL, sizeof(buf), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(disp, flush_cb);
    s_have_bg = load_background(argv[2]);
    int failed = 0;
    for (int i = 3; i < argc; i++) failed += render_theme(argv[1], argv[i]) ? 0 : 1;
    return failed;
}
