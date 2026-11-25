/**
 * @file preview.c
 * v2: theme bindings; a live-switch scene.
 * v3: the home clock scene.
 * v4: schedule screens (list, a daily profile, a conflicting one).
 * v5: --decisions: the open clock-text choices rendered as options side by side.
 * v6: the upload screen (waiting with the QR code; a phone connected).
 * v7: the --decisions mode is gone (all decided).
 * v8: the settings scene is the real settings app (sample backend); the schedule scenes are
 *     gone (inv panel-shots clicks through them with the same code).
 * v9: the Phone screen replaces the upload mock.
 * @version 9
 * @brief Host theme preview: renders the firmware's own widgets with the real LVGL fork (built
 *        from the device sdkconfig) and the real theme code, one PNG-ready frame per theme and
 *        scene, so a theme can be judged before anything goes to the device.
 *
 * Usage: theme_preview <out_dir> <background LVGL path, e.g. A:/backgrounds/x.png | -> <theme.json>...
 * Writes <out_dir>/<theme-stem>_<scene>.ppm (RGB888) for each theme and scene:
 *   clock    — the home screen: the full-size clock over the background
 *   phone    — Settings > Phone: the QR code that pairs the phone panel
 *   weather  — the weather screen: card, forecast strip and a Severe alert over the background
 *   gallery  — every role: surfaces and text tiers, stock widgets, the alert severities, the
 *              accent palette (leaves, swatches)
 *   live     — the weather screen built under the FIRST theme, then switched to this one in
 *              place (no rebuild): what a theme change on the device repaints
 *   editor_<tab> — the on-screen theme editor on each tab, over the background
 *   settings — the settings menu under the real top-bar clock
 *   alert    — the alert details card (first of three alerts) over the weather screen
 *   editor_edit  — FIRST theme only: the editor after moving the accent hue to 200 (teal)
 * `inv theme-preview` builds this and converts the frames to PNG.
 */

#include "lvgl.h"
#include "ui_theme.h"
#include "ui_theme_lvgl.h"
#include "ui_theme_bind.h"
#include "display_widgets.h"
#include "display_fsm.h"   // lottie_load_job_t (the seam stubbed below)
#include "info_panel.h"
#include "theme_editor.h"
#include "settings_app.h"
#include "settings_mock.h"
#include "phone_view.h"
#include "alert_card.h"
#include "settings_frame.h"
#include "sky_panel.h"
#include "sky_dial.h"
#include "nws.h"
#include "src/libs/lodepng/lodepng.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define SCREEN_W 1024
#define SCREEN_H 600

static uint16_t       s_fb[SCREEN_W * SCREEN_H];   // RGB565, what the panel would show
static clock_widget_t *s_clock = NULL;             // the real clock, on the top layer
static nws_alerts_t   s_alerts;                    // what nws_get_alerts() serves here
static lv_image_dsc_t s_bg;                        // decoded background (RGB565), if any
static bool           s_have_bg = false;

// ---------------------------------------------------------------------------
// Platform seams the widgets call (the device implements them in display_fsm / image_decode)
// ---------------------------------------------------------------------------

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
 * @brief The alerts the banner's tap opens (the device's come from the NWS task).
 * @version 1
 * @utility
 * @return The fake alerts.
 */
const nws_alerts_t *nws_get_alerts(void)
{
    return &s_alerts;
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
 * v2: screen fill bound; the old screen is deleted (its bindings end).
 * @version 2
 * @utility
 * @return The screen.
 */
static lv_obj_t *fresh_screen(bool with_bg)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    ui_bind_fill_role(scr, UI_ROLE_SCREEN);
    if (with_bg && s_have_bg) {
        lv_obj_t *img = lv_image_create(scr);
        lv_image_set_src(img, &s_bg);
        lv_obj_set_pos(img, 0, 0);
    }
    lv_obj_t *old = lv_screen_active();
    lv_screen_load(scr);
    if (old) lv_obj_delete(old);   // its bindings end with it, as on the device
    return scr;
}

