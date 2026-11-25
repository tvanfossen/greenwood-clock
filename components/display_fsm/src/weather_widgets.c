/**
 * @file weather_widgets.c
 * v2: forecast icons resolved off-lock into a cache (forecast_icons_rescan).
 * v3: panel text colours judged against the panel's composite backdrop; Lottie top right.
 * v4: colours are theme bindings (ui_theme_bind); the set_color / set_contrast calls are gone.
 * v5: degree signs; forecast high and low on two lines.
 * v6: chance of precipitation per day, wind gusts, feels-like only when it differs.
 * v7: no chance of precipitation in the strip (it crowded the column).
 * @version 7
 * @brief Layer 1: WeatherCard + ForecastStrip widgets.
 *
 * Caller must hold LVGL lock for all functions, EXCEPT forecast_icons_rescan() (SD I/O: off-lock).
 */

#include "display_widgets.h"
#include "display_fsm.h"
#include "ui_theme_bind.h"
#include "image_decode.h"
#include "nws.h"
#include "esp_lvgl_port.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

static const char *TAG = "weather_widgets";

LV_FONT_DECLARE(nunito_48);
LV_FONT_DECLARE(nunito_128);

// ============================================================================
// Condition → Lottie path mapping
// ============================================================================


/** NWS keyword table (case-insensitive, most specific first). The LAST entry is the fallback. */
static const struct {
    const char *base;
    const char *keys[3];
} WX_CONDITIONS[] = {
    {"thunderstorm",  {"Thunderstorm", "Thunder", NULL}},
    {"snow",          {"Snow", "Flurries", "Blizzard"}},
    {"ice",           {"Freezing", "Ice", "Sleet"}},
    {"fog",           {"Fog", "Mist", NULL}},
    {"haze",          {"Haze", NULL, NULL}},
    {"drizzle",       {"Drizzle", "Light Rain", NULL}},
    {"rain",          {"Rain", "Showers", NULL}},
    {"windy",         {"Wind", NULL, NULL}},
    {"clear",         {"Sunny", "Clear", NULL}},
    {"partly_cloudy", {"Partly", NULL, NULL}},
    {"overcast",      {"Overcast", NULL, NULL}},
    {"mostly_cloudy", {"Cloudy", NULL, NULL}},
};
#define WX_CONDITION_COUNT ((int)(sizeof(WX_CONDITIONS) / sizeof(WX_CONDITIONS[0])))

/**
 * @brief Index into WX_CONDITIONS of the first entry whose keyword occurs in @p desc.
 * @version 1
 * @utility
 * @return Matching index, or the last (mostly_cloudy) when @p desc is NULL or nothing matches.
 */
static int condition_index(const char *desc)
{
    int found = WX_CONDITION_COUNT - 1;
    for (int i = 0; desc && found == WX_CONDITION_COUNT - 1 && i < WX_CONDITION_COUNT - 1; i++) {
        for (int k = 0; k < 3 && found != i; k++) {
            if (WX_CONDITIONS[i].keys[k] && strcasestr(desc, WX_CONDITIONS[i].keys[k])) found = i;
        }
    }
    return found;
}

/**
 * @brief Map NWS description keywords (case-insensitive, most specific first) to a
 *        condition asset basename.
 * v2: the keyword table moved to file scope (WX_CONDITIONS) so the icon cache can walk it.
 * @version 2
 * @utility
 * @return Condition basename used to build the POSIX path (for stat), not an LVGL path;
 *         "mostly_cloudy" when @p desc is NULL or nothing matches.
 */
static const char *condition_to_lottie_basename(const char *desc)
{
    return WX_CONDITIONS[condition_index(desc)].base;
}

/** One family of per-condition assets: <subdir>/{day,night}/<condition><ext> on the SD card. */
typedef struct {
    const char *subdir;   /**< Relative to the SD root, e.g. "lottie/weather". */
    const char *ext;      /**< ".json" / ".png". */
} condition_asset_t;

static const condition_asset_t WX_ASSET_LOTTIE = {"lottie/weather", ".json"};
static const condition_asset_t WX_ASSET_ICON   = {"icons/weather", ".png"};

