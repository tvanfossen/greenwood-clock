// components/ui/ui.c

#include <math.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include "esp_log.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "lvgl.h"
#include "esp_lvgl_port.h"
#include "bsp/display.h"
#include "time_sync.h"
#include "secrets.h"
#include "weather.h"
#include "screen_manager.h"
#include "sdcard.h"
#include "settings.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "ui";

// ─── Screen references ────────────────────────────────────────────────────────

static lv_obj_t *screen_clock    = NULL;
static lv_obj_t *screen_settings = NULL;
static lv_obj_t *screen_wifi     = NULL;
static lv_obj_t *screen_brightness = NULL;
static lv_obj_t *screen_about    = NULL;

static lv_obj_t *lbl_time;
static lv_obj_t *lbl_ampm;
static lv_obj_t *lbl_date;
static lv_obj_t *bg_img          = NULL;

static struct tm         saved_timeinfo;
static clock_settings_t  saved_settings;
static lv_obj_t         *img_weather;
static lv_obj_t         *lbl_temp;
static lv_obj_t         *lbl_weather_util;
static lv_obj_t         *box_time_ampm;

static lv_img_dsc_t  weather_icon_dsc;
static uint8_t      *weather_icon_buf  = NULL;
static size_t        weather_icon_size = 0;

LV_FONT_DECLARE(nunito_48);
LV_FONT_DECLARE(nunito_256);

#define ICON_BOX_W 120
#define ICON_BOX_H 120
#define DATA_BOX_W 120
#define DATA_BOX_H 60
#define ICON_PAD    8

// ─── Private: clock helpers ───────────────────────────────────────────────────

/**
 * @brief Convert Celsius to Fahrenheit.
 *
 * @param c Temperature in degrees Celsius.
 * @return Equivalent temperature in degrees Fahrenheit.
 */
static inline float celsius_to_fahrenheit(float c)
{
    return c * 9.0f / 5.0f + 32.0f;
}

/**
 * @brief Format time/AM-PM/date strings from a broken-down time structure.
 *
 * @param ti        Broken-down local time.
 * @param buf_time  Output buffer for HH:MM (12-hour), at least 6 bytes.
 * @param time_sz   Size of buf_time.
 * @param buf_ampm  Output buffer for "AM"/"PM", at least 3 bytes.
 * @param ampm_sz   Size of buf_ampm.
 * @param buf_date  Output buffer for day string, at least 30 bytes.
 * @param date_sz   Size of buf_date.
 */
static void clock_format_strings(const struct tm *ti,
                                  char *buf_time, size_t time_sz,
                                  char *buf_ampm, size_t ampm_sz,
                                  char *buf_date, size_t date_sz)
{
    int h12 = ti->tm_hour % 12;
    if (!h12) h12 = 12;
    snprintf(buf_time, time_sz, "%02d:%02d", h12, ti->tm_min);
    snprintf(buf_ampm, ampm_sz, "%s", ti->tm_hour < 12 ? "AM" : "PM");
    strftime(buf_date, date_sz, "%A, %B, %d", ti);
}

/**
 * @brief LVGL timer callback: update clock display labels.
 *
 * Reads the current local time, formats strings, and applies them to the
 * LVGL labels under the LVGL port lock.
 *
 * @param t LVGL timer handle (unused).
 */
static void clock_update_cb(lv_timer_t *t)
{
    (void)t;
    time_t    now;
    struct tm ti;
    time_sync_get_local(&now, &ti);
    ESP_LOGI(TAG, "clock_update_cb: %04d-%02d-%02d %02d:%02d:%02d",
             ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday,
             ti.tm_hour, ti.tm_min, ti.tm_sec);

    char buf_time[6], buf_ampm[3], buf_date[30];
    clock_format_strings(&ti,
                         buf_time, sizeof(buf_time),
                         buf_ampm, sizeof(buf_ampm),
                         buf_date, sizeof(buf_date));

    lvgl_port_lock(0);
    lv_label_set_text(lbl_time, buf_time);
    lv_label_set_text(lbl_ampm, buf_ampm);
    lv_label_set_text(lbl_date, buf_date);
    lvgl_port_unlock();
}

// ─── Private: weather helpers ─────────────────────────────────────────────────

/**
 * @brief Format temperature and UV display strings from weather data.
 *
 * @param wd       Source weather data.
 * @param temp_txt Output buffer for temperature string (e.g. "72F").
 * @param temp_sz  Size of temp_txt.
 * @param uv_txt   Output buffer for UV string (e.g. "UV 3").
 * @param uv_sz    Size of uv_txt.
 */
static void weather_build_display_strings(const weather_data_t *wd,
                                           char *temp_txt, size_t temp_sz,
                                           char *uv_txt,  size_t uv_sz)
{
    float ftemp = celsius_to_fahrenheit(wd->temp_c);
    snprintf(temp_txt, temp_sz, "%.0fF", ftemp);
    snprintf(uv_txt,  uv_sz,  "UV %d", (int)wd->uv_index);
}

