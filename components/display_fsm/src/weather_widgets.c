// components/display_fsm/src/weather_widgets.c
//
// Layer 1: WeatherCard + ForecastStrip widgets.
// Caller must hold LVGL lock for all functions.

#include "display_widgets.h"
#include "nws.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "weather_widgets";

LV_FONT_DECLARE(nunito_48);
LV_FONT_DECLARE(nunito_128);

// ============================================================================
// WeatherCard
// ============================================================================

struct weather_card_t {
    lv_obj_t *container;
    lv_obj_t *lbl_temp;         // large temperature
    lv_obj_t *lbl_description;  // "Mostly Cloudy"
    lv_obj_t *lbl_feels;        // "Feels like 68F"
    lv_obj_t *lbl_wind;         // "Wind: NW 12 mph"
    lv_obj_t *lbl_humidity;     // "Humidity: 65%"
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

    // Container — left region of screen
    w->container = lv_obj_create(parent);
    lv_obj_set_size(w->container, 650, 420);
    lv_obj_align(w->container, LV_ALIGN_TOP_LEFT, 16, 16);
    lv_obj_set_style_bg_opa(w->container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(w->container, 0, 0);
    lv_obj_set_style_pad_all(w->container, 0, 0);
    lv_obj_clear_flag(w->container, LV_OBJ_FLAG_SCROLLABLE);

    // Temperature — large, 128pt
    w->lbl_temp = lv_label_create(w->container);
    lv_obj_set_style_text_font(w->lbl_temp, &nunito_128, 0);
    lv_obj_set_style_text_color(w->lbl_temp, lv_color_white(), 0);
    lv_obj_align(w->lbl_temp, LV_ALIGN_TOP_LEFT, 0, 20);
    lv_label_set_text(w->lbl_temp, "--\xC2\xB0");

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
    if (w->container) lv_obj_del(w->container);
    lv_free(w);
    ESP_LOGI(TAG, "WeatherCard destroyed");
}

lv_obj_t *weather_card_container(const weather_card_t *w)
{
    return w ? w->container : NULL;
}

void weather_card_update(weather_card_t *w, const nws_conditions_t *cond)
{
    if (!w || !cond || !cond->valid) return;

    char buf[64];

    // Temperature in F
    snprintf(buf, sizeof(buf), "%.0f\xC2\xB0", c_to_f(cond->temp_c));
    lv_label_set_text(w->lbl_temp, buf);

    // Description
    lv_label_set_text(w->lbl_description, cond->description);

    // Feels like
    snprintf(buf, sizeof(buf), "Feels like %.0f\xC2\xB0""F", c_to_f(cond->feels_like_c));
    lv_label_set_text(w->lbl_feels, buf);

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

    // Horizontal container at bottom
    s->container = lv_obj_create(parent);
    lv_obj_set_size(s->container, 992, 130);
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
        lv_obj_set_size(col, 130, 110);
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

        // Hi/Lo temps
        s->days[i].lbl_temps = lv_label_create(col);
        lv_obj_set_style_text_font(s->days[i].lbl_temps, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(s->days[i].lbl_temps, lv_color_hex(0xc0c0c0), 0);
        lv_obj_align(s->days[i].lbl_temps, LV_ALIGN_BOTTOM_MID, 0, 0);
        lv_label_set_text(s->days[i].lbl_temps, "--/--");
    }

    ESP_LOGI(TAG, "ForecastStrip created");
    return s;
}

void forecast_strip_destroy(forecast_strip_t *s)
{
    if (!s) return;
    if (s->container) lv_obj_del(s->container);
    lv_free(s);
    ESP_LOGI(TAG, "ForecastStrip destroyed");
}

lv_obj_t *forecast_strip_container(const forecast_strip_t *s)
{
    return s ? s->container : NULL;
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
            snprintf(buf, sizeof(buf), "--%c/%d%c", p->temp_unit, p->temperature, p->temp_unit);
            lv_label_set_text(s->days[day_idx].lbl_temps, buf);
            day_idx++;
            continue;
        }

        if (!p->is_daytime) continue;  // skip standalone night periods

        // Day name — first 3 chars
        char name[4];
        strncpy(name, p->name, 3);
        name[3] = '\0';
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