/**
 * @brief Format the POSIX path for one asset candidate and report whether it exists.
 * @version 1
 * @utility
 * @return true when the file exists.
 */
static bool asset_candidate(const condition_asset_t *a, const char *tod, const char *base,
                            char *posix_out, size_t posix_sz)
{
    struct stat st;
    snprintf(posix_out, posix_sz, "/sdcard/%s/%s/%s%s", a->subdir, tod, base, a->ext);
    return stat(posix_out, &st) == 0;
}

/**
 * @brief Resolve the asset for a condition basename: requested time of day, then day/ (haze
 *        and windy are day-only), then day/mostly_cloudy. Does file I/O (stat): off-lock only.
 * @version 1
 * @req REQ-WX-005
 * @req REQ-WX-006
 * @return true with @p posix_out / @p lvgl_out filled; false when none exists (not logged).
 */
static bool resolve_base_asset(const condition_asset_t *a, const char *base, bool is_daytime,
                               char *posix_out, size_t posix_sz, char *lvgl_out, size_t lvgl_sz)
{
    bool found = asset_candidate(a, is_daytime ? "day" : "night", base, posix_out, posix_sz) ||
                 asset_candidate(a, "day", base, posix_out, posix_sz) ||
                 asset_candidate(a, "day", "mostly_cloudy", posix_out, posix_sz);
    if (found) snprintf(lvgl_out, lvgl_sz, "A:%s", posix_out + strlen("/sdcard"));
    return found;
}

/**
 * @brief Resolve the asset for an NWS description (see resolve_base_asset for the chain).
 * v2: the chain moved to resolve_base_asset so the icon cache can resolve by basename.
 * @version 2
 * @req REQ-WX-005
 * @return true with @p posix_out / @p lvgl_out filled; false (logged) when none exists.
 */
static bool resolve_condition_asset(const condition_asset_t *a, const char *desc, bool is_daytime,
                                    char *posix_out, size_t posix_sz, char *lvgl_out, size_t lvgl_sz)
{
    const char *base = condition_to_lottie_basename(desc);
    bool found = resolve_base_asset(a, base, is_daytime, posix_out, posix_sz, lvgl_out, lvgl_sz);
    if (!found) {
        ESP_LOGW(TAG, "no %s asset for '%s' (%s) under /sdcard/%s", a->ext, desc ? desc : "(null)",
                 base, a->subdir);
    }
    return found;
}

/** Resolved forecast-icon LVGL path per [condition][is_daytime]; "" = no icon (text fallback). */
static char s_icon_lvgl[WX_CONDITION_COUNT][2][64];
/** Pre-decoded icon per slot (SPIRAM, ARGB8888). Decoded once and never freed: a strip
 *  being rendered may reference it, so a rescan must not replace it under the renderer. */
static lv_image_dsc_t *s_icon_dsc[WX_CONDITION_COUNT][2];

/**
 * @brief Resolve one icon slot and decode it once (off-lock) if it is not decoded yet.
 * @version 1
 * @req REQ-WX-006
 * @return true if the slot has an icon (decoded, or path resolvable for fallback).
 */
static bool icon_slot_load(int c, int day)
{
    char posix[128], lvgl[128];
    bool ok = resolve_base_asset(&WX_ASSET_ICON, WX_CONDITIONS[c].base, day != 0, posix,
                                 sizeof(posix), lvgl, sizeof(lvgl));
    strlcpy(s_icon_lvgl[c][day], ok ? lvgl : "", sizeof(s_icon_lvgl[c][day]));
    if (ok && !s_icon_dsc[c][day]) {
        s_icon_dsc[c][day] = image_decode_png_file(posix);   // NULL → strip falls back to the path
    }
    return ok;
}

/**
 * @brief Re-resolve every condition's forecast icon and pre-decode any not yet decoded.
 *        Does SD I/O and PNG decode: call OFF the LVGL lock (FSM task, before dispatch);
 *        readers run on the same task.
 * v2: pre-decodes icons into RAM, so drawing the strip under the LVGL lock does no file
 *     I/O or PNG decode (strip_upd measured 63-68 ms on device with path sources).
 * @version 2
 * @req REQ-WX-006
 */
