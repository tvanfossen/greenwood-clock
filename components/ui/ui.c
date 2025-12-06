// components/ui/ui.c

#include <math.h>
#include <string.h>
#include <stdio.h>
#include "esp_log.h"
#include "esp_system.h"            // esp_get_free_heap_size()
#include "esp_heap_caps.h"         // heap_caps_malloc for SPIRAM allocation
#include "lvgl.h"
#include "esp_lvgl_port.h"         // lvgl_port_lock()/unlock()
#include "bsp/display.h"           // bsp_display APIs (if needed)
#include "time_sync.h"   // time_sync_get_local()
#include "secrets.h"
#include "weather.h"
#include "screen_manager.h"        // screen navigation
#include "sdcard.h"                // sdcard_is_mounted()
#include "settings.h"              // clock_settings_t
// #include "astronomy.h" // astronomy_fetch_moon_phase() (removed)
#include "freertos/FreeRTOS.h"     // pdMS_TO_TICKS
#include "freertos/task.h"         // vTaskDelay

static const char* TAG = "ui";

// Screen references
static lv_obj_t* screen_clock = NULL;
static lv_obj_t* screen_settings = NULL;
static lv_obj_t* screen_wifi = NULL;
static lv_obj_t* screen_brightness = NULL;
static lv_obj_t* screen_about = NULL;

static lv_obj_t* lbl_time;
static lv_obj_t* lbl_ampm;
static lv_obj_t* lbl_date;
static lv_obj_t* bg_img = NULL;  // Background image reference

// Saved init parameters for start screen
static struct tm saved_timeinfo;
static clock_settings_t saved_settings;
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

// GIF buffer support removed - file-based loading is sufficient
// with LVGL's frame cache (LV_GIF_CACHE_DECODE_DATA=y) handling optimization

LV_FONT_DECLARE(nunito_48);
LV_FONT_DECLARE(nunito_256);

#define STYLE_SECTION(obj) \
    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0); /* Transparent - allows background to show through */ \
    lv_obj_set_style_border_color(obj, lv_color_white(), 0); \
    lv_obj_set_style_border_width(obj, 0, 0); \
    lv_obj_set_style_radius(obj, 16, 0); \
    lv_obj_set_style_bg_color(obj, lv_color_black(), 0); // BG color ignored due to transparency

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
    ESP_LOGI(TAG, "weather_update_cb: start (heap: %lu bytes)", (unsigned long)esp_get_free_heap_size());

    // CRITICAL FIX: Do NOT lock LVGL before network operations!
    // Network fetch can take seconds and would block LVGL task
    weather_data_t wd;
    if (weather_fetch(GREENWOOD_LAT, GREENWOOD_LONG, &wd) != ESP_OK) {
        ESP_LOGE(TAG, "weather_update_cb: fetch failed");
        return;
    }

    ESP_LOGI(TAG, "weather_update_cb: fetch complete, updating UI");

    // Prepare display strings BEFORE locking LVGL
    float ftemp = celsius_to_fahrenheit(wd.temp_c);
    char temp_txt[32];
    snprintf(temp_txt, sizeof(temp_txt), "%.0fF", ftemp);

    int uv = wd.uv_index;
    char uv_text[32];
    snprintf(uv_text, sizeof(uv_text), "UV %d", uv);

    // Fetch weather icon (network operation, do NOT lock)
    free(weather_icon_buf);
    weather_icon_buf = NULL;
    weather_icon_size = 0;

    bool icon_ready = false;
    if (weather_fetch_icon(&wd, &weather_icon_buf, &weather_icon_size) == ESP_OK) {
        ESP_LOGI(TAG, "weather_update_cb: icon %u bytes", (unsigned)weather_icon_size);
        icon_ready = true;

        weather_icon_dsc.header.magic     = LV_IMAGE_HEADER_MAGIC;
        weather_icon_dsc.header.cf        = LV_COLOR_FORMAT_RAW;
        weather_icon_dsc.header.flags     = 0;
        weather_icon_dsc.header.w         = 0;
        weather_icon_dsc.header.h         = 0;
        weather_icon_dsc.header.stride    = 0;
        weather_icon_dsc.data_size        = weather_icon_size;
        weather_icon_dsc.data             = weather_icon_buf;
    }

    // NOW lock LVGL for UI updates
    lvgl_port_lock(0);

    // Update UI widgets (fast, with lock held)
    lv_label_set_text(lbl_temp, temp_txt);
    lv_label_set_text(lbl_weather_util, uv_text);

    if (icon_ready) {
        lv_img_set_src(img_weather, &weather_icon_dsc);
    }

    lvgl_port_unlock();

    ESP_LOGI(TAG, "weather_update_cb: complete (heap: %lu bytes)", (unsigned long)esp_get_free_heap_size());
}