// ---------------------------------------------------------------------------
// The clock (real widget, top layer)
// ---------------------------------------------------------------------------

/**
 * @brief Run LVGL timers and animations for @p ms of real time (morphs, rolls settle).
 * @version 1
 * @utility
 */
static void settle(uint32_t ms)
{
    uint32_t t0 = tick_cb();
    while (tick_cb() - t0 < ms) {
        lv_timer_handler();
        usleep(5000);
    }
}

/**
 * @brief Show the clock in @p mode (hidden for -1), coloured the way the device colours it:
 *        the theme ink made legible over the picture under each mode's area.
 * @version 1
 * @utility
 */
static void clock_in(int mode)
{
    if (!s_clock) {
        s_clock = clock_widget_create(lv_layer_top());
        struct tm tm = { .tm_hour = 12, .tm_min = 9, .tm_mday = 24, .tm_mon = 10, .tm_year = 126,
                         .tm_wday = 2 };
        clock_widget_update(s_clock, &tm);
    }
    lv_obj_t *root = lv_obj_get_child(lv_layer_top(), 0);
    lv_obj_set_flag(root, LV_OBJ_FLAG_HIDDEN, mode < 0);
    if (mode < 0) return;
    const lv_image_dsc_t *pic = s_have_bg ? &s_bg : NULL;
    lv_color_t scr = ui_theme_color(UI_ROLE_SCREEN);
    lv_area_t a;
    clock_widget_set_user_color(s_clock, ui_oklch_to_color(ui_theme_get()->ink));
    clock_widget_full_area(&a);
    clock_widget_set_color(s_clock, ui_theme_ink_on_picture(pic, &a, scr));
    clock_widget_bar_area(&a);
    clock_widget_set_bar_color(s_clock, ui_theme_ink_on_picture(pic, &a, scr));
    clock_widget_min_area(&a);
    clock_widget_set_minimized_base_color(s_clock, ui_theme_ink_on_picture(pic, &a, scr));
    clock_widget_set_mode(s_clock, (clock_mode_t)mode);
    settle(900);
}

// ---------------------------------------------------------------------------
// Scenes
// ---------------------------------------------------------------------------

/**
 * @brief Fill @p c and @p fc with plausible weather (no network on the host).
 * v2: gusts and chances of precipitation (one day under the 20% cut-off).
 * @version 2
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
    c->wind_gust_kmh = 35.0f;
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
        static const int POP[7] = { 10, 30, 80, 60, 70, 20, 90 };
        p->precip_pct = i % 2 ? POP[i / 2] - 10 : POP[i / 2];
    }
    fc->period_count = 14;
    fc->valid = true;
}

/**
 * @brief Local time @p hour : @p min on 2026-@p mon-@p day at the public sample place
 *        (Chicago, not the device's): the clock set to it and the sky computed for it.
 * @version 1
 * @utility
 */
static void sample_moment(int mon, int day, int hour, int min, sky_data_t *d)
{
    setenv("TZ", "CST6CDT,M3.2.0,M11.1.0", 1);
    tzset();
    struct tm tm = { .tm_year = 126, .tm_mon = mon - 1, .tm_mday = day, .tm_hour = hour,
                     .tm_min = min, .tm_isdst = -1 };
    time_t t = mktime(&tm);
    localtime_r(&t, &tm);
    clock_in(CLOCK_MODE_MINIMIZED);
    clock_widget_update(s_clock, &tm);
    sky_data_compute(t, 41.88f, -87.63f, 2.3f, d);
}

/**
 * @brief The weather screen at local @p hour : @p min on 2026-@p mon-@p day: card, forecast
 *        strip, sky dial and a Severe alert over the background.
 * v2: a real event name; no contrast calls (bindings).
 * v3: the real clock, minimized.
 * v4: the sky dial, at a given time.
 * v5: any date (the moon's place and phase vary with it).
 * v6: an optional sky (@p desc, e.g. "Clear"): the card's condition and its icon.
 * @version 6
 * @utility
 */
