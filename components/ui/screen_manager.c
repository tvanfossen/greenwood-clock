// components/ui/screen_manager.c

#include "screen_manager.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_system.h"   // For esp_restart()
#include "bsp/display.h"  // For brightness control
#include "esp_mac.h"      // For MAC address
#include "esp_netif.h"    // For IP address
#include "network.h"      // For WiFi scanning
#include "settings.h"     // For persistent settings
#include "ota.h"          // For OTA updates
#include "sdcard.h"       // For SD card access
#include "freertos/FreeRTOS.h"  // For vTaskDelay and pdMS_TO_TICKS
#include "freertos/task.h"
#include <string.h>
#include <dirent.h>       // For directory scanning

static const char* TAG = "screen_mgr";

#define MAX_SCREEN_STACK 8

static lv_obj_t* screens[SCREEN_MAX] = {NULL};
static screen_id_t screen_stack[MAX_SCREEN_STACK];
static int stack_top = -1;

// Forward declarations for screen creation functions
static lv_obj_t* create_settings_menu(void);
static lv_obj_t* create_wifi_settings(void);
static lv_obj_t* create_brightness_settings(void);
static lv_obj_t* create_background_selector(void);
static lv_obj_t* create_ota_settings(void);
static lv_obj_t* create_animation_preview(void);
static lv_obj_t* create_about_screen(void);

// Forward declarations for callbacks
static void wifi_scan_btn_cb(lv_event_t* e);
static void wifi_network_select_cb(lv_event_t* e);
static void wifi_connect_btn_cb(lv_event_t* e);

void screen_manager_init(void) {
    ESP_LOGI(TAG, "Screen manager initialized");
    stack_top = -1;

    // Clock screen is already created by ui_clock_init()
    // We'll get it when needed
    screens[SCREEN_CLOCK] = NULL;  // Will be set by ui module

    // TODO: Pre-create settings menu to avoid stutter on first swipe
    // Currently disabled for debugging - may cause crash during init
    // ESP_LOGI(TAG, "Pre-creating settings menu...");
    // screens[SCREEN_SETTINGS_MENU] = create_settings_menu();
    // ESP_LOGI(TAG, "Settings menu pre-created");
}

static void push_stack(screen_id_t screen) {
    if (stack_top < MAX_SCREEN_STACK - 1) {
        stack_top++;
        screen_stack[stack_top] = screen;
        ESP_LOGI(TAG, "Pushed screen %d to stack (depth: %d)", screen, stack_top + 1);
    } else {
        ESP_LOGW(TAG, "Screen stack overflow!");
    }
}

static screen_id_t pop_stack(void) {
    if (stack_top >= 0) {
        screen_id_t screen = screen_stack[stack_top];
        stack_top--;
        ESP_LOGI(TAG, "Popped screen %d from stack (depth: %d)", screen, stack_top + 1);
        return screen;
    }
    ESP_LOGW(TAG, "Screen stack underflow!");
    return SCREEN_CLOCK;
}

static lv_obj_t* get_or_create_screen(screen_id_t screen) {
    if (screens[screen] != NULL) {
        return screens[screen];
    }

    // Create screen on demand
    ESP_LOGI(TAG, "Creating screen %d", screen);

    switch (screen) {
        case SCREEN_SETTINGS_MENU:
            screens[screen] = create_settings_menu();
            break;
        case SCREEN_WIFI_SETTINGS:
            screens[screen] = create_wifi_settings();
            break;
        case SCREEN_BRIGHTNESS_SETTINGS:
            screens[screen] = create_brightness_settings();
            break;
        case SCREEN_BACKGROUND_SELECTOR:
            screens[screen] = create_background_selector();
            break;
        case SCREEN_OTA_SETTINGS:
            screens[screen] = create_ota_settings();
            break;
        case SCREEN_ANIMATION_PREVIEW:
            screens[screen] = create_animation_preview();
            break;
        case SCREEN_ABOUT:
            screens[screen] = create_about_screen();
            break;
        case SCREEN_CLOCK:
            // Clock screen is created separately
            ESP_LOGW(TAG, "Clock screen should be set externally");
            break;
        default:
            ESP_LOGE(TAG, "Unknown screen ID: %d", screen);
            break;
    }

    return screens[screen];
}

void screen_manager_set_clock_screen(lv_obj_t* scr) {
    screens[SCREEN_CLOCK] = scr;
    push_stack(SCREEN_CLOCK);
    ESP_LOGI(TAG, "Clock screen set");
}

void screen_manager_push(screen_id_t screen) {
    if (screen >= SCREEN_MAX) {
        ESP_LOGE(TAG, "Invalid screen ID: %d", screen);
        return;
    }

    ESP_LOGI(TAG, "Navigating to screen %d", screen);

    lv_obj_t* scr = get_or_create_screen(screen);
    if (scr == NULL) {
        ESP_LOGE(TAG, "Failed to create screen %d", screen);
        return;
    }

    push_stack(screen);

    lvgl_port_lock(0);
    // Slide over from bottom - smoother than push animation
    lv_scr_load_anim(scr, LV_SCREEN_LOAD_ANIM_OVER_TOP, 350, 0, false);
    lvgl_port_unlock();
}