// Touch event callback for the clock screen (for debugging only)
static void clock_screen_touch_cb(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_PRESSED) {
        ESP_LOGI(TAG, "Touch: PRESSED");
    } else if (code == LV_EVENT_RELEASED) {
        ESP_LOGI(TAG, "Touch: RELEASED");
    } else if (code == LV_EVENT_CLICKED) {
        ESP_LOGI(TAG, "Touch: CLICKED");
    }
    // Long press removed - only swipe up gesture opens settings
}

// Gesture detection callback
static void clock_screen_gesture_cb(lv_event_t* e) {
    lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());

    if (dir == LV_DIR_TOP) {
        ESP_LOGI(TAG, "Gesture: SWIPE UP - Opening settings menu");
        screen_manager_push(SCREEN_SETTINGS_MENU);
    } else if (dir == LV_DIR_BOTTOM) {
        ESP_LOGI(TAG, "Gesture: SWIPE DOWN");
    } else if (dir == LV_DIR_LEFT) {
        ESP_LOGI(TAG, "Gesture: SWIPE LEFT");
    } else if (dir == LV_DIR_RIGHT) {
        ESP_LOGI(TAG, "Gesture: SWIPE RIGHT");
    }
}

// Helper function to clean up GIF buffer - REMOVED (no longer needed)
// Helper function to optimize GIF performance
static void optimize_gif_for_background(lv_obj_t *gif_obj) {
    // Make sure the GIF is scaled to fit the display
    lv_obj_set_width(gif_obj, lv_obj_get_width(lv_scr_act()));
    lv_obj_set_height(gif_obj, lv_obj_get_height(lv_scr_act()));
    
    // Set infinite loop for continuous playback
    // This must be done AFTER the source is set
    lv_gif_set_loop_count(gif_obj, -1);  // -1 = infinite loop
    
    // Restart the GIF animation to ensure it loops properly
    lv_gif_restart(gif_obj);
    
    ESP_LOGI(TAG, "GIF optimized for background playback (infinite loop, fullscreen)");
}