/**
 * @brief Fetch the weather icon and populate the global weather_icon_dsc.
 *
 * Frees any previously loaded icon buffer before fetching a new one.
 * Must be called without the LVGL lock held (network I/O operation).
 *
 * @param wd Source weather data containing the icon URL.
 * @return true if the icon was fetched and the descriptor is ready.
 */
static bool weather_load_icon(const weather_data_t *wd)
{
    free(weather_icon_buf);
    weather_icon_buf  = NULL;
    weather_icon_size = 0;

    if (weather_fetch_icon(wd, &weather_icon_buf, &weather_icon_size) != ESP_OK) {
        ESP_LOGW(TAG, "weather_load_icon: fetch failed");
        return false;
    }

    ESP_LOGI(TAG, "weather_load_icon: %zu bytes received", weather_icon_size);
    weather_icon_dsc = (lv_img_dsc_t){
        .header    = { .magic = LV_IMAGE_HEADER_MAGIC,
                       .cf    = LV_COLOR_FORMAT_RAW,
                       .flags = 0, .w = 0, .h = 0, .stride = 0 },
        .data_size = weather_icon_size,
        .data      = weather_icon_buf,
    };
    return true;
}

/**
 * @brief Apply weather strings and icon to LVGL widgets under the port lock.
 *
 * @param temp_txt   Temperature string (e.g. "72F").
 * @param uv_txt     UV index string (e.g. "UV 3").
 * @param icon_ready true if weather_icon_dsc is populated and ready to display.
 */
static void weather_apply_ui(const char *temp_txt, const char *uv_txt, bool icon_ready)
{
    lvgl_port_lock(0);
    lv_label_set_text(lbl_temp,         temp_txt);
    lv_label_set_text(lbl_weather_util, uv_txt);
    if (icon_ready) lv_img_set_src(img_weather, &weather_icon_dsc);
    lvgl_port_unlock();
}

/**
 * @brief LVGL timer callback: fetch weather and update display.
 *
 * Network operations are performed WITHOUT the LVGL lock.
 * The lock is acquired only for the final UI update.
 *
 * @param t LVGL timer handle (unused).
 */
static void weather_update_cb(lv_timer_t *t)
{
    (void)t;
    ESP_LOGI(TAG, "weather_update_cb: start (heap: %lu)",
             (unsigned long)esp_get_free_heap_size());

    weather_data_t wd;
    if (weather_fetch(GREENWOOD_LAT, GREENWOOD_LONG, &wd) != ESP_OK) {
        ESP_LOGE(TAG, "weather_update_cb: weather_fetch failed");
        return;
    }

    char temp_txt[32], uv_txt[32];
    weather_build_display_strings(&wd,
                                   temp_txt, sizeof(temp_txt),
                                   uv_txt,   sizeof(uv_txt));

    bool icon_ready = weather_load_icon(&wd);
    weather_apply_ui(temp_txt, uv_txt, icon_ready);
    ESP_LOGI(TAG, "weather_update_cb: complete (heap: %lu)",
             (unsigned long)esp_get_free_heap_size());
}

// ─── Private: touch and gesture callbacks ─────────────────────────────────────

/**
 * @brief LVGL event callback: log touch events on the clock screen (debug only).
 *
 * @param e LVGL event handle.
 */
static void clock_screen_touch_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED)       ESP_LOGI(TAG, "touch: PRESSED");
    else if (code == LV_EVENT_RELEASED) ESP_LOGI(TAG, "touch: RELEASED");
    else if (code == LV_EVENT_CLICKED)  ESP_LOGI(TAG, "touch: CLICKED");
}

/**
 * @brief LVGL event callback: handle swipe gestures on the clock screen.
 *
 * Swipe UP opens the settings menu. All other directions are logged.
 *
 * @param e LVGL event handle.
 */
static void clock_screen_gesture_cb(lv_event_t *e)
{
    (void)e;
    lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());
    switch (dir) {
        case LV_DIR_TOP:
            ESP_LOGI(TAG, "gesture: SWIPE UP — opening settings");
            screen_manager_push(SCREEN_SETTINGS_MENU);
            break;
        case LV_DIR_BOTTOM:
            ESP_LOGI(TAG, "gesture: SWIPE DOWN");
            break;
        case LV_DIR_LEFT:
            ESP_LOGI(TAG, "gesture: SWIPE LEFT");
            break;
        case LV_DIR_RIGHT:
            ESP_LOGI(TAG, "gesture: SWIPE RIGHT");
            break;
        default:
            break;
    }
}

// ─── Private: GIF background helpers ─────────────────────────────────────────