void screen_manager_pop(void) {
    if (stack_top <= 0) {
        ESP_LOGW(TAG, "Already at root screen, cannot pop");
        return;
    }

    pop_stack();  // Remove current screen
    screen_id_t prev_screen = screen_stack[stack_top];  // Get previous screen

    ESP_LOGI(TAG, "Going back to screen %d", prev_screen);

    lv_obj_t* scr = get_or_create_screen(prev_screen);
    if (scr == NULL) {
        ESP_LOGE(TAG, "Failed to get screen %d", prev_screen);
        return;
    }

    lvgl_port_lock(0);
    // Slide down to match reverse of swipe up gesture - smooth 500ms movement
    lv_scr_load_anim(scr, LV_SCREEN_LOAD_ANIM_MOVE_BOTTOM, 500, 0, false);
    lvgl_port_unlock();
}

void screen_manager_home(void) {
    if (stack_top == 0) {
        ESP_LOGI(TAG, "Already at home screen");
        return;
    }

    ESP_LOGI(TAG, "Returning to home screen");
    stack_top = 0;  // Reset to home

    lv_obj_t* scr = screens[SCREEN_CLOCK];
    if (scr == NULL) {
        ESP_LOGE(TAG, "Clock screen not set!");
        return;
    }

    lvgl_port_lock(0);
    // Smooth fade for home transition
    lv_scr_load_anim(scr, LV_SCREEN_LOAD_ANIM_FADE_IN, 400, 0, false);
    lvgl_port_unlock();
}

screen_id_t screen_manager_get_current(void) {
    if (stack_top >= 0) {
        return screen_stack[stack_top];
    }
    return SCREEN_CLOCK;
}

// =============================================================================
// Screen creation functions
// =============================================================================

// Back button callback
static void back_button_cb(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        ESP_LOGI(TAG, "Back button clicked");
        screen_manager_pop();
    }
}

// Create a standard back button
static lv_obj_t* create_back_button(lv_obj_t* parent) {
    lv_obj_t* btn = lv_btn_create(parent);
    lv_obj_set_size(btn, 100, 50);
    lv_obj_align(btn, LV_ALIGN_TOP_LEFT, 10, 10);
    lv_obj_add_event_cb(btn, back_button_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t* label = lv_label_create(btn);
    lv_label_set_text(label, "< Back");
    lv_obj_center(label);

    return btn;
}

// Settings menu screen
static void settings_menu_item_cb(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        screen_id_t* screen_id = (screen_id_t*)lv_event_get_user_data(e);
        ESP_LOGI(TAG, "Settings item clicked: %d", *screen_id);
        screen_manager_push(*screen_id);
    }
}

static screen_id_t wifi_screen_id = SCREEN_WIFI_SETTINGS;
static screen_id_t brightness_screen_id = SCREEN_BRIGHTNESS_SETTINGS;
static screen_id_t background_selector_screen_id = SCREEN_BACKGROUND_SELECTOR;
static screen_id_t ota_screen_id = SCREEN_OTA_SETTINGS;
static screen_id_t animation_preview_screen_id = SCREEN_ANIMATION_PREVIEW;
static screen_id_t about_screen_id = SCREEN_ABOUT;

// Reboot button callback
static void reboot_btn_cb(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        ESP_LOGI(TAG, "Reboot button clicked - initiating system restart");
        
        // Give LVGL time to update
        lvgl_port_unlock();
        
        // Wait a moment for UI to settle
        vTaskDelay(pdMS_TO_TICKS(500));
        
        // Perform system restart
        esp_restart();
    }
}

static lv_obj_t* create_settings_menu(void) {
    lv_obj_t* scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);

    // Title
    lv_obj_t* title = lv_label_create(scr);
    lv_label_set_text(title, "Settings");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);

    // Back button
    create_back_button(scr);

    // Create a list of settings options
    lv_obj_t* list = lv_list_create(scr);
    lv_obj_set_size(list, 700, 500);
    lv_obj_align(list, LV_ALIGN_CENTER, 0, 30);

    // Set larger font for list items
    lv_obj_set_style_text_font(list, &lv_font_montserrat_24, 0);

    // WiFi Settings
    lv_obj_t* btn_wifi = lv_list_add_btn(list, LV_SYMBOL_WIFI, "WiFi Settings");
    lv_obj_add_event_cb(btn_wifi, settings_menu_item_cb, LV_EVENT_CLICKED, &wifi_screen_id);

    // Brightness Settings
    lv_obj_t* btn_brightness = lv_list_add_btn(list, LV_SYMBOL_IMAGE, "Brightness");
    lv_obj_add_event_cb(btn_brightness, settings_menu_item_cb, LV_EVENT_CLICKED, &brightness_screen_id);

    // Background Image
    lv_obj_t* btn_background = lv_list_add_btn(list, LV_SYMBOL_IMAGE, "Background Image");
    lv_obj_add_event_cb(btn_background, settings_menu_item_cb, LV_EVENT_CLICKED, &background_selector_screen_id);

    // Software Update (OTA)
    lv_obj_t* btn_ota = lv_list_add_btn(list, LV_SYMBOL_DOWNLOAD, "Software Update");
    lv_obj_add_event_cb(btn_ota, settings_menu_item_cb, LV_EVENT_CLICKED, &ota_screen_id);

    // Animation Preview
    lv_obj_t* btn_animation = lv_list_add_btn(list, LV_SYMBOL_PLAY, "Animation Preview");
    lv_obj_add_event_cb(btn_animation, settings_menu_item_cb, LV_EVENT_CLICKED, &animation_preview_screen_id);

    // About
    lv_obj_t* btn_about = lv_list_add_btn(list, LV_SYMBOL_SETTINGS, "About");
    lv_obj_add_event_cb(btn_about, settings_menu_item_cb, LV_EVENT_CLICKED, &about_screen_id);

    // Reboot Device
    lv_obj_t* btn_reboot = lv_list_add_btn(list, LV_SYMBOL_POWER, "Reboot Device");
    lv_obj_add_event_cb(btn_reboot, reboot_btn_cb, LV_EVENT_CLICKED, NULL);

    return scr;
}

