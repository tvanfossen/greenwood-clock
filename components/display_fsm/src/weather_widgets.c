/**
 * @file weather_widgets.c
 * v2: forecast icons resolved off-lock into a cache (forecast_icons_rescan).
 * @version 2
 * @brief Layer 1: WeatherCard + ForecastStrip widgets.
 *
 * Caller must hold LVGL lock for all functions, EXCEPT forecast_icons_rescan() (SD I/O: off-lock).
 */

#include "display_widgets.h"
#include "display_fsm.h"
#include "ui_contrast.h"
#include "nws.h"
#include "esp_lvgl_port.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

static const char *TAG = "weather_widgets";

// Panel (box) background colours — the OKLCH contrast reference for text on them.
#define WX_CARD_PANEL   0x0a0a1e
#define WX_STRIP_PANEL  0x1a1a2e

/**
 * @brief Derive a legible primary + a dimmed secondary text colour for @p base over the
 *        given (opaque-equivalent) panel colour. Secondary leans ~43% toward the panel.
 * @version 1
 * @dg_internal
 */
static void derive_panel_colors(lv_color_t base, uint32_t panel_hex,
                                lv_color_t *primary, lv_color_t *secondary)
{
    lv_color_t panel = lv_color_hex(panel_hex);
    *primary   = ui_legible(base, panel);
    *secondary = lv_color_mix(panel, *primary, 85);   // gently dimmed, still clearly light
}

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
 * @dg_internal
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
 * @dg_internal
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
 * @dg_internal
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

/**
 * @brief Re-resolve every condition's forecast icon into s_icon_lvgl. Does SD stat() calls:
 *        call OFF the LVGL lock (FSM task, before dispatch); readers run on the same task.
 * @version 1
 * @req REQ-WX-006
 */
void forecast_icons_rescan(void)
{
    char posix[128], lvgl[128];
    int found = 0;
    for (int c = 0; c < WX_CONDITION_COUNT; c++) {
        for (int day = 0; day < 2; day++) {
            bool ok = resolve_base_asset(&WX_ASSET_ICON, WX_CONDITIONS[c].base, day != 0, posix,
                                         sizeof(posix), lvgl, sizeof(lvgl));
            strlcpy(s_icon_lvgl[c][day], ok ? lvgl : "", sizeof(s_icon_lvgl[c][day]));
            found += ok ? 1 : 0;
        }
    }
    ESP_LOGI(TAG, "forecast icons rescanned: %d/%d condition slots have an icon", found,
             WX_CONDITION_COUNT * 2);
}

// ============================================================================
// WeatherCard Lottie background load task
// ============================================================================

#if LV_USE_LOTTIE

#define WX_LOTTIE_W          300
#define WX_LOTTIE_H          300
#define WX_LOTTIE_FPS        20

#endif // LV_USE_LOTTIE

// ============================================================================
// WeatherCard
// ============================================================================