void forecast_icons_rescan(void)
{
    int64_t t0 = esp_timer_get_time();
    int found = 0, decoded = 0;
    for (int c = 0; c < WX_CONDITION_COUNT; c++) {
        for (int day = 0; day < 2; day++) {
            found += icon_slot_load(c, day) ? 1 : 0;
            decoded += s_icon_dsc[c][day] ? 1 : 0;
        }
    }
    ESP_LOGI(TAG, "forecast icons rescanned: %d/%d slots have an icon, %d decoded (%lld ms)",
             found, WX_CONDITION_COUNT * 2, decoded, (esp_timer_get_time() - t0) / 1000);
}

// ============================================================================
// WeatherCard Lottie background load task
// ============================================================================

#if LV_USE_LOTTIE

#define WX_LOTTIE_W          220
#define WX_LOTTIE_H          220
#define WX_LOTTIE_FPS        20

#endif // LV_USE_LOTTIE

// ============================================================================
// WeatherCard
// ============================================================================

struct weather_card_t {
    lv_obj_t *container;
    lv_obj_t *lbl_temp;         // large temperature number
    lv_obj_t *lbl_temp_unit;    // "°F" suffix (Montserrat: Nunito here has no ° glyph)
    lv_obj_t *lbl_description;  // "Mostly Cloudy"
    lv_obj_t *lbl_feels;        // "Feels like 68F"
    lv_obj_t *lbl_wind;         // "Wind: NW 12 mph"
    lv_obj_t *lbl_humidity;     // "Humidity: 65%"
#if LV_USE_LOTTIE
    lv_obj_t      *lottie_widget;    // condition animation
    void          *lottie_buf;       // SPIRAM render buffer
#endif
};

/**
 * @brief Convert degrees Celsius to Fahrenheit.
 * @version 1
 * @utility
 * @return Temperature in Fahrenheit.
 */
static float c_to_f(float c) { return c * 9.0f / 5.0f + 32.0f; }
/**
 * @brief Convert km/h to mph.
 * @version 1
 * @utility
 * @return Speed in mph.
 */
static float kmh_to_mph(float k) { return k * 0.621371f; }

/**
 * @brief Create the current-conditions card (temp, unit, description, feels-like, wind,
 *        humidity labels) on a dark rounded panel at the top-left of @p parent.
 * v3: colours from the theme roles (ui_theme), no literals.
 * v4: colours bound to theme roles (ui_bind_*): repainted live on a theme change.
 * v5: "°F" unit (Montserrat, which has the degree sign).
 * @version 5
 * @return New card, or NULL on allocation failure (logged).
 * @req REQ-WX-002
 */