// WiFi configuration state
static char selected_ssid[33] = "";
static char wifi_password[64] = "";
static lv_obj_t* wifi_keyboard = NULL;
static lv_obj_t* password_textarea = NULL;
static lv_obj_t* wifi_list = NULL;
static lv_obj_t* wifi_status_label = NULL;

// WiFi scan button callback
static void wifi_scan_btn_cb(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code != LV_EVENT_CLICKED) return;

    ESP_LOGI(TAG, "WiFi scan button clicked");

    // Clear existing list
    if (wifi_list != NULL) {
        lv_obj_clean(wifi_list);
    }

    // Update status
    if (wifi_status_label) {
        lv_label_set_text(wifi_status_label, "Scanning...");
    }

    // Perform scan (this blocks, but LVGL will update after)
    wifi_ap_info_t ap_list[20];
    uint16_t found = 0;

    // Unlock LVGL before network operation
    lvgl_port_unlock();
    esp_err_t err = network_scan(ap_list, 20, &found);
    lvgl_port_lock(0);

    if (err != ESP_OK || found == 0) {
        ESP_LOGW(TAG, "WiFi scan failed or no networks found");
        if (wifi_status_label) {
            lv_label_set_text(wifi_status_label, "No networks found");
        }
        return;
    }

    ESP_LOGI(TAG, "Found %d networks", found);
    if (wifi_status_label) {
        char buf[64];
        snprintf(buf, sizeof(buf), "Found %d networks", found);
        lv_label_set_text(wifi_status_label, buf);
    }

    // Add networks to list
    for (uint16_t i = 0; i < found; i++) {
        char btn_text[64];
        const char* lock_icon = (ap_list[i].authmode != WIFI_AUTH_OPEN) ? "* " : "";
        snprintf(btn_text, sizeof(btn_text), "%s%s (%d dBm)",
                 lock_icon, ap_list[i].ssid, ap_list[i].rssi);

        lv_obj_t* btn = lv_list_add_button(wifi_list, NULL, btn_text);

        // Store SSID in user data
        char* ssid_copy = (char*)malloc(33);
        strncpy(ssid_copy, ap_list[i].ssid, 32);
        ssid_copy[32] = '\0';
        lv_obj_set_user_data(btn, ssid_copy);

        // Add click handler
        lv_obj_add_event_cb(btn, wifi_network_select_cb, LV_EVENT_CLICKED, NULL);
    }
}