static void scene_weather_on(int mon, int day, int hour, int min, const char *desc)
{
    lv_obj_t *scr = fresh_screen(true);
    sky_data_t sky;
    sample_moment(mon, day, hour, min, &sky);
    sky_dial_create(scr, &sky, 588, 250, 420, 166);
    nws_conditions_t c;
    nws_forecast_t fc;
    fake_weather(&c, &fc);
    if (desc) snprintf(c.description, sizeof(c.description), "%s", desc);
    weather_card_t *card = weather_card_create(scr);
    weather_card_update(card, &c);
    if (desc) weather_card_load_condition_lottie(card, desc, sky.part == SKY_DAY);
    forecast_strip_t *strip = forecast_strip_create(scr);
    forecast_strip_update(strip, &fc);
    alert_banner_t *ab = alert_banner_create(scr);
    nws_alert_t a;
    memset(&a, 0, sizeof(a));
    strcpy(a.event, "Winter Storm Warning");
    strcpy(a.severity, "Severe");
    strcpy(a.headline, "until 7 PM: heavy snow, 6 to 10 inches");
    alert_banner_show(ab, &a);
    settle(900);
}

/**
 * @brief The weather screen at local @p hour : @p min on 24 Nov.
 * @version 1
 * @utility
 */
static void scene_weather(int hour, int min)
{
    scene_weather_on(11, 24, hour, min, NULL);
}

/** Sky dial moments: the real moon by day and night, at its phases. */
static const struct {
    int         mon, day, hour, min;
    const char *name;
    const char *sky;   /**< The card's condition; NULL: the sample's. */
} MOON_MOMENTS[] = {
    { 12, 1, 10, 0, "moon_day_waning", "Sunny" },      // last quarter, up through the morning
    { 11, 20, 15, 0, "moon_day_waxing", NULL },        // waxing gibbous, risen early afternoon
    { 12, 5, 6, 10, "moon_predawn", "Clear" },         // waning crescent before sunrise
    { 12, 12, 17, 30, "moon_evening", "Clear" },       // waxing crescent after sunset
};

/**
 * @brief Write the sky dial moments for theme @p stem.
 * @version 1
 * @utility
 */
static void moon_moments(const char *out_dir, const char *stem)
{
    char file[512];
    for (size_t i = 0; i < sizeof(MOON_MOMENTS) / sizeof(MOON_MOMENTS[0]); i++) {
        scene_weather_on(MOON_MOMENTS[i].mon, MOON_MOMENTS[i].day, MOON_MOMENTS[i].hour,
                         MOON_MOMENTS[i].min, MOON_MOMENTS[i].sky);
        snprintf(file, sizeof(file), "%s/%s_%s.ppm", out_dir, stem, MOON_MOMENTS[i].name);
        write_frame(file);
    }
}

/**
 * @brief One labelled row of text tiers on a panel of role @p fill.
 * v2: fill and text bound.
 * @version 2
 * @utility
 */