struct weather_card_t {
    lv_obj_t *container;
    lv_obj_t *lbl_temp;         // large temperature number
    lv_obj_t *lbl_temp_unit;    // "F" suffix (separate — Nunito lacks °)
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
 * @dg_internal
 * @return Temperature in Fahrenheit.
 */
static float c_to_f(float c) { return c * 9.0f / 5.0f + 32.0f; }
/**
 * @brief Convert km/h to mph.
 * @version 1
 * @dg_internal
 * @return Speed in mph.
 */
static float kmh_to_mph(float k) { return k * 0.621371f; }

/**
 * @brief Create the current-conditions card (temp, unit, description, feels-like, wind,
 *        humidity labels) on a dark rounded panel at the top-left of @p parent.
 * @version 1
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
    lv_obj_set_style_bg_color(w->container, lv_color_hex(0x0a0a1e), 0);
    lv_obj_set_style_bg_opa(w->container, LV_OPA_70, 0);
    lv_obj_set_style_radius(w->container, 12, 0);
    lv_obj_set_style_border_width(w->container, 0, 0);
    lv_obj_set_style_pad_all(w->container, 16, 0);
    lv_obj_clear_flag(w->container, LV_OBJ_FLAG_SCROLLABLE);

    // Temperature — large number at 128pt, unit suffix in smaller font
    w->lbl_temp = lv_label_create(w->container);
    lv_obj_set_style_text_font(w->lbl_temp, &nunito_128, 0);
    lv_obj_set_style_text_color(w->lbl_temp, lv_color_white(), 0);
    lv_obj_align(w->lbl_temp, LV_ALIGN_TOP_LEFT, 0, 20);
    lv_label_set_text(w->lbl_temp, "--");

    // "°F" suffix — Nunito fonts are ASCII-only (no ° glyph), use Montserrat
    w->lbl_temp_unit = lv_label_create(w->container);
    lv_obj_set_style_text_font(w->lbl_temp_unit, &nunito_48, 0);
    lv_obj_set_style_text_color(w->lbl_temp_unit, lv_color_hex(0xc8c8d8), 0);
    lv_obj_align_to(w->lbl_temp_unit, w->lbl_temp, LV_ALIGN_OUT_RIGHT_TOP, 4, 8);
    lv_label_set_text(w->lbl_temp_unit, "F");

    // Description
    w->lbl_description = lv_label_create(w->container);
    lv_obj_set_style_text_font(w->lbl_description, &nunito_48, 0);
    lv_obj_set_style_text_color(w->lbl_description, lv_color_hex(0xe0e0e0), 0);
    lv_obj_align_to(w->lbl_description, w->lbl_temp, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 4);
    lv_label_set_text(w->lbl_description, "");

    // Feels like
    w->lbl_feels = lv_label_create(w->container);
    lv_obj_set_style_text_font(w->lbl_feels, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(w->lbl_feels, lv_color_hex(0xc8c8d8), 0);
    lv_obj_align_to(w->lbl_feels, w->lbl_description, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 8);
    lv_label_set_text(w->lbl_feels, "");

    // Wind
    w->lbl_wind = lv_label_create(w->container);
    lv_obj_set_style_text_font(w->lbl_wind, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(w->lbl_wind, lv_color_hex(0xc8c8d8), 0);
    lv_obj_align_to(w->lbl_wind, w->lbl_feels, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 4);
    lv_label_set_text(w->lbl_wind, "");

    // Humidity
    w->lbl_humidity = lv_label_create(w->container);
    lv_obj_set_style_text_font(w->lbl_humidity, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(w->lbl_humidity, lv_color_hex(0xc8c8d8), 0);
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

/**
 * @brief Apply @p primary to the temperature and @p secondary to the description,
 *        feels-like, wind and humidity labels. NULL-safe.
 * @version 1
 * @req REQ-WX-002
 */
void weather_card_set_color(weather_card_t *w, lv_color_t primary, lv_color_t secondary)
{
    if (!w) return;
    lv_obj_set_style_text_color(w->lbl_temp, primary, 0);
    lv_obj_set_style_text_color(w->lbl_description, secondary, 0);
    lv_obj_set_style_text_color(w->lbl_feels, secondary, 0);
    lv_obj_set_style_text_color(w->lbl_wind, secondary, 0);
    lv_obj_set_style_text_color(w->lbl_humidity, secondary, 0);
}

/**
 * @brief Derive panel-legible colours from @p base: primary for temperature and
 *        description, dimmed secondary for unit, feels-like, wind and humidity. NULL-safe.
 * @version 1
 * @req REQ-WX-002
 * @req REQ-BG-002
 */
void weather_card_set_contrast(weather_card_t *w, lv_color_t base)
{
    if (!w) return;
    lv_color_t primary, secondary;
    derive_panel_colors(base, WX_CARD_PANEL, &primary, &secondary);
    lv_obj_set_style_text_color(w->lbl_temp, primary, 0);
    lv_obj_set_style_text_color(w->lbl_description, primary, 0);
    lv_obj_set_style_text_color(w->lbl_temp_unit, secondary, 0);
    lv_obj_set_style_text_color(w->lbl_feels, secondary, 0);
    lv_obj_set_style_text_color(w->lbl_wind, secondary, 0);
    lv_obj_set_style_text_color(w->lbl_humidity, secondary, 0);
}

/**
 * @brief Refresh the card labels from current conditions (F / mph; dash for missing
 *        temperatures) and re-align them. Ignores NULL or invalid input.
 * @version 1
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
        lv_label_set_text(w->lbl_temp, "—");
    } else {
        snprintf(buf, sizeof(buf), "%.0f", c_to_f(cond->temp_c));
        lv_label_set_text(w->lbl_temp, buf);
    }
    // Re-align unit suffix after temp text width may have changed
    lv_obj_align_to(w->lbl_temp_unit, w->lbl_temp, LV_ALIGN_OUT_RIGHT_TOP, 4, 8);

    // Description
    lv_label_set_text(w->lbl_description, cond->description);

    // Feels like
    if (cond->feels_like_c <= -200.0f) {
        lv_label_set_text(w->lbl_feels, "Feels like —");
    } else {
        snprintf(buf, sizeof(buf), "Feels like %.0fF", c_to_f(cond->feels_like_c));
        lv_label_set_text(w->lbl_feels, buf);
    }

    // Wind
    snprintf(buf, sizeof(buf), "Wind %s %.0f mph",
             cond->wind_dir_cardinal, kmh_to_mph(cond->wind_speed_kmh));
    lv_label_set_text(w->lbl_wind, buf);

    // Humidity
    snprintf(buf, sizeof(buf), "Humidity %d%%", cond->humidity);
    lv_label_set_text(w->lbl_humidity, buf);

    // Re-align after text changes
    lv_obj_align_to(w->lbl_description, w->lbl_temp, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 4);
    lv_obj_align_to(w->lbl_feels, w->lbl_description, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 8);
    lv_obj_align_to(w->lbl_wind, w->lbl_feels, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 4);
    lv_obj_align_to(w->lbl_humidity, w->lbl_wind, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 4);
}

/**
 * @brief Resolve the condition's Lottie file, allocate its SPIRAM render buffer, create
 *        the background widget and queue it on the shared Lottie loader task.
 *        No-op when LV_USE_LOTTIE is off; failures are logged and cleaned up.
 * @version 1
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
    // Position: centered in card, behind text. Opacity raised from 30% — at 30%
    // it read as a dull, washed-out watermark; 80% makes the condition animation
    // clearly visible while still sitting behind the (now bright) text labels.
    lv_obj_align(w->lottie_widget, LV_ALIGN_CENTER, 0, 0);
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
        lv_obj_t *lbl_temps;
    } days[FORECAST_DAYS];
};

/**
 * @brief Create one label in a forecast column.
 * @version 1
 * @dg_internal
 * @return The label.
 */
static lv_obj_t *strip_label(lv_obj_t *col, const lv_font_t *font, lv_align_t align, int32_t y,
                             const char *text)
{
    lv_obj_t *lbl = lv_label_create(col);
    lv_obj_set_style_text_font(lbl, font, 0);
    lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
    lv_obj_align(lbl, align, 0, y);
    lv_label_set_text(lbl, text);
    return lbl;
}

/**
 * @brief Create day column @p i: name, condition icon (text fallback), hi/lo.
 * @version 1
 * @dg_internal
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

    s->days[i].lbl_name = strip_label(col, &lv_font_montserrat_20, LV_ALIGN_TOP_MID, 0, "---");

    s->days[i].img_icon = lv_image_create(col);
    lv_obj_align(s->days[i].img_icon, LV_ALIGN_CENTER, 0, 2);
    lv_obj_add_flag(s->days[i].img_icon, LV_OBJ_FLAG_HIDDEN);

    s->days[i].lbl_condition = strip_label(col, &lv_font_montserrat_14, LV_ALIGN_CENTER, 4, "");
    lv_obj_set_width(s->days[i].lbl_condition, 120);
    lv_label_set_long_mode(s->days[i].lbl_condition, LV_LABEL_LONG_DOT);
    lv_obj_add_flag(s->days[i].lbl_condition, LV_OBJ_FLAG_HIDDEN);

    s->days[i].lbl_temps = strip_label(col, &lv_font_montserrat_24, LV_ALIGN_BOTTOM_MID, 0, "--/--");
}

/**
 * @brief Create the 7-day forecast strip along the bottom of @p parent.
 * @version 2
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
    lv_obj_set_style_bg_color(s->container, lv_color_hex(WX_STRIP_PANEL), 0);
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
 * @brief Apply @p primary to every day's name and hi/lo labels and @p secondary to the
 *        condition text label. NULL-safe.
 * @version 1
 * @req REQ-WX-003
 */
void forecast_strip_set_color(forecast_strip_t *s, lv_color_t primary, lv_color_t secondary)
{
    if (!s) return;
    for (int i = 0; i < FORECAST_DAYS; i++) {
        lv_obj_set_style_text_color(s->days[i].lbl_name, primary, 0);
        lv_obj_set_style_text_color(s->days[i].lbl_condition, secondary, 0);
        lv_obj_set_style_text_color(s->days[i].lbl_temps, primary, 0);
    }
}

/**
 * @brief Derive a panel-legible primary from @p base and apply it uniformly to every
 *        day-column label (no dimmed tier).
 * @version 1
 * @req REQ-WX-003
 * @req REQ-BG-002
 */
void forecast_strip_set_contrast(forecast_strip_t *s, lv_color_t base)
{
    lv_color_t primary, secondary;
    derive_panel_colors(base, WX_STRIP_PANEL, &primary, &secondary);
    (void)secondary;
    // Uniform: every day-column label (name, condition, hi/lo) gets the same
    // legible primary colour — no dimmed tier — so the strip reads cleanly.
    forecast_strip_set_color(s, primary, primary);
}

/**
 * @brief Show the static condition icon for a day, or the text if no icon asset exists.
 * v2: looks the icon up in the off-lock cache (forecast_icons_rescan) instead of stat()ing the
 *     SD card here, under the LVGL lock.
 * @version 2
 * @req REQ-WX-006
 */
static void strip_set_condition(forecast_strip_t *s, int day, const char *desc, bool is_daytime)
{
    const char *lvgl = s_icon_lvgl[condition_index(desc)][is_daytime ? 1 : 0];
    bool icon = lvgl[0] != '\0';
    if (icon) {
        lv_image_set_src(s->days[day].img_icon, lvgl);
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
 * @brief Weekday abbreviation for today + @p day_idx (NWS period names vary: holidays,
 *        "This Afternoon"), derived from the clock so it is right in every case.
 * @version 1
 * @dg_internal
 */
static void strip_day_name(int day_idx, char *out, size_t out_sz)
{
    time_t now;
    time(&now);
    time_t day_t = now + (time_t)day_idx * 86400;
    struct tm ti;
    localtime_r(&day_t, &ti);
    strftime(out, out_sz, "%a", &ti);
}

/**
 * @brief Fill one column from a forecast period (night period @p p only for "Tonight").
 * @version 1
 * @dg_internal
 */
static void strip_fill_day(forecast_strip_t *s, int day, const nws_forecast_t *fc, int i)
{
    const nws_forecast_period_t *p = &fc->periods[i];
    char buf[32];
    if (p->is_daytime) {
        strip_day_name(day, buf, sizeof(buf));
        lv_label_set_text(s->days[day].lbl_name, buf);
        bool has_night = i + 1 < fc->period_count && !fc->periods[i + 1].is_daytime;
        int lo = has_night ? fc->periods[i + 1].temperature : p->temperature;
        snprintf(buf, sizeof(buf), "%d/%d", p->temperature, lo);
    } else {
        lv_label_set_text(s->days[day].lbl_name, "Tonight");
        snprintf(buf, sizeof(buf), "--/%d", p->temperature);
    }
    lv_label_set_text(s->days[day].lbl_temps, buf);
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
