// components/ui/ui.c

#include <math.h>
#include "esp_log.h"
#include "lvgl.h"
#include "esp_lvgl_port.h"         // lvgl_port_lock()/unlock()
#include "bsp/display.h"           // bsp_display APIs (if needed)
#include "time_sync.h"   // time_sync_get_local()
#include "secrets.h"
#include "weather.h"
// #include "astronomy.h" // astronomy_fetch_moon_phase() (removed)
#include "freertos/FreeRTOS.h"     // pdMS_TO_TICKS
#include "freertos/task.h"         // vTaskDelay

static const char* TAG = "ui";

static lv_obj_t* lbl_time;
static lv_obj_t* lbl_ampm;
static lv_obj_t* lbl_date;
static lv_obj_t* img_weather;
static lv_obj_t* lbl_temp;
static lv_obj_t* lbl_weather_util;
static lv_obj_t* box_time_ampm;
static lv_obj_t* box_weather_icon;
static lv_obj_t* box_temp;
static lv_obj_t* box_uv;

static lv_img_dsc_t weather_icon_dsc;
static uint8_t* weather_icon_buf = NULL;
static size_t   weather_icon_size = 0;

LV_FONT_DECLARE(nunito_48);
LV_FONT_DECLARE(nunito_256);

#define STYLE_SECTION(obj) \
    lv_obj_set_style_bg_opa(obj, 51, 0); /* 20% opacity */ \
    lv_obj_set_style_border_color(obj, lv_color_white(), 0); \
    lv_obj_set_style_border_width(obj, 3, 0); \
    lv_obj_set_style_radius(obj, 16, 0); \
    lv_obj_set_style_bg_color(obj, lv_color_black(), 0); // color is ignored if opa is TRANSP

#define ICON_BOX_W 120
#define ICON_BOX_H 120
#define DATA_BOX_W 120
#define DATA_BOX_H 60
#define ICON_PAD 8

// simple C→F helper
static inline float celsius_to_fahrenheit(float c) {
    return c * 9.0f / 5.0f + 32.0f;
}

// Moon icon buffer and descriptor
// Removed moon icon buffer and descriptor

// Removed moon phase update callback

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
    strftime(buf_date, sizeof(buf_date), "%A, %B, %d", &ti); // Drop year

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

    float ftemp = celsius_to_fahrenheit(wd.temp_c);
    char temp_txt[32];
    snprintf(temp_txt, sizeof(temp_txt), "%.0fF", ftemp); // Drop feels like
    lv_label_set_text(lbl_temp, temp_txt);

    int uv = wd.uv_index;
    char uv_text[32];
    snprintf(uv_text, sizeof(uv_text), "UV %d", uv);
    lv_label_set_text(lbl_weather_util, uv_text);

    free(weather_icon_buf);
    weather_icon_buf = NULL;
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

