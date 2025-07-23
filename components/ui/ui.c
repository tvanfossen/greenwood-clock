// components/ui/ui.c

#include "esp_log.h"
#include "lvgl.h"
#include "esp_lvgl_port.h"         // lvgl_port_lock()/unlock()
#include "bsp/display.h"           // bsp_display APIs (if needed)
#include "time_sync.h"   // time_sync_get_local()
#include "secrets.h"
#include "weather.h"
#include "freertos/FreeRTOS.h"     // pdMS_TO_TICKS
#include "freertos/task.h"         // vTaskDelay

static const char* TAG = "ui";

static lv_obj_t* lbl_time;
static lv_obj_t* lbl_ampm;
static lv_obj_t* lbl_date;

static lv_obj_t* img_weather;
static lv_obj_t* icon_bg;
static lv_obj_t* lbl_temp;
static lv_obj_t* lbl_weather_util;

static lv_img_dsc_t weather_icon_dsc;
static uint8_t* weather_icon_buf = NULL;
static size_t   weather_icon_size = 0;

LV_FONT_DECLARE(nunito_48);
LV_FONT_DECLARE(nunito_256);

// simple C→F helper
static inline float celsius_to_fahrenheit(float c) {
    return c * 9.0f / 5.0f + 32.0f;
}


static void clock_update_cb(lv_timer_t* t) {
    ESP_LOGI(TAG, "clock_update_cb: start");

    time_t now;
    struct tm ti;
    time_sync_get_local(&now, &ti);
    ESP_LOGI(TAG, "clock_update_cb: current RTC time %04d-%02d-%02d %02d:%02d:%02d",
             ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday,
             ti.tm_hour, ti.tm_min, ti.tm_sec);

    char buf_time[6];
    int h12 = ti.tm_hour % 12;
    if (!h12) h12 = 12;
    snprintf(buf_time, sizeof(buf_time), "%02d:%02d", h12, ti.tm_min);

    char buf_ampm[3];
    snprintf(buf_ampm, sizeof(buf_ampm), ti.tm_hour < 12 ? "AM" : "PM");

    char buf_date[30];
    strftime(buf_date, sizeof(buf_date), "%A %B %d %Y", &ti);

    ESP_LOGI(TAG, "clock_update_cb: updating time='%s' ampm='%s' date='%s'",
             buf_time, buf_ampm, buf_date);

    lvgl_port_lock(0);
    lv_label_set_text(lbl_time, buf_time);
    lv_label_set_text(lbl_ampm, buf_ampm);
    lv_label_set_text(lbl_date, buf_date);
    lvgl_port_unlock();

    ESP_LOGI(TAG, "clock_update_cb: end");
}

static void weather_update_cb(lv_timer_t* t)
{
    ESP_LOGI(TAG, "weather_update_cb: start");
    lvgl_port_lock(0);

    weather_data_t wd;
    if (weather_fetch(GREENWOOD_LAT, GREENWOOD_LONG, &wd) != ESP_OK) {
        ESP_LOGE(TAG, "weather_update_cb: fetch failed");
        return;
    }

    float ftemp      = celsius_to_fahrenheit(wd.temp_c);
    float feelsF     = celsius_to_fahrenheit(wd.feels_like_c);
    char  temp_txt[128];

    snprintf(temp_txt, sizeof(temp_txt), "%.0fF (Feels %.0fF)", ftemp, feelsF);
    lv_label_set_text(lbl_temp, temp_txt);
    lv_obj_align_to(lbl_temp, lbl_date, LV_ALIGN_OUT_BOTTOM_MID, 0, 8);
    ESP_LOGI(TAG, "weather_update_cb: temp_text='%s'", temp_txt);

    int   hum        = wd.humidity;
    int   uv         = wd.uv_index;
    int   aqi        = wd.aqi;
    char  util_text[128];
    snprintf(util_text, sizeof(util_text), "Humidity %d%% UV %d AQI %d", hum, uv, aqi);
    lv_label_set_text(lbl_weather_util, util_text);
    lv_obj_align_to(lbl_weather_util, lbl_temp, LV_ALIGN_OUT_BOTTOM_MID, 0, 8);

    ESP_LOGI(TAG, "weather_update_cb: util_text='%s'", util_text);

    free(weather_icon_buf);
    weather_icon_buf  = NULL;
    weather_icon_size = 0;

    if (weather_fetch_icon(&wd, &weather_icon_buf, &weather_icon_size) == ESP_OK) {
        ESP_LOGI(TAG, "weather_update_cb: icon %u bytes", (unsigned)weather_icon_size);

        weather_icon_dsc.header.magic     = LV_IMAGE_HEADER_MAGIC;
        weather_icon_dsc.header.cf        = LV_COLOR_FORMAT_RAW;  /* RAW container */
        weather_icon_dsc.header.flags     = 0;                 /* no extras */
        weather_icon_dsc.header.w         = 0;                 /* let LVGL parse from data */
        weather_icon_dsc.header.h         = 0;
        weather_icon_dsc.header.stride    = 0;

        weather_icon_dsc.data_size        = weather_icon_size;
        weather_icon_dsc.data             = weather_icon_buf;

        /* hand it off to LVGL */
        lv_img_set_src(img_weather, &weather_icon_dsc);
    }
    lvgl_port_unlock();

    ESP_LOGI(TAG, "weather_update_cb: complete");
}