weather_card_t *weather_card_create(lv_obj_t *parent)
{
    weather_card_t *w = lv_malloc(sizeof(weather_card_t));
    if (!w) {
        ESP_LOGE(TAG, "Failed to allocate weather_card_t");
        return NULL;
    }
    memset(w, 0, sizeof(*w));

    // Container — left region of screen with dark semi-opaque backdrop.
    // Y=66: reserves 50px alert banner zone + 16px margin.
    // H=350: card bottom at y=416; forecast strip top at y=432 (600-8-160) → 16px
    // gap. (Was 380 → bottom 446, which overlapped the strip top by ~14px.)
    w->container = lv_obj_create(parent);
    lv_obj_set_size(w->container, 556, 350);
    lv_obj_align(w->container, LV_ALIGN_TOP_LEFT, 16, ALERT_BANNER_HEIGHT + 16);
    ui_bind_fill(w->container, UI_SLOT_CARD);
    lv_obj_set_style_bg_opa(w->container, LV_OPA_70, 0);
    lv_obj_set_style_radius(w->container, 12, 0);
    lv_obj_set_style_border_width(w->container, 0, 0);
    lv_obj_set_style_pad_all(w->container, 16, 0);
    lv_obj_clear_flag(w->container, LV_OBJ_FLAG_SCROLLABLE);

    // Temperature — large number at 128pt, unit suffix in smaller font
    w->lbl_temp = lv_label_create(w->container);
    lv_obj_set_style_text_font(w->lbl_temp, &nunito_128, 0);
    ui_bind_text(w->lbl_temp, UI_ROLE_TEXT);
    lv_obj_align(w->lbl_temp, LV_ALIGN_TOP_LEFT, 0, 20);
    lv_label_set_text(w->lbl_temp, "--");

    // "°F" suffix: these Nunito fonts are ASCII-only (no ° glyph), Montserrat has it
    w->lbl_temp_unit = lv_label_create(w->container);
    lv_obj_set_style_text_font(w->lbl_temp_unit, &lv_font_montserrat_48, 0);
    ui_bind_text(w->lbl_temp_unit, UI_ROLE_TEXT_MUTED);
    lv_obj_align_to(w->lbl_temp_unit, w->lbl_temp, LV_ALIGN_OUT_RIGHT_TOP, 4, 8);
    lv_label_set_text(w->lbl_temp_unit, "\xC2\xB0" "F");

    // Description
    w->lbl_description = lv_label_create(w->container);
    lv_obj_set_style_text_font(w->lbl_description, &nunito_48, 0);
    ui_bind_text(w->lbl_description, UI_ROLE_TEXT);
    lv_obj_align_to(w->lbl_description, w->lbl_temp, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 4);
    lv_label_set_text(w->lbl_description, "");

    // Feels like
    w->lbl_feels = lv_label_create(w->container);
    lv_obj_set_style_text_font(w->lbl_feels, &lv_font_montserrat_24, 0);
    ui_bind_text(w->lbl_feels, UI_ROLE_TEXT_MUTED);
    lv_obj_align_to(w->lbl_feels, w->lbl_description, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 8);
    lv_label_set_text(w->lbl_feels, "");

    // Wind
    w->lbl_wind = lv_label_create(w->container);
    lv_obj_set_style_text_font(w->lbl_wind, &lv_font_montserrat_24, 0);
    ui_bind_text(w->lbl_wind, UI_ROLE_TEXT_MUTED);
    lv_obj_align_to(w->lbl_wind, w->lbl_feels, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 4);
    lv_label_set_text(w->lbl_wind, "");

    // Humidity
    w->lbl_humidity = lv_label_create(w->container);
    lv_obj_set_style_text_font(w->lbl_humidity, &lv_font_montserrat_24, 0);
    ui_bind_text(w->lbl_humidity, UI_ROLE_TEXT_MUTED);
    lv_obj_align_to(w->lbl_humidity, w->lbl_wind, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 4);
    lv_label_set_text(w->lbl_humidity, "");

    ESP_LOGI(TAG, "WeatherCard created");
    return w;
}

/**
 * @brief Free the Lottie SPIRAM buffer, delete the card's LVGL tree and animations, and
 *        free the card. NULL-safe.
 * @version 1
 * @req REQ-WX-002
 */
void weather_card_destroy(weather_card_t *w)
{
    if (!w) return;
#if LV_USE_LOTTIE
    // lottie_widget is a child of container — lv_obj_delete(container) destroys it.
    // But free the SPIRAM render buffer.
    if (w->lottie_buf) { heap_caps_free(w->lottie_buf); w->lottie_buf = NULL; }
#endif
    if (w->container) {
        lv_anim_delete(w->container, NULL);
        lv_obj_delete(w->container);
    }
    lv_free(w);
    ESP_LOGI(TAG, "WeatherCard destroyed");
}

/**
 * @brief Get the card's root LVGL container.
 * @version 1
 * @return Container object, or NULL when @p w is NULL.
 * @req REQ-WX-002
 */
lv_obj_t *weather_card_container(const weather_card_t *w)
{
    return w ? w->container : NULL;
}

/** The feels-like temperature shows only when it is this far (F) from the actual. */
#define FEELS_SHOW_F 3.0f

/**
 * @brief Set the feels-like line: shown only when it differs from the actual temperature by
 *        FEELS_SHOW_F or more (and both are known).
 * @version 1
 * @utility
 * @return Whether it shows.
 */