// Network selection callback
static void wifi_network_select_cb(lv_event_t* e) {
    lv_obj_t* btn = lv_event_get_target(e);
    char* ssid = (char*)lv_obj_get_user_data(btn);

    if (ssid == NULL) return;

    ESP_LOGI(TAG, "Selected network: %s", ssid);
    strncpy(selected_ssid, ssid, sizeof(selected_ssid) - 1);
    selected_ssid[sizeof(selected_ssid) - 1] = '\0';

    // Show password input
    if (password_textarea && wifi_keyboard) {
        lv_obj_clear_flag(password_textarea, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(wifi_keyboard, LV_OBJ_FLAG_HIDDEN);
        lv_textarea_set_text(password_textarea, "");

        if (wifi_status_label) {
            char buf[64];
            snprintf(buf, sizeof(buf), "Enter password for: %s", selected_ssid);
            lv_label_set_text(wifi_status_label, buf);
        }
    }
}

// WiFi connect button callback
static void wifi_connect_btn_cb(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code != LV_EVENT_CLICKED) return;

    if (selected_ssid[0] == '\0') {
        ESP_LOGW(TAG, "No network selected");
        return;
    }

    // Get password from textarea
    const char* pwd = lv_textarea_get_text(password_textarea);
    strncpy(wifi_password, pwd, sizeof(wifi_password) - 1);
    wifi_password[sizeof(wifi_password) - 1] = '\0';

    ESP_LOGI(TAG, "Connecting to %s...", selected_ssid);

    if (wifi_status_label) {
        lv_label_set_text(wifi_status_label, "Connecting...");
    }

    // Hide keyboard and password field
    if (password_textarea && wifi_keyboard) {
        lv_obj_add_flag(password_textarea, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(wifi_keyboard, LV_OBJ_FLAG_HIDDEN);
    }

    // Connect (unlock LVGL first)
    lvgl_port_unlock();
    esp_err_t err = network_connect(selected_ssid, wifi_password);

    if (err == ESP_OK) {
        // Save credentials to NVS
        clock_settings_t cfg;
        settings_load(&cfg);
        strncpy(cfg.wifi_ssid, selected_ssid, sizeof(cfg.wifi_ssid));
        cfg.wifi_ssid[sizeof(cfg.wifi_ssid) - 1] = '\0';
        strncpy(cfg.wifi_password, wifi_password, sizeof(cfg.wifi_password));
        cfg.wifi_password[sizeof(cfg.wifi_password) - 1] = '\0';
        cfg.wifi_configured = true;
        settings_save(&cfg);
        ESP_LOGI(TAG, "WiFi credentials saved");
    }

    lvgl_port_lock(0);

    if (wifi_status_label) {
        if (err == ESP_OK) {
            lv_label_set_text(wifi_status_label, "Connected! Saved to settings.");
        } else {
            lv_label_set_text(wifi_status_label, "Connection failed");
        }
    }
}

// WiFi settings screen
static lv_obj_t* create_wifi_settings(void) {
    lv_obj_t* scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);

    // Title
    lv_obj_t* title = lv_label_create(scr);
    lv_label_set_text(title, "WiFi Settings");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);

    // Back button
    create_back_button(scr);

    // Status label
    wifi_status_label = lv_label_create(scr);
    char ssid[33] = "";
    if (network_is_connected() && network_get_ssid(ssid) == ESP_OK) {
        char buf[128];
        snprintf(buf, sizeof(buf), "Connected to: %s", ssid);
        lv_label_set_text(wifi_status_label, buf);
    } else {
        lv_label_set_text(wifi_status_label, "Not connected");
    }
    lv_obj_set_style_text_color(wifi_status_label, lv_color_white(), 0);
    lv_obj_align(wifi_status_label, LV_ALIGN_TOP_MID, 0, 70);

    // Scan button
    lv_obj_t* scan_btn = lv_button_create(scr);
    lv_obj_set_size(scan_btn, 200, 50);
    lv_obj_align(scan_btn, LV_ALIGN_TOP_MID, 0, 110);
    lv_obj_add_event_cb(scan_btn, wifi_scan_btn_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t* scan_label = lv_label_create(scan_btn);
    lv_label_set_text(scan_label, LV_SYMBOL_REFRESH " Scan Networks");
    lv_obj_center(scan_label);

    // Network list
    wifi_list = lv_list_create(scr);
    lv_obj_set_size(wifi_list, 700, 250);
    lv_obj_align(wifi_list, LV_ALIGN_CENTER, 0, 20);

    // Password input (hidden by default)
    password_textarea = lv_textarea_create(scr);
    lv_obj_set_size(password_textarea, 600, 50);
    lv_obj_align(password_textarea, LV_ALIGN_BOTTOM_MID, 0, -280);
    lv_textarea_set_placeholder_text(password_textarea, "Enter password...");
    lv_textarea_set_password_mode(password_textarea, true);
    lv_obj_add_flag(password_textarea, LV_OBJ_FLAG_HIDDEN);

    // Keyboard (hidden by default)
    wifi_keyboard = lv_keyboard_create(scr);
    lv_keyboard_set_textarea(wifi_keyboard, password_textarea);
    lv_obj_set_size(wifi_keyboard, 750, 220);
    lv_obj_align(wifi_keyboard, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_add_flag(wifi_keyboard, LV_OBJ_FLAG_HIDDEN);

    // Connect button (always visible, next to password field)
    lv_obj_t* connect_btn = lv_button_create(scr);
    lv_obj_set_size(connect_btn, 120, 50);
    lv_obj_align_to(connect_btn, password_textarea, LV_ALIGN_OUT_RIGHT_MID, 10, 0);
    lv_obj_add_event_cb(connect_btn, wifi_connect_btn_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t* connect_label = lv_label_create(connect_btn);
    lv_label_set_text(connect_label, "Connect");
    lv_obj_center(connect_label);

    return scr;
}

// Brightness slider callback
static void brightness_slider_cb(lv_event_t* e) {
    lv_obj_t* slider = lv_event_get_target(e);
    int32_t value = lv_slider_get_value(slider);

    ESP_LOGI(TAG, "Brightness changed to: %ld%%", value);
    bsp_display_brightness_set(value);

    // Update label
    lv_obj_t* label = (lv_obj_t*)lv_event_get_user_data(e);
    if (label) {
        char buf[32];
        snprintf(buf, sizeof(buf), "Brightness: %ld%%", value);
        lv_label_set_text(label, buf);
    }

    // Save to settings (unlock LVGL first)
    lvgl_port_unlock();
    clock_settings_t cfg;
    if (settings_load(&cfg) == ESP_OK) {
        cfg.brightness = (uint8_t)value;
        esp_err_t err = settings_save(&cfg);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "Brightness setting saved");
        } else {
            ESP_LOGW(TAG, "Failed to save brightness: %s", esp_err_to_name(err));
        }
    }
    lvgl_port_lock(0);
}