/**
 * @brief Configure a GIF object for fullscreen infinite-loop background playback.
 *
 * Must be called with the LVGL port lock held.
 *
 * @param gif_obj LVGL GIF object to configure.
 */
static void optimize_gif_for_background(lv_obj_t *gif_obj)
{
    lv_obj_set_width(gif_obj,  lv_obj_get_width(lv_scr_act()));
    lv_obj_set_height(gif_obj, lv_obj_get_height(lv_scr_act()));
    lv_gif_set_loop_count(gif_obj, -1);
    lv_gif_restart(gif_obj);
    ESP_LOGI(TAG, "optimize_gif_for_background: fullscreen infinite loop set");
}

// ─── Private: background loading helpers ─────────────────────────────────────

/**
 * @brief Apply the shared section-box style to an LVGL object.
 *
 * Sets transparent background, no border, and rounded corners.
 *
 * @param obj LVGL object to style.
 */
static void ui_style_section(lv_obj_t *obj)
{
    lv_obj_set_style_bg_opa(obj,      LV_OPA_TRANSP,    0);
    lv_obj_set_style_border_color(obj, lv_color_white(), 0);
    lv_obj_set_style_border_width(obj, 0,                0);
    lv_obj_set_style_radius(obj,      16,               0);
    lv_obj_set_style_bg_color(obj,    lv_color_black(), 0);
}

/**
 * @brief Return true if the given path has a .gif extension (case-insensitive).
 *
 * @param path File path string.
 * @return true if path ends in ".gif".
 */
static bool ui_is_gif_path(const char *path)
{
    const char *ext = strrchr(path, '.');
    return ext != NULL && strcasecmp(ext, ".gif") == 0;
}

/**
 * @brief Create a background image LVGL object and set its common positioning flags.
 *
 * Creates either a GIF or static image object depending on is_gif, loads the
 * file, positions it centred, sends it to the background layer, and clears
 * scrollable/layout flags.
 *
 * @param scr    Parent screen object.
 * @param path   LVGL file path string (e.g. "A:/bg.gif").
 * @param is_gif true to create an lv_gif object; false for lv_img.
 * @return The created LVGL object.
 */
static lv_obj_t *ui_bg_create_object(lv_obj_t *scr, const char *path, bool is_gif)
{
    lv_obj_t *obj;
    if (is_gif) {
        obj = lv_gif_create(scr);
        lv_gif_set_src(obj, path);
    } else {
        obj = lv_img_create(scr);
        lv_img_set_src(obj, path);
    }
    lv_obj_align(obj, LV_ALIGN_CENTER, 0, 0);
    lv_obj_move_background(obj);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(obj, LV_OBJ_FLAG_IGNORE_LAYOUT);
    return obj;
}

/**
 * @brief Release the LVGL lock, delay 50 ms, re-acquire, then optimise the GIF.
 *
 * The 50 ms delay allows the GIF decoder to initialise before the loop/size
 * parameters are applied. Caller must hold the LVGL lock before this call;
 * the lock is re-held on return.
 *
 * @param obj GIF LVGL object to configure.
 */
static void ui_bg_finalize_gif(lv_obj_t *obj)
{
    lvgl_port_unlock();
    vTaskDelay(pdMS_TO_TICKS(50));
    lvgl_port_lock(0);
    optimize_gif_for_background(obj);
}

/**
 * @brief Verify that a background image object loaded successfully.
 *
 * Deletes the object and returns false if the image/GIF failed to load.
 * Must be called with the LVGL lock held.
 *
 * @param obj    The LVGL background object to verify.
 * @param is_gif true if obj is a GIF object.
 * @param path   Path string used only for log messages.
 * @return true if the image is loaded; false if it was deleted.
 */
static bool ui_bg_verify(lv_obj_t *obj, bool is_gif, const char *path)
{
    bool loaded = is_gif ? lv_gif_is_loaded(obj) : (lv_img_get_src(obj) != NULL);
    if (!loaded) {
        ESP_LOGW(TAG, "ui_bg_verify: background failed to load from %s", path);
        lv_obj_del(obj);
        return false;
    }
    ESP_LOGI(TAG, "ui_bg_verify: background loaded from %s", path);
    return true;
}

/**
 * @brief Load the configured background image from the SD card.
 *
 * Returns NULL immediately if the SD card is not mounted or no image is
 * configured. Otherwise creates the LVGL object, applies GIF optimisation
 * if needed (with a brief lock-release delay), and verifies the load.
 *
 * This function manages its own LVGL port lock — it must be called without
 * the lock already held.
 *
 * @param scr      Parent screen object.
 * @param settings Clock settings containing the background_image path.
 * @return Loaded LVGL object, or NULL if loading failed or skipped.
 */