void ui_clock_init(const struct tm *ti0) {
    ESP_LOGI(TAG, "ui_clock_init: start");
    lvgl_port_lock(0);
    lv_obj_t* scr = lv_scr_act();
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    // Do not clean screen here: preserve splash image

    // Time+Date box: full screen width, top aligned, 8px padding left/right/top
    box_time_ampm = lv_obj_create(scr);
    STYLE_SECTION(box_time_ampm);
    lv_obj_clear_flag(box_time_ampm, LV_OBJ_FLAG_SCROLLABLE);
    int time_box_width = lv_obj_get_width(scr) - ICON_PAD * 2;
    lv_obj_set_size(box_time_ampm, time_box_width, 350);
    lv_obj_align(box_time_ampm, LV_ALIGN_TOP_MID, 0, ICON_PAD);
    lv_obj_set_style_pad_left(box_time_ampm, ICON_PAD, 0);
    lv_obj_set_style_pad_right(box_time_ampm, ICON_PAD, 0);
    lv_obj_set_style_pad_top(box_time_ampm, ICON_PAD, 0);

    lbl_time = lv_label_create(box_time_ampm);
    lv_obj_set_style_text_font(lbl_time, &nunito_256, 0);
    lv_obj_set_style_text_color(lbl_time, lv_color_white(), 0);
    lv_obj_set_style_text_align(lbl_time, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(lbl_time, LV_ALIGN_TOP_MID, 0, 24);

    lbl_ampm = lv_label_create(box_time_ampm);
    lv_obj_set_style_text_font(lbl_ampm, &nunito_48, 0);
    lv_obj_set_style_text_color(lbl_ampm, lv_color_white(), 0);
    lv_obj_align_to(lbl_ampm, lbl_time, LV_ALIGN_OUT_RIGHT_MID, 160, 0);

    lbl_date = lv_label_create(box_time_ampm);
    lv_obj_set_style_text_font(lbl_date, &nunito_48, 0);
    lv_obj_set_style_text_color(lbl_date, lv_color_white(), 0);
    lv_obj_set_style_text_align(lbl_date, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(lbl_date, LV_ALIGN_BOTTOM_MID, 0, -16); // less offset down

    // Info box: horizontal layout, bottom aligned, full screen width, 8px padding left/right/bottom
    static lv_coord_t row_dsc[] = {LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    static lv_coord_t col_dsc[] = {ICON_BOX_W, ICON_BOX_W, DATA_BOX_W, DATA_BOX_W, LV_GRID_TEMPLATE_LAST};
    lv_obj_t* box_info_row = lv_obj_create(scr);
    STYLE_SECTION(box_info_row);
    int info_box_width = lv_obj_get_width(scr) - ICON_PAD * 2;
    lv_obj_set_width(box_info_row, info_box_width);
    lv_obj_set_height(box_info_row, ICON_BOX_H + 8);
    lv_obj_align(box_info_row, LV_ALIGN_BOTTOM_MID, 0, -ICON_PAD);
    lv_obj_set_style_pad_left(box_info_row, ICON_PAD, 0);
    lv_obj_set_style_pad_right(box_info_row, ICON_PAD, 0);
    lv_obj_set_style_pad_bottom(box_info_row, ICON_PAD, 0);
    lv_obj_clear_flag(box_info_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(box_info_row, LV_LAYOUT_GRID);
    lv_obj_set_grid_dsc_array(box_info_row, col_dsc, row_dsc);

    // Weather icon
    img_weather = lv_img_create(box_info_row);
    lv_obj_set_grid_cell(img_weather, LV_GRID_ALIGN_CENTER, 0, 1, LV_GRID_ALIGN_CENTER, 0, 1);
    lv_obj_clear_flag(img_weather, LV_OBJ_FLAG_SCROLLABLE);

    // Temp label
    lbl_temp = lv_label_create(box_info_row);
    lv_obj_set_style_text_font(lbl_temp, &nunito_48, 0);
    lv_obj_set_style_text_color(lbl_temp, lv_color_white(), 0);
    lv_obj_set_style_text_align(lbl_temp, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_grid_cell(lbl_temp, LV_GRID_ALIGN_CENTER, 1, 1, LV_GRID_ALIGN_CENTER, 0, 1);

    // UV label
    lbl_weather_util = lv_label_create(box_info_row);
    lv_obj_set_style_text_font(lbl_weather_util, &nunito_48, 0);
    lv_obj_set_style_text_color(lbl_weather_util, lv_color_white(), 0);
    lv_obj_set_style_text_align(lbl_weather_util, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_grid_cell(lbl_weather_util, LV_GRID_ALIGN_CENTER, 2, 1, LV_GRID_ALIGN_CENTER, 0, 1);

    // Initial draw
    clock_update_cb(NULL);
    weather_update_cb(NULL);

    // Timers
    lv_timer_create(clock_update_cb, 60000, NULL);
    lv_timer_create(weather_update_cb, 30 * 60 * 1000, NULL);

    lvgl_port_unlock();
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