// Brightness settings screen
static lv_obj_t* create_brightness_settings(void) {
    lv_obj_t* scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);

    // Title
    lv_obj_t* title = lv_label_create(scr);
    lv_label_set_text(title, "Brightness");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);

    // Back button
    create_back_button(scr);

    // Brightness label
    lv_obj_t* label = lv_label_create(scr);
    lv_label_set_text(label, "Brightness: 50%");
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_24, 0);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, -100);

    // Brightness slider
    lv_obj_t* slider = lv_slider_create(scr);
    lv_obj_set_width(slider, 600);
    lv_slider_set_range(slider, 10, 100);  // 10% to 100%
    lv_slider_set_value(slider, 50, LV_ANIM_OFF);  // Default to 50%
    lv_obj_align(slider, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_event_cb(slider, brightness_slider_cb, LV_EVENT_VALUE_CHANGED, label);

    // Instructions
    lv_obj_t* instr = lv_label_create(scr);
    lv_label_set_text(instr, "Slide to adjust screen brightness");
    lv_obj_set_style_text_color(instr, lv_color_white(), 0);
    lv_obj_align(instr, LV_ALIGN_CENTER, 0, 100);

    return scr;
}

// Background selector callback
static void background_select_cb(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code != LV_EVENT_CLICKED) return;

    // Get the selected filename from user data
    const char* filename = (const char*)lv_event_get_user_data(e);
    if (filename == NULL) return;

    ESP_LOGI(TAG, "Background selected: %s", filename);

    // Load current settings
    clock_settings_t cfg;
    if (settings_load(&cfg) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to load settings");
        return;
    }

    // Update background image path
    snprintf(cfg.background_image, sizeof(cfg.background_image), "A:%s", filename);

    // Save settings
    if (settings_save(&cfg) == ESP_OK) {
        ESP_LOGI(TAG, "Background saved to settings: %s", cfg.background_image);

        // Show confirmation message
        lv_obj_t* mbox = lv_msgbox_create(NULL);
        lv_msgbox_add_title(mbox, "Success");
        lv_msgbox_add_text(mbox, "Background updated!\nRestart to apply changes.");
        lv_msgbox_add_close_button(mbox);
        lv_obj_center(mbox);

        // Go back after a delay
        vTaskDelay(pdMS_TO_TICKS(2000));
        screen_manager_pop();
    } else {
        ESP_LOGE(TAG, "Failed to save settings");
    }
}

// Background selector screen
static lv_obj_t* create_background_selector(void) {
    lv_obj_t* scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);

    // Title
    lv_obj_t* title = lv_label_create(scr);
    lv_label_set_text(title, "Select Background");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);

    // Back button
    create_back_button(scr);

    // Create list for PNG files
    lv_obj_t* list = lv_list_create(scr);
    lv_obj_set_size(list, 700, 500);
    lv_obj_align(list, LV_ALIGN_CENTER, 0, 30);
    lv_obj_set_style_text_font(list, &lv_font_montserrat_20, 0);

    // Scan SD card for PNG files
    if (!sdcard_is_mounted()) {
        lv_obj_t* btn = lv_list_add_btn(list, LV_SYMBOL_WARNING, "SD card not mounted");
        lv_obj_clear_flag(btn, LV_OBJ_FLAG_CLICKABLE);
        return scr;
    }

    DIR* dir = opendir("/sdcard");
    if (dir == NULL) {
        ESP_LOGE(TAG, "Failed to open /sdcard directory");
        lv_obj_t* btn = lv_list_add_btn(list, LV_SYMBOL_WARNING, "Failed to read SD card");
        lv_obj_clear_flag(btn, LV_OBJ_FLAG_CLICKABLE);
        return scr;
    }

    struct dirent* entry;
    int count = 0;
    while ((entry = readdir(dir)) != NULL) {
        // Skip directories and non-PNG files
        if (entry->d_type == DT_DIR) continue;

        const char* ext = strrchr(entry->d_name, '.');
        if (ext == NULL || strcasecmp(ext, ".png") != 0) continue;

        // Create full path
        static char filepath[256];  // Static to keep it alive for callback
        snprintf(filepath, sizeof(filepath), "/sdcard/%s", entry->d_name);

        // Add to list
        lv_obj_t* btn = lv_list_add_btn(list, LV_SYMBOL_IMAGE, entry->d_name);
        lv_obj_add_event_cb(btn, background_select_cb, LV_EVENT_CLICKED, (void*)strdup(filepath));

        count++;
    }
    closedir(dir);

    if (count == 0) {
        lv_obj_t* btn = lv_list_add_btn(list, LV_SYMBOL_WARNING, "No PNG files found");
        lv_obj_clear_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    } else {
        ESP_LOGI(TAG, "Found %d PNG files on SD card", count);
    }

    return scr;
}