void ui_clock_init(const struct tm *ti0, const clock_settings_t *settings) {
    ESP_LOGI(TAG, "ui_clock_init: start");

    // Initialize screen manager
    screen_manager_init();

    lvgl_port_lock(0);
    lv_obj_t* scr = lv_scr_act();
    screen_clock = scr;  // Store reference to clock screen
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    // Set screen background to black by default (will be overlaid by image if available)
    lv_obj_set_style_bg_color(scr, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);

    // Load background from SD card (user-selected image from settings)
    bg_img = NULL;  // Reset global reference
    if (sdcard_is_mounted() && settings != NULL && settings->background_image[0] != '\0') {
        ESP_LOGI(TAG, "SD card mounted, attempting to load background from %s", settings->background_image);
        
        // Check if the file is a GIF based on extension
        bool is_gif = false;
        const char* ext = strrchr(settings->background_image, '.');
        if (ext != NULL && (strcasecmp(ext, ".gif") == 0)) {
            is_gif = true;
        }
        
        if (is_gif) {
            ESP_LOGI(TAG, "Detected GIF file, loading from %s", settings->background_image);
            
            // Create GIF object and load directly from SD card file path
            // LVGL's frame cache (LV_GIF_CACHE_DECODE_DATA=y) handles optimization automatically
            bg_img = lv_gif_create(scr);
            lv_gif_set_src(bg_img, settings->background_image);
        } else {
            bg_img = lv_img_create(scr);
            lv_img_set_src(bg_img, settings->background_image);
        }
        
        lv_obj_align(bg_img, LV_ALIGN_CENTER, 0, 0);
        lv_obj_move_background(bg_img);  // Move to back so UI elements appear on top

        // Mark as non-scrollable and prevent unnecessary redraws
        lv_obj_clear_flag(bg_img, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(bg_img, LV_OBJ_FLAG_IGNORE_LAYOUT);
        
        // Optimize GIF if applicable (must be done after source is set)
        if (is_gif) {
            // Add small delay to ensure GIF is loaded before setting loop parameters
            vTaskDelay(pdMS_TO_TICKS(50));
            optimize_gif_for_background(bg_img);
        }

        // Verify image loaded
        bool loaded = is_gif ? lv_gif_is_loaded(bg_img) : (lv_img_get_src(bg_img) != NULL);
        if (!loaded) {
            ESP_LOGW(TAG, "Background image failed to load from %s - file may not exist or be invalid",
                     settings->background_image);
            ESP_LOGW(TAG, "Will use solid black background instead");
            lv_obj_del(bg_img);
            bg_img = NULL;
        } else {
            ESP_LOGI(TAG, "Background image loaded successfully from %s",
                     settings->background_image);
        }
    } else {
        ESP_LOGW(TAG, "SD card not mounted or no background image configured, using solid black background");
    }

    // Load text color from settings
    lv_color_t text_color = lv_color_white();  // Default to white
    if (settings != NULL) {
        // Convert RGB888 (0xRRGGBB) to lv_color_t
        uint32_t rgb = settings->text_color;
        uint8_t r = (rgb >> 16) & 0xFF;
        uint8_t g = (rgb >> 8) & 0xFF;
        uint8_t b = rgb & 0xFF;
        text_color = lv_color_make(r, g, b);
        ESP_LOGI(TAG, "Text color loaded from settings: 0x%06lX (RGB: %d, %d, %d)",
                 settings->text_color, r, g, b);
    }

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
    lv_obj_set_style_text_color(lbl_time, text_color, 0);
    lv_obj_set_style_text_align(lbl_time, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(lbl_time, LV_ALIGN_TOP_MID, 0, 24);

    lbl_ampm = lv_label_create(box_time_ampm);
    lv_obj_set_style_text_font(lbl_ampm, &nunito_48, 0);
    lv_obj_set_style_text_color(lbl_ampm, text_color, 0);
    lv_obj_align_to(lbl_ampm, lbl_time, LV_ALIGN_OUT_RIGHT_MID, 160, 0);

    lbl_date = lv_label_create(box_time_ampm);
    lv_obj_set_style_text_font(lbl_date, &nunito_48, 0);
    lv_obj_set_style_text_color(lbl_date, text_color, 0);
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
    lv_obj_set_style_text_color(lbl_temp, text_color, 0);
    lv_obj_set_style_text_align(lbl_temp, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_grid_cell(lbl_temp, LV_GRID_ALIGN_CENTER, 1, 1, LV_GRID_ALIGN_CENTER, 0, 1);

    // UV label
    lbl_weather_util = lv_label_create(box_info_row);
    lv_obj_set_style_text_font(lbl_weather_util, &nunito_48, 0);
    lv_obj_set_style_text_color(lbl_weather_util, text_color, 0);
    lv_obj_set_style_text_align(lbl_weather_util, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_grid_cell(lbl_weather_util, LV_GRID_ALIGN_CENTER, 2, 1, LV_GRID_ALIGN_CENTER, 0, 1);

    // Initial draw
    clock_update_cb(NULL);
    weather_update_cb(NULL);

    // Timers
    lv_timer_create(clock_update_cb, 60000, NULL);
    lv_timer_create(weather_update_cb, 30 * 60 * 1000, NULL);

    // Register touch event handlers (long press removed - only swipe up for settings)
    lv_obj_add_event_cb(scr, clock_screen_touch_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(scr, clock_screen_touch_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(scr, clock_screen_touch_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(scr, clock_screen_gesture_cb, LV_EVENT_GESTURE, NULL);
    ESP_LOGI(TAG, "ui_clock_init: touch event handlers registered");

    lvgl_port_unlock();

    // Set clock screen in screen manager (after unlock since it will lock again)
    screen_manager_set_clock_screen(scr);

    ESP_LOGI(TAG, "ui_clock_init: complete");
}

void ui_show_splash(void) {
    ESP_LOGI(TAG, "ui_show_splash: start - loading from SPIFFS");
    lvgl_port_lock(0);
    lv_obj_t* scr = lv_scr_act();
    lv_obj_clean(scr);
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);

    // Load splash image from SPIFFS (boot splash, always shows first)
    // Use LVGL POSIX driver prefix "A:" - maps to root filesystem
    lv_obj_t* img = lv_img_create(scr);
    lv_img_set_src(img, "B:/splash.png");  // B: drive = SPIFFS
    lv_obj_align(img, LV_ALIGN_CENTER, 0, 0);

    // Log image loading status
    const void* src = lv_img_get_src(img);
    if (src == NULL) {
        ESP_LOGW(TAG, "Splash image failed to load from SPIFFS /splash.png");
    } else {
        ESP_LOGI(TAG, "Splash image loaded from SPIFFS, source pointer: %p", src);
    }

    lvgl_port_unlock();
    ESP_LOGI(TAG, "ui_show_splash: complete");
}

// Start button callback - launches the clock UI
static void start_button_cb(lv_event_t* e) {
    ESP_LOGI(TAG, "Start button pressed - initializing clock UI");
    lvgl_port_lock(0);
    lv_obj_t* scr = lv_scr_act();
    lv_obj_clean(scr);
    lvgl_port_unlock();
    ui_clock_init(&saved_timeinfo, &saved_settings);
    
}

void ui_show_start_screen(const struct tm *timeinfo, const clock_settings_t *settings) {
    ESP_LOGI(TAG, "ui_show_start_screen: displaying bootloader screen");

    // Save parameters for when Start is clicked
    if (timeinfo) saved_timeinfo = *timeinfo;
    if (settings) saved_settings = *settings;

    lvgl_port_lock(0);
    lv_obj_t* scr = lv_scr_act();
    lv_obj_clean(scr);
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);

    // Title
    lv_obj_t* title = lv_label_create(scr);
    lv_label_set_text(title, "Greenwood Clock");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -200);

    // Info text
    lv_obj_t* info = lv_label_create(scr);
    lv_label_set_text(info, "System Ready\nOTA updates available from this screen");
    lv_obj_set_style_text_align(info, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(info, lv_color_hex(0x808080), 0);
    lv_obj_align(info, LV_ALIGN_CENTER, 0, -120);

    // OTA Server URL label
    lv_obj_t* url_label = lv_label_create(scr);
    lv_label_set_text(url_label, "OTA Server URL:");
    lv_obj_set_style_text_color(url_label, lv_color_white(), 0);
    lv_obj_align(url_label, LV_ALIGN_CENTER, 0, -60);

    // OTA Server URL text area (editable) - First instance
    lv_obj_t* ota_url_textarea_1 = lv_textarea_create(scr);
    lv_obj_set_size(ota_url_textarea_1, 520, 40);
    lv_obj_align(ota_url_textarea_1, LV_ALIGN_CENTER, -135, -20);
    lv_textarea_set_one_line(ota_url_textarea_1, true);
    lv_textarea_set_placeholder_text(ota_url_textarea_1, "http://192.168.1.96:8000");

    // Load URL from settings
    if (settings != NULL && strlen(settings->ota_server_url) > 0) {
        lv_textarea_set_text(ota_url_textarea_1, settings->ota_server_url);
    } else {
        lv_textarea_set_text(ota_url_textarea_1, "http://192.168.1.96:8000");
    }

    // OTA Server URL text area (editable) - Second instance (duplicate)
    lv_obj_t* ota_url_textarea_2 = lv_textarea_create(scr);
    lv_obj_set_size(ota_url_textarea_2, 520, 40);
    lv_obj_align(ota_url_textarea_2, LV_ALIGN_CENTER, 135, -20);
    lv_textarea_set_one_line(ota_url_textarea_2, true);
    lv_textarea_set_placeholder_text(ota_url_textarea_2, "http://192.168.1.96:8000");

    // Load URL from settings
    if (settings != NULL && strlen(settings->ota_server_url) > 0) {
        lv_textarea_set_text(ota_url_textarea_2, settings->ota_server_url);
    } else {
        lv_textarea_set_text(ota_url_textarea_2, "http://192.168.1.96:8000");
    }

    // Start button
    lv_obj_t* btn = lv_btn_create(scr);
    lv_obj_set_size(btn, 200, 60);
    lv_obj_align(btn, LV_ALIGN_CENTER, 0, 60);
    lv_obj_add_event_cb(btn, start_button_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t* btn_label = lv_label_create(btn);
    lv_label_set_text(btn_label, "Start Clock");
    lv_obj_set_style_text_font(btn_label, &lv_font_montserrat_24, 0);
    lv_obj_center(btn_label);

    // OTA Update button - First instance (left)
    lv_obj_t* ota_btn_1 = lv_btn_create(scr);
    lv_obj_set_size(ota_btn_1, 200, 60);
    lv_obj_align(ota_btn_1, LV_ALIGN_CENTER, -220, 150);
    lv_obj_add_event_cb(ota_btn_1, start_button_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t* ota_label_1 = lv_label_create(ota_btn_1);
    lv_label_set_text(ota_label_1, LV_SYMBOL_DOWNLOAD " Update");
    lv_obj_set_style_text_font(ota_label_1, &lv_font_montserrat_20, 0);
    lv_obj_center(ota_label_1);

    // OTA Update button - Second instance (right)
    lv_obj_t* ota_btn_2 = lv_btn_create(scr);
    lv_obj_set_size(ota_btn_2, 200, 60);
    lv_obj_align(ota_btn_2, LV_ALIGN_CENTER, 220, 150);
    lv_obj_add_event_cb(ota_btn_2, start_button_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t* ota_label_2 = lv_label_create(ota_btn_2);
    lv_label_set_text(ota_label_2, LV_SYMBOL_DOWNLOAD " Update");
    lv_obj_set_style_text_font(ota_label_2, &lv_font_montserrat_20, 0);
    lv_obj_center(ota_label_2);

    lvgl_port_unlock();
    ESP_LOGI(TAG, "ui_show_start_screen: complete");
}

void ui_refresh_background(void) {
    ESP_LOGI(TAG, "ui_refresh_background: start");

    if (!screen_clock) {
        ESP_LOGW(TAG, "Clock screen not initialized, cannot refresh background");
        return;
    }

    clock_settings_t cfg;
    if (settings_load(&cfg) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to load settings");
        return;
    }

    lvgl_port_lock(0);

    // Delete old background if exists
    if (bg_img) {
        lv_obj_del(bg_img);
        bg_img = NULL;
    }

    // Load new background from settings
    if (sdcard_is_mounted() && cfg.background_image[0] != '\0') {
        ESP_LOGI(TAG, "Loading new background from %s", cfg.background_image);
        
        // Check if the file is a GIF based on extension
        bool is_gif = false;
        const char* ext = strrchr(cfg.background_image, '.');
        if (ext != NULL && (strcasecmp(ext, ".gif") == 0)) {
            is_gif = true;
        }
        
        if (is_gif) {
            ESP_LOGI(TAG, "Detected GIF file, loading from %s", cfg.background_image);
            
            // Create GIF object and load directly from SD card file path
            // LVGL's frame cache (LV_GIF_CACHE_DECODE_DATA=y) handles optimization automatically
            bg_img = lv_gif_create(screen_clock);
            lv_gif_set_src(bg_img, cfg.background_image);
        } else {
            bg_img = lv_img_create(screen_clock);
            lv_img_set_src(bg_img, cfg.background_image);
        }
        
        lv_obj_align(bg_img, LV_ALIGN_CENTER, 0, 0);
        lv_obj_move_background(bg_img);
        lv_obj_clear_flag(bg_img, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(bg_img, LV_OBJ_FLAG_IGNORE_LAYOUT);
        
        // Optimize GIF if applicable (must be done after source is set)
        if (is_gif) {
            // Add small delay to ensure GIF is loaded before setting loop parameters
            vTaskDelay(pdMS_TO_TICKS(50));
            optimize_gif_for_background(bg_img);
        }

        bool loaded = is_gif ? lv_gif_is_loaded(bg_img) : (lv_img_get_src(bg_img) != NULL);
        if (!loaded) {
            ESP_LOGW(TAG, "Failed to load background from %s", cfg.background_image);
            lv_obj_del(bg_img);
            bg_img = NULL;
        } else {
            ESP_LOGI(TAG, "Background refreshed successfully");
        }
    } else {
        ESP_LOGW(TAG, "No background configured, using black");
    }

    lvgl_port_unlock();
}

void ui_refresh_text_color(void) {
    ESP_LOGI(TAG, "ui_refresh_text_color: start");

    if (!screen_clock) {
        ESP_LOGW(TAG, "Clock screen not initialized, cannot refresh text color");
        return;
    }

    clock_settings_t cfg;
    if (settings_load(&cfg) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to load settings");
        return;
    }

    lv_color_t text_color = lv_color_hex(cfg.text_color);
    ESP_LOGI(TAG, "Updating text color to 0x%06X", cfg.text_color);

    lvgl_port_lock(0);

    // Update all text labels
    if (lbl_time) lv_obj_set_style_text_color(lbl_time, text_color, LV_PART_MAIN);
    if (lbl_ampm) lv_obj_set_style_text_color(lbl_ampm, text_color, LV_PART_MAIN);
    if (lbl_date) lv_obj_set_style_text_color(lbl_date, text_color, LV_PART_MAIN);
    if (lbl_temp) lv_obj_set_style_text_color(lbl_temp, text_color, LV_PART_MAIN);
    if (lbl_weather_util) lv_obj_set_style_text_color(lbl_weather_util, text_color, LV_PART_MAIN);

    lvgl_port_unlock();
    ESP_LOGI(TAG, "Text color refreshed successfully");
}