static bool card_set_feels(weather_card_t *w, const nws_conditions_t *cond)
{
    bool known = cond->feels_like_c > -200.0f && cond->temp_c > -200.0f;
    float feels = c_to_f(cond->feels_like_c);
    bool show = known && fabsf(feels - c_to_f(cond->temp_c)) >= FEELS_SHOW_F;
    if (show) lv_label_set_text_fmt(w->lbl_feels, "Feels like %.0f\xC2\xB0" "F", (double)feels);
    lv_obj_set_flag(w->lbl_feels, LV_OBJ_FLAG_HIDDEN, !show);
    return show;
}

/**
 * @brief Set the wind line, with the gusts when they are a few mph above the wind.
 * @version 1
 * @utility
 */
static void card_set_wind(weather_card_t *w, const nws_conditions_t *cond)
{
    float speed = kmh_to_mph(cond->wind_speed_kmh), gust = kmh_to_mph(cond->wind_gust_kmh);
    if (gust >= speed + 3.0f) {
        lv_label_set_text_fmt(w->lbl_wind, "Wind %s %.0f mph, gusts %.0f",
                              cond->wind_dir_cardinal, (double)speed, (double)gust);
    } else {
        lv_label_set_text_fmt(w->lbl_wind, "Wind %s %.0f mph", cond->wind_dir_cardinal,
                              (double)speed);
    }
}

/**
 * @brief Refresh the card labels from current conditions (F / mph; "--" for missing
 *        temperatures and humidity) and re-align them. Ignores NULL or invalid input.
 * v2: "--" for missing values (the em dash is not in the font: it drew nothing); humidity too
 *     (showed "-1%").
 * v3: degree sign on the feels-like temperature.
 * v4: feels-like only when it differs by 3 F or more; wind gusts.
 * @version 4
 * @req REQ-WX-002
 */
void weather_card_update(weather_card_t *w, const nws_conditions_t *cond)
{
    if (!w || !cond || !cond->valid) return;

    char buf[64];

    // Temperature in F. NWS occasionally returns a response with no
    // "temperature" field; nws_conditions defaults to -999°C → -1766°F.
    // Treat anything below -200°C as "no data" and show a dash.
    if (cond->temp_c <= -200.0f) {
        lv_label_set_text(w->lbl_temp, "--");
    } else {
        snprintf(buf, sizeof(buf), "%.0f", c_to_f(cond->temp_c));
        lv_label_set_text(w->lbl_temp, buf);
    }
    // Re-align unit suffix after temp text width may have changed
    lv_obj_align_to(w->lbl_temp_unit, w->lbl_temp, LV_ALIGN_OUT_RIGHT_TOP, 4, 8);

    // Description
    lv_label_set_text(w->lbl_description, cond->description);

    bool feels = card_set_feels(w, cond);
    card_set_wind(w, cond);

    // Humidity
    if (cond->humidity < 0) snprintf(buf, sizeof(buf), "Humidity --");
    else snprintf(buf, sizeof(buf), "Humidity %d%%", cond->humidity);
    lv_label_set_text(w->lbl_humidity, buf);

    // Re-align after text changes
    lv_obj_align_to(w->lbl_description, w->lbl_temp, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 4);
    lv_obj_align_to(w->lbl_feels, w->lbl_description, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 8);
    lv_obj_align_to(w->lbl_wind, feels ? w->lbl_feels : w->lbl_description,
                    LV_ALIGN_OUT_BOTTOM_LEFT, 0, feels ? 4 : 8);
    lv_obj_align_to(w->lbl_humidity, w->lbl_wind, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 4);
}

/**
 * @brief Resolve the condition's Lottie file, allocate its SPIRAM render buffer, create
 *        the background widget and queue it on the shared Lottie loader task.
 *        No-op when LV_USE_LOTTIE is off; failures are logged and cleaned up.
 * @version 2
 * v2: 220 px at the card's top right, clear of the text (was 300 px centred over it).
 * @req REQ-WX-005
 * @req REQ-LOTTIE-001
 */