// OTA Update screen
static lv_obj_t* ota_status_label = NULL;
static lv_obj_t* ota_progress_bar = NULL;
static lv_obj_t* ota_update_btn = NULL;
static lv_obj_t* ota_url_textarea = NULL;
static lv_obj_t* ota_keyboard = NULL;

static void ota_progress_callback(const ota_status_t* status, void* user_data) {
    ESP_LOGI(TAG, "OTA Progress: state=%d, progress=%d%%", status->state, status->progress_percent);

    lvgl_port_lock(0);

    if (ota_status_label) {
        switch (status->state) {
            case OTA_STATE_CHECKING:
                lv_label_set_text(ota_status_label, "Checking for updates...");
                break;
            case OTA_STATE_DOWNLOADING:
                lv_label_set_text_fmt(ota_status_label, "Downloading: %d%% (%zu/%zu bytes)",
                                      status->progress_percent,
                                      status->downloaded_bytes,
                                      status->total_bytes);
                break;
            case OTA_STATE_VERIFYING:
                lv_label_set_text(ota_status_label, "Verifying firmware...");
                break;
            case OTA_STATE_SUCCESS:
                lv_label_set_text(ota_status_label, "Update successful! Rebooting...");
                break;
            case OTA_STATE_ERROR:
                lv_label_set_text_fmt(ota_status_label, "Error: %s", status->error_msg);
                break;
            default:
                break;
        }
    }

    if (ota_progress_bar) {
        lv_bar_set_value(ota_progress_bar, status->progress_percent, LV_ANIM_OFF);
    }

    lvgl_port_unlock();
}

static void ota_save_url_btn_cb(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code != LV_EVENT_CLICKED) return;

    if (!ota_url_textarea) return;

    // Get URL from textarea
    const char* url = lv_textarea_get_text(ota_url_textarea);
    if (!url || strlen(url) == 0) {
        ESP_LOGW(TAG, "OTA URL is empty, not saving");
        return;
    }

    ESP_LOGI(TAG, "Saving OTA server URL: %s", url);

    // Save to settings
    lvgl_port_unlock();
    clock_settings_t cfg;
    if (settings_load(&cfg) == ESP_OK) {
        strlcpy(cfg.ota_server_url, url, sizeof(cfg.ota_server_url));
        esp_err_t err = settings_save(&cfg);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "OTA server URL saved");
        } else {
            ESP_LOGW(TAG, "Failed to save OTA URL: %s", esp_err_to_name(err));
        }
    }
    lvgl_port_lock(0);

    // Hide keyboard
    if (ota_keyboard) {
        lv_obj_add_flag(ota_keyboard, LV_OBJ_FLAG_HIDDEN);
    }
}

static void ota_url_focused_cb(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_FOCUSED && ota_keyboard) {
        lv_obj_clear_flag(ota_keyboard, LV_OBJ_FLAG_HIDDEN);
    } else if (code == LV_EVENT_DEFOCUSED && ota_keyboard) {
        lv_obj_add_flag(ota_keyboard, LV_OBJ_FLAG_HIDDEN);
    }
}

static void ota_update_btn_cb(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code != LV_EVENT_CLICKED) return;

    if (!ota_url_textarea) return;

    // Get URL from textarea
    const char* url = lv_textarea_get_text(ota_url_textarea);
    if (!url || strlen(url) == 0) {
        ESP_LOGW(TAG, "OTA URL is empty");
        if (ota_status_label) {
            lvgl_port_lock(0);
            lv_label_set_text(ota_status_label, "Error: Server URL is empty");
            lvgl_port_unlock();
        }
        return;
    }

    ESP_LOGI(TAG, "OTA update button clicked, using URL: %s", url);

    // Disable button during update
    if (ota_update_btn) {
        lv_obj_add_state(ota_update_btn, LV_STATE_DISABLED);
    }

    // Start OTA update in background (will reboot on success)
    lvgl_port_unlock();
    esp_err_t err = ota_perform_update(url, ota_progress_callback, NULL);
    lvgl_port_lock(0);

    if (err != ESP_OK) {
        // Re-enable button if update failed
        if (ota_update_btn) {
            lv_obj_clear_state(ota_update_btn, LV_STATE_DISABLED);
        }
    }
}

