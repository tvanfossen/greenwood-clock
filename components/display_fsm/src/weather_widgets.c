// components/display_fsm/src/weather_widgets.c
//
// Layer 1: WeatherCard + ForecastStrip widgets.
// Caller must hold LVGL lock for all functions.

#include "display_widgets.h"
#include "display_fsm.h"
#include "nws.h"
#include "esp_lvgl_port.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

static const char *TAG = "weather_widgets";

LV_FONT_DECLARE(nunito_48);
LV_FONT_DECLARE(nunito_128);

// ============================================================================
// Condition → Lottie path mapping
// ============================================================================

#define LOTTIE_WEATHER_DIR "/sdcard/lottie/weather"

// Map NWS description keywords to Lottie file basenames.
// Returns POSIX path (for stat), not LVGL path.
static const char *condition_to_lottie_basename(const char *desc)
{
    if (!desc) return "mostly_cloudy";

    // Order matters: more specific matches first. "haze"/"windy" are day-only;
    // the caller handles the night fallback.
    static const struct {
        const char *base;
        const char *keys[3];
    } table[] = {
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
    for (size_t i = 0; i < sizeof(table) / sizeof(table[0]); i++) {
        for (int k = 0; k < 3; k++) {
            if (table[i].keys[k] && strcasestr(desc, table[i].keys[k]))
                return table[i].base;
        }
    }
    return "mostly_cloudy";     // fallback — file exists in both day/ and night/
}

// Build the full POSIX path for a weather condition Lottie.
// is_daytime selects day/ vs night/ subdirectory.
static void build_condition_lottie_path(const char *desc, bool is_daytime,
                                         char *posix_out, size_t posix_sz,
                                         char *lvgl_out, size_t lvgl_sz)
{
    const char *base = condition_to_lottie_basename(desc);
    const char *tod = is_daytime ? "day" : "night";
    snprintf(posix_out, posix_sz, "%s/%s/%s.json", LOTTIE_WEATHER_DIR, tod, base);
    snprintf(lvgl_out, lvgl_sz, "A:/lottie/weather/%s/%s.json", tod, base);
}

// ============================================================================
// WeatherCard Lottie background load task
// ============================================================================

#if LV_USE_LOTTIE

#define WX_LOTTIE_W          400
#define WX_LOTTIE_H          400
#define WX_LOTTIE_FPS        15

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

static float c_to_f(float c) { return c * 9.0f / 5.0f + 32.0f; }
static float kmh_to_mph(float k) { return k * 0.621371f; }

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
    // H=380: card bottom at y=446, forecast strip top at y=462 → 16px gap.
    w->container = lv_obj_create(parent);
    lv_obj_set_size(w->container, 556, 380);
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
    lv_obj_set_style_text_color(w->lbl_temp_unit, lv_color_hex(0x8888aa), 0);
    lv_obj_align_to(w->lbl_temp_unit, w->lbl_temp, LV_ALIGN_OUT_RIGHT_TOP, 4, 8);
    lv_label_set_text(w->lbl_temp_unit, "F");

    // Description
    w->lbl_description = lv_label_create(w->container);
    lv_obj_set_style_text_font(w->lbl_description, &nunito_48, 0);
    lv_obj_set_style_text_color(w->lbl_description, lv_color_hex(0xc0c0c0), 0);
    lv_obj_align_to(w->lbl_description, w->lbl_temp, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 4);
    lv_label_set_text(w->lbl_description, "");

    // Feels like
    w->lbl_feels = lv_label_create(w->container);
    lv_obj_set_style_text_font(w->lbl_feels, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(w->lbl_feels, lv_color_hex(0x8888aa), 0);
    lv_obj_align_to(w->lbl_feels, w->lbl_description, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 8);
    lv_label_set_text(w->lbl_feels, "");

    // Wind
    w->lbl_wind = lv_label_create(w->container);
    lv_obj_set_style_text_font(w->lbl_wind, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(w->lbl_wind, lv_color_hex(0x8888aa), 0);
    lv_obj_align_to(w->lbl_wind, w->lbl_feels, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 4);
    lv_label_set_text(w->lbl_wind, "");

    // Humidity
    w->lbl_humidity = lv_label_create(w->container);
    lv_obj_set_style_text_font(w->lbl_humidity, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(w->lbl_humidity, lv_color_hex(0x8888aa), 0);
    lv_obj_align_to(w->lbl_humidity, w->lbl_wind, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 4);
    lv_label_set_text(w->lbl_humidity, "");

    ESP_LOGI(TAG, "WeatherCard created");
    return w;
}

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

lv_obj_t *weather_card_container(const weather_card_t *w)
{
    return w ? w->container : NULL;
}

void weather_card_set_color(weather_card_t *w, lv_color_t primary, lv_color_t secondary)
{
    if (!w) return;
    lv_obj_set_style_text_color(w->lbl_temp, primary, 0);
    lv_obj_set_style_text_color(w->lbl_description, secondary, 0);
    lv_obj_set_style_text_color(w->lbl_feels, secondary, 0);
    lv_obj_set_style_text_color(w->lbl_wind, secondary, 0);
    lv_obj_set_style_text_color(w->lbl_humidity, secondary, 0);
}

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

void weather_card_load_condition_lottie(weather_card_t *w, const char *condition_desc,
                                         bool is_daytime)
{
#if LV_USE_LOTTIE
    if (!w || !condition_desc) return;

    // Build paths
    char posix_path[128], lvgl_path[128];
    build_condition_lottie_path(condition_desc, is_daytime,
                                posix_path, sizeof(posix_path),
                                lvgl_path, sizeof(lvgl_path));

    // Check if file exists before creating Lottie widget
    struct stat st;
    if (stat(posix_path, &st) != 0) {
        // Try fallback: day/mostly_cloudy.json (cloudy.json doesn't exist)
        snprintf(posix_path, sizeof(posix_path), "%s/day/mostly_cloudy.json", LOTTIE_WEATHER_DIR);
        snprintf(lvgl_path, sizeof(lvgl_path), "A:/lottie/weather/day/mostly_cloudy.json");
        if (stat(posix_path, &st) != 0) {
            ESP_LOGW(TAG, "No weather Lottie files found (tried %s)", posix_path);
            return;
        }
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
    // Position: centered in card as background, behind text
    lv_obj_align(w->lottie_widget, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_opa(w->lottie_widget, LV_OPA_30, 0);
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
        lv_obj_t *lbl_condition;
        lv_obj_t *lbl_temps;
    } days[FORECAST_DAYS];
};

forecast_strip_t *forecast_strip_create(lv_obj_t *parent)
{
    forecast_strip_t *s = lv_malloc(sizeof(forecast_strip_t));
    if (!s) {
        ESP_LOGE(TAG, "Failed to allocate forecast_strip_t");
        return NULL;
    }
    memset(s, 0, sizeof(*s));

    // Horizontal container at bottom. Height 160 (was 130) so verbose NWS
    // condition descriptions like "Chance Showers And Thunderstorms" wrap
    // to 3–4 lines without overlapping the temp label below.
    s->container = lv_obj_create(parent);
    lv_obj_set_size(s->container, 992, 160);
    lv_obj_align(s->container, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_obj_set_style_bg_color(s->container, lv_color_hex(0x1a1a2e), 0);
    lv_obj_set_style_bg_opa(s->container, LV_OPA_80, 0);
    lv_obj_set_style_border_width(s->container, 0, 0);
    lv_obj_set_style_pad_all(s->container, 4, 0);
    lv_obj_set_flex_flow(s->container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s->container, LV_FLEX_ALIGN_SPACE_EVENLY,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(s->container, LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < FORECAST_DAYS; i++) {
        lv_obj_t *col = lv_obj_create(s->container);
        lv_obj_set_size(col, 130, 140);
        lv_obj_set_style_bg_opa(col, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(col, 0, 0);
        lv_obj_set_style_pad_all(col, 4, 0);
        lv_obj_clear_flag(col, LV_OBJ_FLAG_SCROLLABLE);
        s->days[i].col = col;

        // Day name
        s->days[i].lbl_name = lv_label_create(col);
        lv_obj_set_style_text_font(s->days[i].lbl_name, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(s->days[i].lbl_name, lv_color_white(), 0);
        lv_obj_align(s->days[i].lbl_name, LV_ALIGN_TOP_MID, 0, 0);
        lv_label_set_text(s->days[i].lbl_name, "---");

        // Condition text (replaces icon until Lottie assets exist)
        s->days[i].lbl_condition = lv_label_create(col);
        lv_obj_set_style_text_font(s->days[i].lbl_condition, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(s->days[i].lbl_condition, lv_color_hex(0x8888aa), 0);
        lv_obj_set_width(s->days[i].lbl_condition, 120);
        lv_label_set_long_mode(s->days[i].lbl_condition, LV_LABEL_LONG_DOT);
        lv_obj_align(s->days[i].lbl_condition, LV_ALIGN_CENTER, 0, 4);
        lv_label_set_text(s->days[i].lbl_condition, "");

        // Hi/Lo temps — high contrast white, larger font for readability
        s->days[i].lbl_temps = lv_label_create(col);
        lv_obj_set_style_text_font(s->days[i].lbl_temps, &lv_font_montserrat_24, 0);
        lv_obj_set_style_text_color(s->days[i].lbl_temps, lv_color_white(), 0);
        lv_obj_align(s->days[i].lbl_temps, LV_ALIGN_BOTTOM_MID, 0, 0);
        lv_label_set_text(s->days[i].lbl_temps, "--/--");
    }

    ESP_LOGI(TAG, "ForecastStrip created");
    return s;
}

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

lv_obj_t *forecast_strip_container(const forecast_strip_t *s)
{
    return s ? s->container : NULL;
}

void forecast_strip_set_color(forecast_strip_t *s, lv_color_t primary, lv_color_t secondary)
{
    if (!s) return;
    for (int i = 0; i < FORECAST_DAYS; i++) {
        lv_obj_set_style_text_color(s->days[i].lbl_name, primary, 0);
        lv_obj_set_style_text_color(s->days[i].lbl_condition, secondary, 0);
        lv_obj_set_style_text_color(s->days[i].lbl_temps, primary, 0);
    }
}

void forecast_strip_update(forecast_strip_t *s, const nws_forecast_t *fc)
{
    if (!s || !fc || !fc->valid) return;

    int day_idx = 0;
    char buf[32];

    // NWS returns 14 periods (day+night pairs). Pair them into 7 days.
    for (int i = 0; i < fc->period_count && day_idx < FORECAST_DAYS; i++) {
        const nws_forecast_period_t *p = &fc->periods[i];

        // Use daytime period as the anchor for each day
        if (!p->is_daytime && day_idx == 0 && i == 0) {
            // First period is "Tonight" — show it as day 0
            lv_label_set_text(s->days[day_idx].lbl_name, "Tonight");
            lv_label_set_text(s->days[day_idx].lbl_condition, p->short_forecast);
            snprintf(buf, sizeof(buf), "--/%d", p->temperature);
            lv_label_set_text(s->days[day_idx].lbl_temps, buf);
            day_idx++;
            continue;
        }

        if (!p->is_daytime) continue;  // skip standalone night periods

        // Day name — always derive from the system clock + day_idx offset.
        // NWS labels vary: weekday ("Friday"), context ("This Afternoon"),
        // and holiday names ("Memorial Day", "Independence Day"). First 3
        // chars of the latter two render as "Thi"/"Mem" etc. — computing
        // from the date is correct in every case.
        char name[4];
        {
            time_t now;
            time(&now);
            time_t day_t = now + (time_t)day_idx * 86400;
            struct tm ti;
            localtime_r(&day_t, &ti);
            strftime(name, sizeof(name), "%a", &ti);  // "Thu", "Fri", ...
        }
        lv_label_set_text(s->days[day_idx].lbl_name, name);

        // Short forecast as text
        lv_label_set_text(s->days[day_idx].lbl_condition, p->short_forecast);

        // Hi/Lo: day temp is hi, next period (night) is lo
        int hi = p->temperature;
        int lo = hi;  // fallback
        if (i + 1 < fc->period_count && !fc->periods[i + 1].is_daytime) {
            lo = fc->periods[i + 1].temperature;
        }
        snprintf(buf, sizeof(buf), "%d/%d", hi, lo);
        lv_label_set_text(s->days[day_idx].lbl_temps, buf);

        day_idx++;
    }

    ESP_LOGI(TAG, "ForecastStrip updated: %d days", day_idx);
}