static void tier_panel(lv_obj_t *scr, ui_role_t fill, int32_t x, int32_t y)
{
    lv_obj_t *p = lv_obj_create(scr);
    lv_obj_set_size(p, 300, 150);
    lv_obj_set_pos(p, x, y);
    lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
    ui_bind_fill_role(p, fill);
    lv_obj_remove_flag(p, LV_OBJ_FLAG_SCROLLABLE);
    static const ui_role_t TIERS[] = { UI_ROLE_TEXT, UI_ROLE_TEXT_MUTED, UI_ROLE_TEXT_FAINT,
                                       UI_ROLE_ACCENT };
    for (int i = 0; i < 4; i++) {
        lv_obj_t *l = lv_label_create(p);
        lv_label_set_text_fmt(l, "%s on %s", ui_theme_role_name(TIERS[i]),
                              ui_theme_role_name(fill));
        lv_obj_set_style_text_font(l, &lv_font_montserrat_20, 0);
        lv_obj_set_pos(l, 0, i * 30);
        ui_bind_text(l, TIERS[i]);
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
 * v2: event and headline like a real alert.
 * @version 2
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
        snprintf(a.event, sizeof(a.event), "%s", SEV[i]);
        snprintf(a.headline, sizeof(a.headline), "example headline text");
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
 * v2: clock hidden.
 * @version 2
 * @utility
 */
static void scene_gallery(void)
{
    clock_in(-1);
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
 * @brief The home screen: the full-size clock over the background.
 * @version 1
 * @utility
 */
static void scene_clock(void)
{
    fresh_screen(true);
    clock_in(CLOCK_MODE_FULL);
}

static void noop_cb(lv_event_t *e);

/**
 * @brief The Phone screen under the top-bar clock: a sample address and code, one phone
 *        connected.
 * v2: the clock's frame area.
 * @version 2
 * @utility
 */
static void scene_phone(void)
{
    lv_obj_t *scr = fresh_screen(true);
    clock_in(CLOCK_MODE_TOPBAR);
    settings_frame_set_area(SETTINGS_FRAME_Y, SETTINGS_FRAME_W, SETTINGS_FRAME_H);
    phone_view_create(scr, "http://192.168.1.50/www/panel/index.html#0f3a9c1d2e4b5a6978c0d1e2f3a4b5c6",
                      noop_cb, noop_cb);
    phone_view_update(582, 1);
}

static bool install_theme(const char *path, ui_theme_t *t);

/**
 * @brief The theme editor over the background, on @p tab, written to <stem>_editor_<name>.
 * v2: top-bar clock; a profile for the save choice.
 * @version 2
 * @utility
 */
static void scene_editor(const char *out_dir, const char *stem, theme_editor_tab_t tab,
                         const char *name)
{
    lv_obj_t *scr = fresh_screen(true);
    clock_in(CLOCK_MODE_TOPBAR);
    theme_editor_create(scr, SETTINGS_FRAME_Y, "Winter afternoon", NULL, NULL);
    theme_editor_set_tab(tab);
    char file[512];
    snprintf(file, sizeof(file), "%s/%s_editor_%s.ppm", out_dir, stem, name);
    write_frame(file);
}

/**
 * @brief A menu entry that does nothing (the preview only draws the menu).
 * @version 1
 * @callback
 */
static void noop_cb(lv_event_t *e)
{
    LV_UNUSED(e);
}

/**
 * @brief The settings menu (the device's frame and grid) under the top-bar clock.
 * v2: the real settings app over the sample backend.
 * v3: the clock's frame area.
 * @version 3
 * @utility
 */
static void scene_settings(void)
{
    lv_obj_t *scr = fresh_screen(true);
    clock_in(CLOCK_MODE_TOPBAR);
    settings_frame_set_area(SETTINGS_FRAME_Y, SETTINGS_FRAME_W, SETTINGS_FRAME_H);
    settings_app_open(scr, settings_mock_backend());
}

/**
 * @brief Fill s_alerts with three alerts of different severities and lengths.
 * v2: generic place names (no real region).
 * @version 2
 * @utility
 */
static void fake_alerts(void)
{
    memset(&s_alerts, 0, sizeof(s_alerts));
    static const char *const EV[3] = { "Winter Storm Warning", "Wind Advisory", "Frost Advisory" };
    static const char *const SEV[3] = { "Severe", "Moderate", "Minor" };
    for (int i = 0; i < 3; i++) {
        nws_alert_t *a = &s_alerts.alerts[i];
        strcpy(a->event, EV[i]);
        strcpy(a->severity, SEV[i]);
        strcpy(a->urgency, "Expected");
        strcpy(a->certainty, "Likely");
        strcpy(a->area_desc, "North County; Lake County; River County; Hill County");
        a->onset = 1795993200 + i * 3600;
        a->expires = a->onset + 9 * 3600;
    }
    strcpy(s_alerts.alerts[0].headline, "Winter Storm Warning issued November 24 at 9:12AM EST until "
           "November 24 at 7:00PM EST by NWS Example Office");
    strcpy(s_alerts.alerts[0].description, "* WHAT...Heavy snow expected. Total snow accumulations "
           "of 6 to 10 inches. Winds gusting as high as 35 mph.\n\n* WHERE...Portions of the "
           "region.\n\n* WHEN...Until 7 PM EST this evening.\n\n* IMPACTS...Travel could be "
           "very difficult to impossible. The hazardous conditions could impact the evening "
           "commute.");
    strcpy(s_alerts.alerts[0].instruction, "If you must travel, keep an extra flashlight, food, "
           "and water in your vehicle in case of an emergency. The latest road conditions can be "
           "obtained by calling 5 1 1.");
    s_alerts.alert_count = 3;
    s_alerts.valid = true;
}

/**
 * @brief The alert details card over the weather screen (as a banner tap opens it).
 * v2: the weather scene takes a time.
 * @version 2
 * @utility
 */
static void scene_alert(void)
{
    fake_alerts();
    scene_weather(12, 9);
    alert_card_open(lv_layer_top(), &s_alerts, 0);
}

/**
 * @brief The sky screen (sun and moon cards under the minimised clock) at local
 *        @p hour : @p min on 2026-@p mon-@p day, at a public sample place (Chicago, not the
 *        device's).
 * v2: through sample_moment (shared with the weather scene).
 * @version 2
 * @utility
 */
static void scene_sky(int mon, int day, int hour, int min)
{
    lv_obj_t *scr = fresh_screen(true);
    sky_data_t d;
    sample_moment(mon, day, hour, min, &d);
    sky_panel_create(scr, &d);
    settle(900);
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
 * @brief Install theme @p path (parse, set, recolour the LVGL theme and every binding).
 * @version 1
 * @utility
 * @return Whether it parsed (@p t set only then).
 */
static bool install_theme(const char *path, ui_theme_t *t)
{
    char *json = read_text(path);
    bool ok = json && ui_theme_parse(json, t);
    free(json);
    if (!ok) {
        fprintf(stderr, "skip %s: not a theme file\n", path);
        return false;
    }
    ui_theme_set(t);
    ui_theme_lvgl_init(lv_display_get_default());   // first call installs, later ones recolour
    ui_bind_refresh_all();
    return true;
}

/**
 * @brief Build the weather scene under theme @p first, switch to @p path in place and write
 *        <stem>_live.ppm: the live repaint, no widget rebuilt.
 * v2: the weather scene takes a time.
 * @version 2
 * @utility
 */
static void render_live(const char *out_dir, const char *first, const char *path)
{
    ui_theme_t t;
    if (!install_theme(first, &t)) return;
    scene_weather(12, 9);
    if (!install_theme(path, &t)) return;
    char stem[64], file[512];
    stem_of(path, stem, sizeof(stem));
    snprintf(file, sizeof(file), "%s/%s_live.ppm", out_dir, stem);
    write_frame(file);
}

/**
 * @brief The editor under theme @p path after moving the accent hue to 200: what one drag of
 *        the h slider repaints.
 * v2: top-bar clock; a profile.
 * @version 2
 * @utility
 */
static void render_edit_demo(const char *out_dir, const char *path)
{
    ui_theme_t t;
    if (!install_theme(path, &t)) return;
    lv_obj_t *scr = fresh_screen(true);
    clock_in(CLOCK_MODE_TOPBAR);
    theme_editor_create(scr, SETTINGS_FRAME_Y, "Winter afternoon", NULL, NULL);
    theme_editor_set_tab(THEME_EDITOR_ACCENT);
    ui_oklch_t accent = t.accent;
    accent.h = 200.0f;
    theme_editor_set_core(THEME_EDITOR_ACCENT, accent);
    char stem[64], file[512];
    stem_of(path, stem, sizeof(stem));
    snprintf(file, sizeof(file), "%s/%s_editor_edit.ppm", out_dir, stem);
    write_frame(file);
    fresh_screen(false);
}

/**
 * @brief Render every scene for the theme in @p path into @p out_dir.
 * v2: installs through install_theme (bindings repainted).
 * v3: editor scenes per tab.
 * v4: settings and alert-card scenes.
 * v5: sky scenes (day, night, dawn).
 * v6: weather at noon, dusk and night.
 * v7: the home clock.
 * v8: the schedule prototype.
 * v9: the upload screen.
 * v10: the schedule scenes are gone (the panel shots cover them).
 * v11: the Phone screen (was the upload mock).
 * v12: the editor's Alert tab.
 * v13: sky dial moments (the real moon, by day and night).
 * @version 13
 * @utility
 * @return Whether the theme parsed.
 */
static bool render_theme(const char *out_dir, const char *path)
{
    ui_theme_t t;
    if (!install_theme(path, &t)) return false;
    char stem[64], file[512];
    stem_of(path, stem, sizeof(stem));
    scene_clock();
    snprintf(file, sizeof(file), "%s/%s_clock.ppm", out_dir, stem);
    write_frame(file);
    scene_phone();
    snprintf(file, sizeof(file), "%s/%s_phone.ppm", out_dir, stem);
    write_frame(file);
    scene_weather(12, 9);
    snprintf(file, sizeof(file), "%s/%s_weather.ppm", out_dir, stem);
    write_frame(file);
    scene_weather(20, 40);
    snprintf(file, sizeof(file), "%s/%s_weather_night.ppm", out_dir, stem);
    write_frame(file);
    scene_weather(16, 10);
    snprintf(file, sizeof(file), "%s/%s_weather_dusk.ppm", out_dir, stem);
    write_frame(file);
    moon_moments(out_dir, stem);
    scene_gallery();
    snprintf(file, sizeof(file), "%s/%s_gallery.ppm", out_dir, stem);
    write_frame(file);
    scene_editor(out_dir, stem, THEME_EDITOR_INK, "ink");
    scene_editor(out_dir, stem, THEME_EDITOR_ACCENT, "accent");
    scene_editor(out_dir, stem, THEME_EDITOR_ALERT, "alert");
    scene_editor(out_dir, stem, THEME_EDITOR_CONTRAST, "text");
    scene_editor(out_dir, stem, THEME_EDITOR_SLOTS, "slots");
    fresh_screen(false);   // closes the editor: the theme it opened with comes back
    scene_settings();
    snprintf(file, sizeof(file), "%s/%s_settings.ppm", out_dir, stem);
    write_frame(file);
    scene_alert();
    snprintf(file, sizeof(file), "%s/%s_alert.ppm", out_dir, stem);
    write_frame(file);
    alert_card_close();
    scene_sky(11, 24, 14, 20);
    snprintf(file, sizeof(file), "%s/%s_sky_day.ppm", out_dir, stem);
    write_frame(file);
    scene_sky(11, 24, 20, 40);
    snprintf(file, sizeof(file), "%s/%s_sky_night.ppm", out_dir, stem);
    write_frame(file);
    scene_sky(12, 5, 6, 10);
    snprintf(file, sizeof(file), "%s/%s_sky_dawn.ppm", out_dir, stem);
    write_frame(file);
    printf("%s: %s\n", stem, t.name);
    return true;
}

/**
 * @brief Entry point: set up LVGL with an RGB565 display, then render each theme.
 * v2: the picture feeds bound text; live-switch renders.
 * v3: 64-byte aligned draw buffer (LVGL asserts and spins otherwise); edit demo.
 * v4: --decisions renders the open clock-text options instead.
 * v5: --decisions removed (decided: charcoal, the date follows the time, alerts from one core).
 * @version 5
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
    static uint8_t buf[SCREEN_W * 100 * 2] __attribute__((aligned(LV_DRAW_BUF_ALIGN)));   // LVGL asserts (and spins) on a misaligned buffer
    lv_display_set_buffers(disp, buf, NULL, sizeof(buf), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(disp, flush_cb);
    s_have_bg = load_background(argv[2]);
    ui_bind_set_picture(s_have_bg ? &s_bg : NULL);   // what bound text sits on, as on the device
    int failed = 0;
    for (int i = 3; i < argc; i++) failed += render_theme(argv[1], argv[i]) ? 0 : 1;
    for (int i = 4; i < argc; i++) render_live(argv[1], argv[3], argv[i]);
    render_edit_demo(argv[1], argv[3]);
    return failed;
}