static lv_obj_t* create_ota_settings(void) {
    lv_obj_t* scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);

    // Title
    lv_obj_t* title = lv_label_create(scr);
    lv_label_set_text(title, "Software Update");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);

    // Back button
    create_back_button(scr);

    // Current version info
    lv_obj_t* version_label = lv_label_create(scr);
    lv_label_set_text_fmt(version_label, "Current Version: %s\nPartition: %s",
                          ota_get_current_version(),
                          ota_get_running_partition());
    lv_obj_set_style_text_color(version_label, lv_color_white(), 0);
    lv_obj_set_style_text_align(version_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(version_label, LV_ALIGN_TOP_MID, 0, 80);

    // Server URL label
    lv_obj_t* url_label = lv_label_create(scr);
    lv_label_set_text(url_label, "Server URL:");
    lv_obj_set_style_text_color(url_label, lv_color_white(), 0);
    lv_obj_align(url_label, LV_ALIGN_TOP_LEFT, 160, 160);

    // Server URL text area (editable)
    ota_url_textarea = lv_textarea_create(scr);
    lv_obj_set_size(ota_url_textarea, 520, 50);
    lv_obj_align(ota_url_textarea, LV_ALIGN_TOP_MID, 0, 190);
    lv_textarea_set_one_line(ota_url_textarea, true);
    lv_textarea_set_placeholder_text(ota_url_textarea, "http://192.168.1.96:8000");

    // Load URL from settings
    clock_settings_t cfg;
    if (settings_load(&cfg) == ESP_OK && strlen(cfg.ota_server_url) > 0) {
        lv_textarea_set_text(ota_url_textarea, cfg.ota_server_url);
    } else {
        lv_textarea_set_text(ota_url_textarea, "http://192.168.1.96:8000");
    }

    lv_obj_add_event_cb(ota_url_textarea, ota_url_focused_cb, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(ota_url_textarea, ota_url_focused_cb, LV_EVENT_DEFOCUSED, NULL);

    // Save URL button
    lv_obj_t* save_url_btn = lv_btn_create(scr);
    lv_obj_set_size(save_url_btn, 120, 50);
    lv_obj_align_to(save_url_btn, ota_url_textarea, LV_ALIGN_OUT_RIGHT_MID, 10, 0);
    lv_obj_add_event_cb(save_url_btn, ota_save_url_btn_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t* save_label = lv_label_create(save_url_btn);
    lv_label_set_text(save_label, "Save");
    lv_obj_center(save_label);

    // Keyboard for URL editing (hidden by default)
    ota_keyboard = lv_keyboard_create(scr);
    lv_keyboard_set_textarea(ota_keyboard, ota_url_textarea);
    lv_obj_set_size(ota_keyboard, 750, 220);
    lv_obj_align(ota_keyboard, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_add_flag(ota_keyboard, LV_OBJ_FLAG_HIDDEN);

    // Update button
    ota_update_btn = lv_btn_create(scr);
    lv_obj_set_size(ota_update_btn, 300, 60);
    lv_obj_align(ota_update_btn, LV_ALIGN_CENTER, 0, -50);
    lv_obj_add_event_cb(ota_update_btn, ota_update_btn_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t* btn_label = lv_label_create(ota_update_btn);
    lv_label_set_text(btn_label, LV_SYMBOL_DOWNLOAD " Check for Update");
    lv_obj_center(btn_label);

    // Status label
    ota_status_label = lv_label_create(scr);
    lv_label_set_text(ota_status_label, "Enter server URL and press update");
    lv_obj_set_style_text_color(ota_status_label, lv_color_white(), 0);
    lv_obj_set_style_text_align(ota_status_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(ota_status_label, 600);
    lv_obj_align(ota_status_label, LV_ALIGN_CENTER, 0, 30);

    // Progress bar
    ota_progress_bar = lv_bar_create(scr);
    lv_obj_set_size(ota_progress_bar, 600, 30);
    lv_obj_align(ota_progress_bar, LV_ALIGN_CENTER, 0, 80);
    lv_bar_set_value(ota_progress_bar, 0, LV_ANIM_OFF);

    // Warning label
    lv_obj_t* warning = lv_label_create(scr);
    lv_label_set_text(warning, "Warning: Do not power off during update!");
    lv_obj_set_style_text_color(warning, lv_color_make(255, 100, 100), 0);
    lv_obj_set_style_text_align(warning, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(warning, LV_ALIGN_CENTER, 0, 130);

    return scr;
}

// Animation Preview screen
// Note: Large animations removed to save flash space
// TODO: Load animations from SPIFFS or SD card instead

static lv_obj_t* create_animation_preview(void) {
    lv_obj_t* scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);

    // Title
    lv_obj_t* title = lv_label_create(scr);
    lv_label_set_text(title, "Animation Preview");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);

    // Back button
    create_back_button(scr);

    // Info label
    lv_obj_t* info_label = lv_label_create(scr);
    lv_obj_set_style_text_color(info_label, lv_color_white(), 0);
    lv_obj_set_style_text_align(info_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(info_label, 700);
    lv_obj_align(info_label, LV_ALIGN_TOP_MID, 0, 80);

    // Check memory before attempting to load animation
    size_t free_heap_before = esp_get_free_heap_size();
    ESP_LOGI(TAG, "Animation Preview - Free heap before: %lu bytes", (unsigned long)free_heap_before);

#if LV_USE_LOTTIE
    // Only proceed if we have enough memory (need ~160KB for buffer + animation overhead)
    if (free_heap_before < 500000) {  // 500 KB minimum
        ESP_LOGW(TAG, "Not enough memory for animation preview");
        lv_label_set_text(info_label,
            "Insufficient memory for animation\n\n"
            "Animation requires ~500 KB free heap\n"
            "Current free: < 500 KB");
        lv_obj_set_style_text_color(info_label, lv_color_make(255, 100, 100), 0);
        return scr;
    }

    lv_label_set_text(info_label,
        "Loading animation from SPIFFS...\n\n"
        "This is a safe test environment.\n"
        "If the animation crashes, the device\n"
        "will reboot to the main screen.");

    // Create Lottie animation widget
    lv_obj_t* lottie_anim = lv_lottie_create(scr);
    if (!lottie_anim) {
        ESP_LOGE(TAG, "Failed to create Lottie widget");
        lv_label_set_text(info_label, "Failed to create animation widget");
        lv_obj_set_style_text_color(info_label, lv_color_make(255, 100, 100), 0);
        return scr;
    }

    // Set explicit size for the animation widget
    lv_obj_set_size(lottie_anim, 200, 200);

    // Allocate buffer for animation using RGB565 format (2 bytes/pixel) for better compatibility
    // This avoids color format conversion warnings during rendering
    static uint8_t lottie_buf[200 * 200 * 2];  // ~80 KB for RGB565
    lv_lottie_set_buffer(lottie_anim, 200, 200, lottie_buf);
    
    // Set buffer format to RGB565 to match display and avoid conversion warnings
    lv_obj_set_style_img_recolor_opa(lottie_anim, LV_OPA_0, 0);  // Disable recolor to preserve animation colors

    // Load animation from SD card file (function returns void - errors logged internally by LVGL)
    ESP_LOGI(TAG, "Loading Lottie animation from A:/sdcard/hummingbird.json");
    lv_lottie_set_src_file(lottie_anim, "A:/sdcard/hummingbird.json");

    // Check if animation loaded by examining the object
    lv_anim_t * anim = lv_lottie_get_anim(lottie_anim);
    if (anim != NULL) {
        ESP_LOGI(TAG, "Lottie animation loaded successfully");
    } else {
        ESP_LOGW(TAG, "Lottie animation may not have loaded - check SD card mount and A:/sdcard/hummingbird.json file");
    }

    // Center the animation
    lv_obj_center(lottie_anim);

    // Check memory after loading
    size_t free_heap_after = esp_get_free_heap_size();
    size_t memory_used = free_heap_before - free_heap_after;

    ESP_LOGI(TAG, "Animation loaded from SD card successfully");
    ESP_LOGI(TAG, "Free heap after: %lu bytes", (unsigned long)free_heap_after);
    ESP_LOGI(TAG, "Memory used: %lu bytes (~%lu KB)",
             (unsigned long)memory_used,
             (unsigned long)(memory_used / 1024));

    // Update info label with success message
    char info_text[256];
    snprintf(info_text, sizeof(info_text),
        "Animation loaded successfully!\n\n"
        "Source: A:/sdcard/hummingbird.json\n"
        "Memory used: ~%lu KB\n"
        "Free heap: %lu KB\n\n"
        "Animation: Hummingbird (200x200px)",
        (unsigned long)(memory_used / 1024),
        (unsigned long)(free_heap_after / 1024));

    lv_label_set_text(info_label, info_text);
    lv_obj_set_style_text_color(info_label, lv_color_make(100, 255, 100), 0);  // Green

    ESP_LOGI(TAG, "Animation preview ready - loaded from SPIFFS");
#else
    lv_label_set_text(info_label,
        "Lottie animations not enabled\n\n"
        "CONFIG_LV_USE_LOTTIE is not set");
    lv_obj_set_style_text_color(info_label, lv_color_make(255, 200, 100), 0);
#endif

    return scr;
}

// About screen
static lv_obj_t* create_about_screen(void) {
    lv_obj_t* scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);

    // Title
    lv_obj_t* title = lv_label_create(scr);
    lv_label_set_text(title, "About");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);

    // Back button
    create_back_button(scr);

    // Get system information
    size_t free_heap = esp_get_free_heap_size();
    size_t min_heap = esp_get_minimum_free_heap_size();
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);

    // Get IP address
    esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t ip_info;
    char ip_str[16] = "N/A";
    if (netif && esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
        snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&ip_info.ip));
    }

    // Build info string
    char info[512];
    snprintf(info, sizeof(info),
        "Greenwood Clock\n"
        "Version: 1.0.0\n\n"
        "Platform: ESP32-P4\n"
        "Board: Function EV Board\n\n"
        "MAC: %02X:%02X:%02X:%02X:%02X:%02X\n"
        "IP: %s\n\n"
        "Free Heap: %lu KB\n"
        "Min Heap: %lu KB\n",
        mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
        ip_str,
        (unsigned long)(free_heap / 1024),
        (unsigned long)(min_heap / 1024)
    );

    // Content
    lv_obj_t* label = lv_label_create(scr);
    lv_label_set_text(label, info);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 20);

    return scr;
}