void ui_show_splash(void) {
    ESP_LOGI(TAG, "ui_show_splash: start");
    lvgl_port_lock(0);
    lv_obj_t* scr = lv_scr_act();
    lv_obj_clean(scr);
    LV_IMG_DECLARE(splash);
    lv_obj_t* img = lv_img_create(scr);
    lv_img_set_src(img, &splash);
    lv_obj_align(img, LV_ALIGN_CENTER, 0, 0);
    lvgl_port_unlock();
    ESP_LOGI(TAG, "ui_show_splash: complete");
}

void ui_clock_init(const struct tm *ti0) {
    ESP_LOGI(TAG, "ui_clock_init: start");
    lvgl_port_lock(0);
        lv_obj_t* scr = lv_scr_act();
        // Do not clean screen here: preserve splash image

        // Create main time label
        ESP_LOGI(TAG, "ui_clock_init: creating time label");
        lbl_time = lv_label_create(scr);
        lv_obj_set_style_text_font(lbl_time, &nunito_256, 0);
        lv_obj_set_style_text_color(lbl_time, lv_color_white(), 0);

        // Create AM/PM label
        ESP_LOGI(TAG, "ui_clock_init: creating AM/PM label");
        lbl_ampm = lv_label_create(scr);
        lv_obj_set_style_text_font(lbl_ampm, &nunito_48, 0);
        lv_obj_set_style_text_color(lbl_ampm, lv_color_white(), 0);

        // Create date label
        ESP_LOGI(TAG, "ui_clock_init: creating date label");
        lbl_date = lv_label_create(scr);
        lv_obj_set_style_text_font(lbl_date, &nunito_48, 0);
        lv_obj_set_style_text_color(lbl_date, lv_color_white(), 0);

        // Initial draw
        ESP_LOGI(TAG, "ui_clock_init: performing initial draw");
        clock_update_cb(NULL);

        // Fixed alignment (never changes)
        ESP_LOGI(TAG, "ui_clock_init: aligning labels");
        lv_obj_align(lbl_time, LV_ALIGN_CENTER, 0, -60);
        lv_obj_align_to(lbl_ampm, lbl_time, LV_ALIGN_OUT_RIGHT_MID, 8, 0);
        lv_obj_align_to(lbl_date, lbl_time, LV_ALIGN_OUT_BOTTOM_MID, 0, 8);

        // Start periodic update every minute
        ESP_LOGI(TAG, "ui_clock_init: creating update timer (60s)");
        lv_timer_create(clock_update_cb, 60000, NULL);
        ESP_LOGI(TAG, "ui_clock_init: complete");
    lvgl_port_unlock();
}


void ui_weather_init(void)
{
    ESP_LOGI(TAG, "ui_weather_init");
    lvgl_port_lock(0);
        lv_obj_t* scr = lv_scr_act();
        lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
        lv_lodepng_init();

        /* 2) Create the round black background */
        icon_bg = lv_obj_create(scr);
        const lv_coord_t ICON_BG_SIZE = 100; /* whatever diameter you want */
        lv_obj_set_size(icon_bg, ICON_BG_SIZE, ICON_BG_SIZE);
        lv_obj_set_style_radius(icon_bg, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(icon_bg, 0, 0);
        lv_obj_set_style_border_opa(icon_bg, LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_color(icon_bg, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(icon_bg, LV_OPA_COVER, 0);
        lv_obj_clear_flag(icon_bg, LV_OBJ_FLAG_SCROLLABLE);
        /* Align that *under* the date label, centered horizontally */
        lv_obj_align(icon_bg, LV_ALIGN_BOTTOM_LEFT, 16, -16);

        /* 3) Create the image as a child of icon_bg and center it */
        img_weather = lv_img_create(icon_bg);
        lv_obj_center(img_weather);
        /* You’ll set its src later in weather_update_cb */

        /* 4) Create your temperature label to the right of the icon_bg */
        lbl_temp = lv_label_create(scr);
        lv_obj_set_style_text_font(lbl_temp, &nunito_48, 0);
        lv_obj_set_style_text_color(lbl_temp, lv_color_white(), 0);
        lv_obj_set_style_text_align(lbl_temp, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align_to(lbl_temp, lbl_date, LV_ALIGN_OUT_BOTTOM_MID, 0, 8);

        /* 5) And your “utility” label under the temp label */
        lbl_weather_util = lv_label_create(scr);
        lv_obj_set_style_text_font(lbl_weather_util, &nunito_48, 0);
        lv_obj_set_style_text_color(lbl_weather_util, lv_color_white(), 0);
        lv_obj_set_style_text_align(lbl_weather_util, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align_to(lbl_weather_util, lbl_temp, LV_ALIGN_OUT_BOTTOM_MID, 0, 8);

        weather_update_cb(NULL);
        lv_timer_create(weather_update_cb, 30 * 60 * 1000, NULL);
    lvgl_port_unlock();

}