void weather_card_load_condition_lottie(weather_card_t *w, const char *condition_desc,
                                         bool is_daytime)
{
#if LV_USE_LOTTIE
    if (!w || !condition_desc) return;

    char posix_path[128], lvgl_path[128];
    if (!resolve_condition_asset(&WX_ASSET_LOTTIE, condition_desc, is_daytime, posix_path,
                                 sizeof(posix_path), lvgl_path, sizeof(lvgl_path))) {
        return;
    }

    // Allocate SPIRAM render buffer
    uint32_t stride = lv_draw_buf_width_to_stride(WX_LOTTIE_W,
                                                    LV_COLOR_FORMAT_ARGB8888_PREMULTIPLIED);
    size_t buf_sz = (size_t)stride * WX_LOTTIE_H;
    w->lottie_buf = heap_caps_aligned_alloc(64, buf_sz, MALLOC_CAP_SPIRAM);
    if (!w->lottie_buf) {
        ESP_LOGE(TAG, "Weather Lottie SPIRAM alloc failed (%zu B)", buf_sz);
        return;
    }
    memset(w->lottie_buf, 0, buf_sz);   // avoid random-pixel noise before first frame

    // Create Lottie widget (caller holds LVGL lock)
    w->lottie_widget = lv_lottie_create(w->container);
    if (!w->lottie_widget) {
        ESP_LOGE(TAG, "lv_lottie_create failed for weather condition");
        heap_caps_free(w->lottie_buf);
        w->lottie_buf = NULL;
        return;
    }
    if (lv_lottie_render_failed(w->lottie_widget)) {
        ESP_LOGE(TAG, "Weather Lottie render task failed");
        lv_obj_delete(w->lottie_widget);
        w->lottie_widget = NULL;
        heap_caps_free(w->lottie_buf);
        w->lottie_buf = NULL;
        return;
    }

    lv_obj_set_size(w->lottie_widget, WX_LOTTIE_W, WX_LOTTIE_H);
    lv_lottie_set_buffer(w->lottie_widget, WX_LOTTIE_W, WX_LOTTIE_H, w->lottie_buf);
    // Top-right of the card, beside the temperature and clear of the text column
    // (centred, it sat over the description). 80% opacity: 30% read as a watermark.
    lv_obj_align(w->lottie_widget, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_set_style_opa(w->lottie_widget, LV_OPA_80, 0);
    lv_obj_move_background(w->lottie_widget);

    // Submit to shared Lottie loader task (deep JSON parse needs 64KB stack)
    lottie_load_job_t job = {0};
    job.widget     = w->lottie_widget;
    job.target_fps = WX_LOTTIE_FPS;
    strlcpy(job.path, posix_path, sizeof(job.path));
    display_fsm_load_lottie(&job);
    ESP_LOGI(TAG, "Weather Lottie queued: %s", posix_path);
#else
    (void)w; (void)condition_desc; (void)is_daytime;
#endif
}

// ============================================================================
// ForecastStrip
// ============================================================================

#define FORECAST_DAYS 7

struct forecast_strip_t {
    lv_obj_t *container;
    struct {
        lv_obj_t *col;
        lv_obj_t *lbl_name;
        lv_obj_t *lbl_condition;   /* fallback when no icon asset exists */
        lv_obj_t *img_icon;        /* static condition icon (sdcard/icons/weather) */
        lv_obj_t *lbl_hi;
        lv_obj_t *lbl_lo;
    } days[FORECAST_DAYS];
};

/**
 * @brief Create one label in a forecast column.
 * v2: colours from the theme roles (ui_theme), no literals.
 * v3: text bound to the theme (ui_bind_text).
 * v4: takes the text role.
 * @version 4
 * @utility
 * @return The label.
 */
static lv_obj_t *strip_label(lv_obj_t *col, const lv_font_t *font, ui_role_t role,
                             lv_align_t align, int32_t y, const char *text)
{
    lv_obj_t *lbl = lv_label_create(col);
    lv_obj_set_style_text_font(lbl, font, 0);
    ui_bind_text(lbl, role);
    lv_obj_align(lbl, align, 0, y);
    lv_label_set_text(lbl, text);
    return lbl;
}

/**
 * @brief Create day column @p i: name, condition icon (text fallback), the high and, below
 *        it, the low.
 * v2: high and low on two lines (was "hi/lo" on one).
 * v3: the chance of precipitation at the icon's lower right.
 * v4: no chance of precipitation.
 * @version 4
 * @utility
 */
static void strip_create_column(forecast_strip_t *s, int i)
{
    lv_obj_t *col = lv_obj_create(s->container);
    lv_obj_set_size(col, 130, 140);
    lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(col, 0, 0);
    lv_obj_set_style_pad_all(col, 4, 0);
    lv_obj_clear_flag(col, LV_OBJ_FLAG_SCROLLABLE);
    s->days[i].col = col;

    s->days[i].lbl_name = strip_label(col, &lv_font_montserrat_20, UI_ROLE_TEXT, LV_ALIGN_TOP_MID,
                                      0, "---");

    s->days[i].img_icon = lv_image_create(col);   // 64 px, between the name and the high
    lv_obj_align(s->days[i].img_icon, LV_ALIGN_TOP_MID, 0, 24);
    lv_obj_add_flag(s->days[i].img_icon, LV_OBJ_FLAG_HIDDEN);

    s->days[i].lbl_condition = strip_label(col, &lv_font_montserrat_14, UI_ROLE_TEXT,
                                           LV_ALIGN_TOP_MID, 48, "");
    lv_obj_set_width(s->days[i].lbl_condition, 120);
    lv_obj_set_style_text_align(s->days[i].lbl_condition, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s->days[i].lbl_condition, LV_LABEL_LONG_DOT);
    lv_obj_add_flag(s->days[i].lbl_condition, LV_OBJ_FLAG_HIDDEN);

    s->days[i].lbl_hi = strip_label(col, &lv_font_montserrat_24, UI_ROLE_TEXT, LV_ALIGN_TOP_MID,
                                    86, "--");
    s->days[i].lbl_lo = strip_label(col, &lv_font_montserrat_20, UI_ROLE_TEXT_MUTED,
                                    LV_ALIGN_TOP_MID, 110, "--");
}

/**
 * @brief Create the 7-day forecast strip along the bottom of @p parent.
 * v3: colours from the theme roles (ui_theme), no literals.
 * v4: fill bound to the strip slot (ui_bind_fill).
 * @version 4
 * @req REQ-WX-006
 * @return New strip, or NULL on allocation failure (logged).
 */
forecast_strip_t *forecast_strip_create(lv_obj_t *parent)
{
    forecast_strip_t *s = lv_malloc(sizeof(forecast_strip_t));
    if (!s) {
        ESP_LOGE(TAG, "Failed to allocate forecast_strip_t");
        return NULL;
    }
    memset(s, 0, sizeof(*s));

    s->container = lv_obj_create(parent);
    lv_obj_set_size(s->container, 992, 160);
    lv_obj_align(s->container, LV_ALIGN_BOTTOM_MID, 0, -8);
    ui_bind_fill(s->container, UI_SLOT_STRIP);
    lv_obj_set_style_bg_opa(s->container, LV_OPA_80, 0);
    lv_obj_set_style_border_width(s->container, 0, 0);
    lv_obj_set_style_pad_all(s->container, 4, 0);
    lv_obj_set_flex_flow(s->container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s->container, LV_FLEX_ALIGN_SPACE_EVENLY,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(s->container, LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < FORECAST_DAYS; i++) strip_create_column(s, i);

    ESP_LOGI(TAG, "ForecastStrip created");
    return s;
}

/**
 * @brief Delete the strip's LVGL tree and animations and free the strip. NULL-safe.
 * @version 1
 * @req REQ-WX-003
 */
void forecast_strip_destroy(forecast_strip_t *s)
{
    if (!s) return;
    if (s->container) {
        lv_anim_delete(s->container, NULL);
        lv_obj_delete(s->container);
    }
    lv_free(s);
    ESP_LOGI(TAG, "ForecastStrip destroyed");
}

/**
 * @brief Get the strip's root LVGL container.
 * @version 1
 * @return Container object, or NULL when @p s is NULL.
 * @req REQ-WX-003
 */
lv_obj_t *forecast_strip_container(const forecast_strip_t *s)
{
    return s ? s->container : NULL;
}

/**
 * @brief Show the static condition icon for a day, or the text if no icon asset exists.
 * v2: looks the icon up in the off-lock cache (forecast_icons_rescan) instead of stat()ing the
 *     SD card here, under the LVGL lock.
 * v3: uses the pre-decoded descriptor when available (no file read/decode under the lock).
 * @version 3
 * @req REQ-WX-006
 */
static void strip_set_condition(forecast_strip_t *s, int day, const char *desc, bool is_daytime)
{
    int c = condition_index(desc), tod = is_daytime ? 1 : 0;
    const char *lvgl = s_icon_lvgl[c][tod];
    bool icon = lvgl[0] != '\0';
    if (icon) {
        if (s_icon_dsc[c][tod]) lv_image_set_src(s->days[day].img_icon, s_icon_dsc[c][tod]);
        else lv_image_set_src(s->days[day].img_icon, lvgl);   // decode failed: path fallback
        lv_obj_clear_flag(s->days[day].img_icon, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s->days[day].lbl_condition, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_label_set_text(s->days[day].lbl_condition, desc ? desc : "");
        lv_obj_clear_flag(s->days[day].lbl_condition, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s->days[day].img_icon, LV_OBJ_FLAG_HIDDEN);
    }
    ESP_LOGI(TAG, "ForecastStrip day %d: '%s' -> %s", day, desc ? desc : "", icon ? lvgl : "text");
}

/**
 * @brief Weekday abbreviation and month/day for today + @p day_idx, e.g. "Sun 11/22" (NWS
 *        period names vary: holidays, "This Afternoon"), derived from the clock so it is
 *        right in every case.
 * v2: adds the month/day after the weekday.
 * @version 2
 * @utility
 */
static void strip_day_name(int day_idx, char *out, size_t out_sz)
{
    time_t now;
    time(&now);
    time_t day_t = now + (time_t)day_idx * 86400;
    struct tm ti;
    localtime_r(&day_t, &ti);
    char wday[8];
    strftime(wday, sizeof(wday), "%a", &ti);
    snprintf(out, out_sz, "%s %d/%d", wday, ti.tm_mon + 1, ti.tm_mday);
}

/**
 * @brief Fill one column from a forecast period (night period @p p only for "Tonight"): the
 *        high ("Tonight" has none) over the low, in degrees.
 * v2: high and low on two lines with degree signs; a missing low shows "--".
 * v3: chance of precipitation.
 * v4: "Hi" / "Lo" written before each.
 * v5: no chance of precipitation.
 * @version 5
 * @utility
 */
static void strip_fill_day(forecast_strip_t *s, int day, const nws_forecast_t *fc, int i)
{
    const nws_forecast_period_t *p = &fc->periods[i];
    char buf[32];
    if (p->is_daytime) {
        strip_day_name(day, buf, sizeof(buf));
        lv_label_set_text(s->days[day].lbl_name, buf);
        lv_label_set_text_fmt(s->days[day].lbl_hi, "Hi %d\xC2\xB0", p->temperature);
        bool has_night = i + 1 < fc->period_count && !fc->periods[i + 1].is_daytime;
        const nws_forecast_period_t *night = has_night ? &fc->periods[i + 1] : NULL;
        if (night) lv_label_set_text_fmt(s->days[day].lbl_lo, "Lo %d\xC2\xB0", night->temperature);
        else lv_label_set_text(s->days[day].lbl_lo, "Lo --");
    } else {
        lv_label_set_text(s->days[day].lbl_name, "Tonight");
        lv_label_set_text(s->days[day].lbl_hi, "");
        lv_label_set_text_fmt(s->days[day].lbl_lo, "Lo %d\xC2\xB0", p->temperature);
    }
    strip_set_condition(s, day, p->short_forecast, p->is_daytime);
}

/**
 * @brief Populate the strip: NWS gives day/night pairs; one column per day, with a
 *        leading night period shown as "Tonight".
 * @version 2
 * @req REQ-WX-006
 */
void forecast_strip_update(forecast_strip_t *s, const nws_forecast_t *fc)
{
    if (!s || !fc || !fc->valid) return;
    int day = 0;
    for (int i = 0; i < fc->period_count && day < FORECAST_DAYS; i++) {
        bool leading_night = (i == 0 && !fc->periods[0].is_daytime);
        if (!fc->periods[i].is_daytime && !leading_night) continue;
        strip_fill_day(s, day++, fc, i);
    }
    ESP_LOGI(TAG, "ForecastStrip updated: %d days", day);
}