/**
 * @brief Create, optionally finalise, and verify a background object under the LVGL lock.
 *
 * Acquires and releases the LVGL lock internally (may briefly release it for
 * GIF initialisation delay). Caller must not hold the lock.
 *
 * @param scr    Parent screen object.
 * @param path   LVGL file path string.
 * @param is_gif true if the path refers to a GIF file.
 * @return Created object on success, NULL if verification failed.
 */
static lv_obj_t *ui_bg_load_locked(lv_obj_t *scr, const char *path, bool is_gif)
{
    lvgl_port_lock(0);
    lv_obj_t *obj = ui_bg_create_object(scr, path, is_gif);
    if (is_gif) ui_bg_finalize_gif(obj);
    bool ok = ui_bg_verify(obj, is_gif, path);
    lvgl_port_unlock();
    return ok ? obj : NULL;
}

static lv_obj_t *ui_bg_try_load(lv_obj_t *scr, const clock_settings_t *settings)
{
    if (!sdcard_is_mounted() || !settings || !settings->background_image[0]) {
        ESP_LOGW(TAG, "ui_bg_try_load: SD not mounted or no background configured");
        return NULL;
    }
    const char *path   = settings->background_image;
    bool        is_gif = ui_is_gif_path(path);
    ESP_LOGI(TAG, "ui_bg_try_load: loading %s (gif=%d)", path, (int)is_gif);
    return ui_bg_load_locked(scr, path, is_gif);
}

// ─── Private: clock screen builders ──────────────────────────────────────────

/**
 * @brief Derive the display text colour from settings, defaulting to white.
 *
 * @param settings Clock settings; may be NULL.
 * @return LVGL colour value.
 */
static lv_color_t ui_color_from_settings(const clock_settings_t *settings)
{
    if (!settings) return lv_color_white();
    uint32_t rgb = settings->text_color;
    ESP_LOGI(TAG, "ui_color_from_settings: 0x%06lX", (unsigned long)rgb);
    return lv_color_make((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
}

/**
 * @brief Create and position the time box container on the clock screen.
 *
 * Sets module-level box_time_ampm. Must be called with the LVGL lock held.
 *
 * @param scr Parent screen object.
 */
static void ui_create_time_box_container(lv_obj_t *scr)
{
    box_time_ampm = lv_obj_create(scr);
    ui_style_section(box_time_ampm);
    lv_obj_clear_flag(box_time_ampm, LV_OBJ_FLAG_SCROLLABLE);
    int w = lv_obj_get_width(scr) - ICON_PAD * 2;
    lv_obj_set_size(box_time_ampm, w, 350);
    lv_obj_align(box_time_ampm, LV_ALIGN_TOP_MID, 0, ICON_PAD);
}

/**
 * @brief Apply inner padding to the time box container.
 *
 * Must be called after ui_create_time_box_container(). Requires LVGL lock.
 */
static void ui_set_time_box_padding(void)
{
    lv_obj_set_style_pad_left(box_time_ampm,  ICON_PAD, 0);
    lv_obj_set_style_pad_right(box_time_ampm, ICON_PAD, 0);
    lv_obj_set_style_pad_top(box_time_ampm,   ICON_PAD, 0);
}

/**
 * @brief Create the primary time label (HH:MM) inside the time box.
 *
 * Sets module-level lbl_time. Must be called with the LVGL lock held.
 *
 * @param box   Parent time box object.
 * @param color Text colour.
 */
static void ui_create_time_labels(lv_obj_t *box, lv_color_t color)
{
    lbl_time = lv_label_create(box);
    lv_obj_set_style_text_font(lbl_time,  &nunito_256,         0);
    lv_obj_set_style_text_color(lbl_time, color,               0);
    lv_obj_set_style_text_align(lbl_time, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(lbl_time, LV_ALIGN_TOP_MID, 0, 24);
}

/**
 * @brief Create the AM/PM and date labels inside the time box.
 *
 * Sets module-level lbl_ampm and lbl_date. Must be called after
 * ui_create_time_labels(). Requires LVGL lock.
 *
 * @param box   Parent time box object.
 * @param color Text colour.
 */
static void ui_create_ampm_date_labels(lv_obj_t *box, lv_color_t color)
{
    lbl_ampm = lv_label_create(box);
    lv_obj_set_style_text_font(lbl_ampm,  &nunito_48, 0);
    lv_obj_set_style_text_color(lbl_ampm, color,      0);
    lv_obj_align_to(lbl_ampm, lbl_time, LV_ALIGN_OUT_RIGHT_MID, 160, 0);

    lbl_date = lv_label_create(box);
    lv_obj_set_style_text_font(lbl_date,  &nunito_48,          0);
    lv_obj_set_style_text_color(lbl_date, color,               0);
    lv_obj_set_style_text_align(lbl_date, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(lbl_date, LV_ALIGN_BOTTOM_MID, 0, -16);
}

/**
 * @brief Create the complete time/date box on the clock screen.
 *
 * Orchestrates container creation, padding, and label creation.
 * Must be called with the LVGL lock held.
 *
 * @param scr   Parent screen object.
 * @param color Text colour for all labels.
 */
static void ui_create_time_box(lv_obj_t *scr, lv_color_t color)
{
    ui_create_time_box_container(scr);
    ui_set_time_box_padding();
    ui_create_time_labels(box_time_ampm, color);
    ui_create_ampm_date_labels(box_time_ampm, color);
}

/**
 * @brief Create and configure the info row grid container.
 *
 * Must be called with the LVGL lock held.
 *
 * @param scr Parent screen object.
 * @return The created grid container object.
 */
static lv_obj_t *ui_create_info_row_container(lv_obj_t *scr)
{
    static lv_coord_t row_dsc[] = {LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    static lv_coord_t col_dsc[] = {ICON_BOX_W, ICON_BOX_W, DATA_BOX_W, DATA_BOX_W,
                                    LV_GRID_TEMPLATE_LAST};
    lv_obj_t *box = lv_obj_create(scr);
    ui_style_section(box);
    int w = lv_obj_get_width(scr) - ICON_PAD * 2;
    lv_obj_set_width(box,  w);
    lv_obj_set_height(box, ICON_BOX_H + 8);
    lv_obj_align(box, LV_ALIGN_BOTTOM_MID, 0, -ICON_PAD);
    lv_obj_clear_flag(box, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_layout(box, LV_LAYOUT_GRID);
    lv_obj_set_grid_dsc_array(box, col_dsc, row_dsc);
    return box;
}

/**
 * @brief Apply padding to the info row container.
 *
 * @param box The info row container.
 */
static void ui_create_info_row_padding(lv_obj_t *box)
{
    lv_obj_set_style_pad_left(box,   ICON_PAD, 0);
    lv_obj_set_style_pad_right(box,  ICON_PAD, 0);
    lv_obj_set_style_pad_bottom(box, ICON_PAD, 0);
}

/**
 * @brief Create the weather icon and temperature label widgets in the info row.
 *
 * Sets module-level img_weather and lbl_temp. Requires LVGL lock.
 *
 * @param box   Info row grid container.
 * @param color Text colour for the temperature label.
 */
static void ui_create_info_widgets(lv_obj_t *box, lv_color_t color)
{
    img_weather = lv_img_create(box);
    lv_obj_set_grid_cell(img_weather, LV_GRID_ALIGN_CENTER, 0, 1,
                                       LV_GRID_ALIGN_CENTER, 0, 1);
    lv_obj_clear_flag(img_weather, LV_OBJ_FLAG_SCROLLABLE);

    lbl_temp = lv_label_create(box);
    lv_obj_set_style_text_font(lbl_temp,  &nunito_48,          0);
    lv_obj_set_style_text_color(lbl_temp, color,               0);
    lv_obj_set_style_text_align(lbl_temp, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_grid_cell(lbl_temp, LV_GRID_ALIGN_CENTER, 1, 1,
                                    LV_GRID_ALIGN_CENTER, 0, 1);
}

/**
 * @brief Create the UV index label in the info row.
 *
 * Sets module-level lbl_weather_util. Requires LVGL lock.
 *
 * @param box   Info row grid container.
 * @param color Text colour.
 */
static void ui_create_uv_label(lv_obj_t *box, lv_color_t color)
{
    lbl_weather_util = lv_label_create(box);
    lv_obj_set_style_text_font(lbl_weather_util,  &nunito_48,          0);
    lv_obj_set_style_text_color(lbl_weather_util, color,               0);
    lv_obj_set_style_text_align(lbl_weather_util, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_grid_cell(lbl_weather_util, LV_GRID_ALIGN_CENTER, 2, 1,
                                            LV_GRID_ALIGN_CENTER, 0, 1);
}

/**
 * @brief Create the complete bottom info row on the clock screen.
 *
 * Orchestrates grid container, padding, and weather/UV widget creation.
 * Must be called with the LVGL lock held.
 *
 * @param scr   Parent screen object.
 * @param color Text colour for info labels.
 */
static void ui_create_info_row(lv_obj_t *scr, lv_color_t color)
{
    lv_obj_t *box = ui_create_info_row_container(scr);
    ui_create_info_row_padding(box);
    ui_create_info_widgets(box, color);
    ui_create_uv_label(box, color);
}

/**
 * @brief Register clock timers and touch/gesture event callbacks.
 *
 * Creates 60-second clock update timer and 30-minute weather update timer.
 * Registers touch and gesture callbacks on the screen object.
 * Must be called with the LVGL lock held.
 *
 * @param scr Clock screen object.
 */
static void ui_register_clock_callbacks(lv_obj_t *scr)
{
    lv_timer_create(clock_update_cb,   60000,          NULL);
    lv_timer_create(weather_update_cb, 30 * 60 * 1000, NULL);
    lv_obj_add_event_cb(scr, clock_screen_touch_cb,   LV_EVENT_PRESSED,  NULL);
    lv_obj_add_event_cb(scr, clock_screen_touch_cb,   LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(scr, clock_screen_touch_cb,   LV_EVENT_CLICKED,  NULL);
    lv_obj_add_event_cb(scr, clock_screen_gesture_cb, LV_EVENT_GESTURE,  NULL);
}

/**
 * @brief Configure the clock screen background colour and base flags.
 *
 * Must be called with the LVGL lock held.
 *
 * @param scr Clock screen object.
 */
static void ui_clock_setup_screen(lv_obj_t *scr)
{
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(scr, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER,      LV_PART_MAIN);
}

/**
 * @brief Build all LVGL clock screen objects under the LVGL lock.
 *
 * Creates the time box, info row, and registers all timers and callbacks.
 *
 * @param scr      Clock screen object (already configured).
 * @param settings Clock settings for colour derivation.
 */
static void ui_clock_build_locked(lv_obj_t *scr, const clock_settings_t *settings)
{
    ui_clock_setup_screen(scr);
    lv_color_t color = ui_color_from_settings(settings);
    ui_create_time_box(scr, color);
    ui_create_info_row(scr, color);
    ui_register_clock_callbacks(scr);
}

/**
 * @brief Perform post-lock clock initialisation steps.
 *
 * Loads the background (manages its own lock), triggers the initial data
 * callbacks (which manage their own locks and network I/O), and registers
 * the screen with the screen manager.
 *
 * @param scr      Clock screen object.
 * @param settings Clock settings containing the background path.
 */
static void ui_clock_post_init(lv_obj_t *scr, const clock_settings_t *settings)
{
    bg_img = ui_bg_try_load(scr, settings);
    clock_update_cb(NULL);
    weather_update_cb(NULL);
    screen_manager_set_clock_screen(scr);
    ESP_LOGI(TAG, "ui_clock_post_init: complete, heap=%lu",
             (unsigned long)esp_get_free_heap_size());
}

// ─── Public: clock screen ────────────────────────────────────────────────────

/**
 * @brief Initialise and display the main clock screen.
 *
 * Creates all LVGL objects, loads the configured background, triggers
 * initial time and weather fetches, and registers the screen with the
 * screen manager. This function is safe to call from any task.
 *
 * @param ti0      Initial time (unused; callbacks fetch current time directly).
 * @param settings Clock configuration (background path, text colour, etc.).
 */
void ui_clock_init(const struct tm *ti0, const clock_settings_t *settings)
{
    (void)ti0;
    ESP_LOGI(TAG, "ui_clock_init: start");
    screen_manager_init();

    lvgl_port_lock(0);
    lv_obj_t *scr = lv_scr_act();
    screen_clock = scr;
    ui_clock_build_locked(scr, settings);
    lvgl_port_unlock();

    ui_clock_post_init(scr, settings);
    ESP_LOGI(TAG, "ui_clock_init: complete");
}

// ─── Private: splash screen helpers ──────────────────────────────────────────

/**
 * @brief Create and load the splash image from SPIFFS onto the given screen.
 *
 * Must be called with the LVGL lock held.
 *
 * @param scr Parent screen object.
 */
static void ui_splash_load_image(lv_obj_t *scr)
{
    lv_obj_t *img = lv_img_create(scr);
    lv_img_set_src(img, "B:/splash.png");
    lv_obj_align(img, LV_ALIGN_CENTER, 0, 0);
    const void *src = lv_img_get_src(img);
    if (!src) {
        ESP_LOGW(TAG, "ui_splash_load_image: failed to load B:/splash.png");
    } else {
        ESP_LOGI(TAG, "ui_splash_load_image: loaded, src=%p", src);
    }
}

// ─── Public: splash screen ───────────────────────────────────────────────────

/**
 * @brief Display the splash screen from SPIFFS.
 *
 * Clears the current screen, sets a black background, and loads
 * "B:/splash.png" (SPIFFS). Used during boot before Wi-Fi is ready.
 */
void ui_show_splash(void)
{
    ESP_LOGI(TAG, "ui_show_splash: start");
    lvgl_port_lock(0);
    lv_obj_t *scr = lv_scr_act();
    lv_obj_clean(scr);
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    ui_splash_load_image(scr);
    lvgl_port_unlock();
    ESP_LOGI(TAG, "ui_show_splash: complete");
}

// ─── Private: start screen helpers ───────────────────────────────────────────

/**
 * @brief Callback invoked when the Start Clock button is pressed.
 *
 * Clears the current screen and transitions to the clock UI using the
 * previously saved time and settings from ui_show_start_screen().
 *
 * @param e LVGL event handle (unused).
 */
static void start_button_cb(lv_event_t *e)
{
    (void)e;
    ESP_LOGI(TAG, "start_button_cb: launching clock UI");
    lvgl_port_lock(0);
    lv_obj_t *scr = lv_scr_act();
    lv_obj_clean(scr);
    lvgl_port_unlock();
    ui_clock_init(&saved_timeinfo, &saved_settings);
}

/**
 * @brief Create the "Greenwood Clock" title label on the start screen.
 *
 * Must be called with the LVGL lock held.
 *
 * @param scr Parent screen object.
 */
static void ui_start_create_title(lv_obj_t *scr)
{
    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "Greenwood Clock");
    lv_obj_set_style_text_font(title,  &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(title, lv_color_white(),       0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -200);
}

/**
 * @brief Create the status info text label on the start screen.
 *
 * Must be called with the LVGL lock held.
 *
 * @param scr Parent screen object.
 */
static void ui_start_create_info_text(lv_obj_t *scr)
{
    lv_obj_t *info = lv_label_create(scr);
    lv_label_set_text(info, "System Ready\nOTA updates available from this screen");
    lv_obj_set_style_text_align(info,  LV_TEXT_ALIGN_CENTER,  0);
    lv_obj_set_style_text_color(info,  lv_color_hex(0x808080), 0);
    lv_obj_align(info, LV_ALIGN_CENTER, 0, -120);
}

/**
 * @brief Create the OTA URL section label on the start screen.
 *
 * Must be called with the LVGL lock held.
 *
 * @param scr Parent screen object.
 */
static void ui_start_create_ota_url_label(lv_obj_t *scr)
{
    lv_obj_t *lbl = lv_label_create(scr);
    lv_label_set_text(lbl, "OTA Server URL:");
    lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
    lv_obj_align(lbl, LV_ALIGN_CENTER, 0, -60);
}

/**
 * @brief Create a single OTA server URL text area at the given horizontal offset.
 *
 * Must be called with the LVGL lock held.
 *
 * @param scr      Parent screen object.
 * @param x_offset Horizontal offset from screen centre in pixels.
 * @param url      Initial URL text; uses placeholder if empty.
 */
static void ui_start_create_ota_textarea(lv_obj_t *scr, int x_offset, const char *url)
{
    lv_obj_t *ta = lv_textarea_create(scr);
    lv_obj_set_size(ta, 520, 40);
    lv_obj_align(ta, LV_ALIGN_CENTER, x_offset, -20);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_placeholder_text(ta, "http://192.168.1.96:8000");
    lv_textarea_set_text(ta, url[0] ? url : "http://192.168.1.96:8000");
}

/**
 * @brief Create the header section (title + info text) on the start screen.
 *
 * Must be called with the LVGL lock held.
 *
 * @param scr Parent screen object.
 */
static void ui_start_create_header(lv_obj_t *scr)
{
    ui_start_create_title(scr);
    ui_start_create_info_text(scr);
}

/**
 * @brief Create the OTA URL section (label + two text areas) on the start screen.
 *
 * Must be called with the LVGL lock held.
 *
 * @param scr Parent screen object.
 * @param url Initial URL text to pre-fill in both text areas.
 */
static void ui_start_create_ota_section(lv_obj_t *scr, const char *url)
{
    ui_start_create_ota_url_label(scr);
    ui_start_create_ota_textarea(scr, -135, url);
    ui_start_create_ota_textarea(scr,  135, url);
}

/**
 * @brief Create an OTA update button at the given horizontal offset.
 *
 * Must be called with the LVGL lock held.
 *
 * @param scr Parent screen object.
 * @param x   Horizontal offset from screen centre in pixels.
 */
static void ui_start_create_ota_btn(lv_obj_t *scr, int x)
{
    lv_obj_t *btn = lv_btn_create(scr);
    lv_obj_set_size(btn, 200, 60);
    lv_obj_align(btn, LV_ALIGN_CENTER, x, 150);
    lv_obj_add_event_cb(btn, start_button_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, LV_SYMBOL_DOWNLOAD " Update");
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_20, 0);
    lv_obj_center(lbl);
}

/**
 * @brief Create the Start Clock button on the start screen.
 *
 * Must be called with the LVGL lock held.
 *
 * @param scr Parent screen object.
 */
static void ui_start_create_start_btn(lv_obj_t *scr)
{
    lv_obj_t *btn = lv_btn_create(scr);
    lv_obj_set_size(btn, 200, 60);
    lv_obj_align(btn, LV_ALIGN_CENTER, 0, 60);
    lv_obj_add_event_cb(btn, start_button_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, "Start Clock");
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_24, 0);
    lv_obj_center(lbl);
}

/**
 * @brief Create the Start and OTA button group on the start screen.
 *
 * Must be called with the LVGL lock held.
 *
 * @param scr Parent screen object.
 */
static void ui_start_create_buttons(lv_obj_t *scr)
{
    ui_start_create_start_btn(scr);
    ui_start_create_ota_btn(scr, -220);
    ui_start_create_ota_btn(scr,  220);
}

/**
 * @brief Clear the active LVGL screen and set a black background.
 *
 * Must be called with the LVGL lock held.
 *
 * @return The active (now cleared) screen object.
 */
static lv_obj_t *ui_prepare_screen(void)
{
    lv_obj_t *scr = lv_scr_act();
    lv_obj_clean(scr);
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    return scr;
}

// ─── Public: start screen ────────────────────────────────────────────────────

/**
 * @brief Display the boot start screen with system status and OTA entry point.
 *
 * Saves the provided time and settings for later use by start_button_cb.
 * Creates the title, info text, OTA URL inputs, and navigation buttons.
 *
 * @param timeinfo Current time (saved for later use); may be NULL.
 * @param settings Clock configuration (saved for later use); may be NULL.
 */
void ui_show_start_screen(const struct tm *timeinfo, const clock_settings_t *settings)
{
    ESP_LOGI(TAG, "ui_show_start_screen: start");
    if (timeinfo) saved_timeinfo = *timeinfo;
    if (settings) saved_settings = *settings;

    const char *url = (settings && settings->ota_server_url[0])
                      ? settings->ota_server_url : "";

    lvgl_port_lock(0);
    lv_obj_t *scr = ui_prepare_screen();
    ui_start_create_header(scr);
    ui_start_create_ota_section(scr, url);
    ui_start_create_buttons(scr);
    lvgl_port_unlock();
    ESP_LOGI(TAG, "ui_show_start_screen: complete");
}

// ─── Private: text colour helper ─────────────────────────────────────────────

/**
 * @brief Apply a text colour to all clock screen labels under the LVGL lock.
 *
 * @param color New text colour to apply.
 */
static void ui_apply_text_color(lv_color_t color)
{
    lvgl_port_lock(0);
    if (lbl_time)         lv_obj_set_style_text_color(lbl_time,         color, LV_PART_MAIN);
    if (lbl_ampm)         lv_obj_set_style_text_color(lbl_ampm,         color, LV_PART_MAIN);
    if (lbl_date)         lv_obj_set_style_text_color(lbl_date,         color, LV_PART_MAIN);
    if (lbl_temp)         lv_obj_set_style_text_color(lbl_temp,         color, LV_PART_MAIN);
    if (lbl_weather_util) lv_obj_set_style_text_color(lbl_weather_util, color, LV_PART_MAIN);
    lvgl_port_unlock();
}

// ─── Public: refresh functions ────────────────────────────────────────────────

/**
 * @brief Delete the current background object under the LVGL lock, if present.
 *
 * Clears the global bg_img pointer after deletion.
 */
static void ui_bg_delete_old(void)
{
    lvgl_port_lock(0);
    if (bg_img) {
        lv_obj_del(bg_img);
        bg_img = NULL;
    }
    lvgl_port_unlock();
}

/**
 * @brief Reload the background image from current settings.
 *
 * Deletes the existing background (if any), then loads the newly configured
 * image from the SD card. Safe to call at any time after ui_clock_init().
 */
void ui_refresh_background(void)
{
    ESP_LOGI(TAG, "ui_refresh_background: start");

    if (!screen_clock) {
        ESP_LOGW(TAG, "ui_refresh_background: clock screen not initialised");
        return;
    }

    clock_settings_t cfg;
    if (settings_load(&cfg) != ESP_OK) {
        ESP_LOGE(TAG, "ui_refresh_background: settings_load failed");
        return;
    }

    ui_bg_delete_old();
    bg_img = ui_bg_try_load(screen_clock, &cfg);
    ESP_LOGI(TAG, "ui_refresh_background: complete, bg=%s", bg_img ? "loaded" : "none");
}

/**
 * @brief Reload the text colour from current settings and apply to all labels.
 *
 * Reads the text_color field from NVS settings and updates lbl_time,
 * lbl_ampm, lbl_date, lbl_temp, and lbl_weather_util.
 */
void ui_refresh_text_color(void)
{
    ESP_LOGI(TAG, "ui_refresh_text_color: start");

    if (!screen_clock) {
        ESP_LOGW(TAG, "ui_refresh_text_color: clock screen not initialised");
        return;
    }

    clock_settings_t cfg;
    if (settings_load(&cfg) != ESP_OK) {
        ESP_LOGE(TAG, "ui_refresh_text_color: settings_load failed");
        return;
    }

    ESP_LOGI(TAG, "ui_refresh_text_color: applying 0x%06lX", (unsigned long)cfg.text_color);
    ui_apply_text_color(lv_color_hex(cfg.text_color));
    ESP_LOGI(TAG, "ui_refresh_text_color: complete");
}
