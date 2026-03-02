// components/ui/screen_manager.c

#include "screen_manager.h"
#include "ui.h"           // For UI refresh functions
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_system.h"   // For esp_restart()
#include "bsp/display.h"  // For brightness control
#include "esp_mac.h"      // For MAC address
#include "esp_netif.h"    // For IP address
#include "network.h"      // For WiFi scanning
#include "settings.h"     // For persistent settings
#include "sdcard.h"       // For SD card access
#include "freertos/FreeRTOS.h"  // For vTaskDelay and pdMS_TO_TICKS
#include "freertos/task.h"
#include <string.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>
#include "esp_heap_caps.h"

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
static lv_obj_t* create_text_color_settings(void);
static lv_obj_t* create_animation_preview(void);
static lv_obj_t* create_about_screen(void);

// Forward declarations for callbacks
static void wifi_scan_btn_cb(lv_event_t* e);
static void wifi_network_select_cb(lv_event_t* e);
static void wifi_connect_btn_cb(lv_event_t* e);
static void background_select_cb(lv_event_t* e);

// =============================================================================
// Navigation stack
// =============================================================================

void screen_manager_init(void) {
    ESP_LOGI(TAG, "Screen manager initialized");
    stack_top = -1;
    screens[SCREEN_CLOCK] = NULL;  // Set by ui module via screen_manager_set_clock_screen
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

// =============================================================================
// Screen creation dispatch — function pointer array eliminates switch-case ABC
// =============================================================================

/** @brief Function pointer type for screen factory functions. */
typedef lv_obj_t* (*screen_creator_fn_t)(void);

/** @brief Dispatch table mapping each screen_id_t to its factory function. */
static const screen_creator_fn_t SCREEN_CREATORS[SCREEN_MAX] = {
    [SCREEN_CLOCK]              = NULL,  // Created externally by ui module
    [SCREEN_SETTINGS_MENU]      = create_settings_menu,
    [SCREEN_WIFI_SETTINGS]      = create_wifi_settings,
    [SCREEN_BRIGHTNESS_SETTINGS]= create_brightness_settings,
    [SCREEN_BACKGROUND_SELECTOR]= create_background_selector,
    [SCREEN_TEXT_COLOR_SETTINGS]= create_text_color_settings,
    [SCREEN_ANIMATION_PREVIEW]  = create_animation_preview,
    [SCREEN_ABOUT]              = create_about_screen,
};

/**
 * @brief Return existing screen or create it on first access.
 * @param screen  Screen ID to retrieve or create.
 * @return LVGL screen object, or NULL on failure.
 */
static lv_obj_t* get_or_create_screen(screen_id_t screen) {
    if (screens[screen] != NULL) {
        return screens[screen];
    }
    if (screen >= SCREEN_MAX || SCREEN_CREATORS[screen] == NULL) {
        ESP_LOGW(TAG, "No creator for screen %d", screen);
        return NULL;
    }
    ESP_LOGI(TAG, "Creating screen %d on demand", screen);
    screens[screen] = SCREEN_CREATORS[screen]();
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
    lv_scr_load_anim(scr, LV_SCREEN_LOAD_ANIM_OVER_TOP, 350, 0, false);
    lvgl_port_unlock();
}

void screen_manager_pop(void) {
    if (stack_top <= 0) {
        ESP_LOGW(TAG, "Already at root screen, cannot pop");
        return;
    }
    pop_stack();
    screen_id_t prev_screen = screen_stack[stack_top];
    ESP_LOGI(TAG, "Going back to screen %d", prev_screen);
    lv_obj_t* scr = get_or_create_screen(prev_screen);
    if (scr == NULL) {
        ESP_LOGE(TAG, "Failed to get screen %d", prev_screen);
        return;
    }
    lvgl_port_lock(0);
    lv_scr_load_anim(scr, LV_SCREEN_LOAD_ANIM_MOVE_BOTTOM, 500, 0, false);
    lvgl_port_unlock();
}

void screen_manager_home(void) {
    if (stack_top == 0) {
        ESP_LOGI(TAG, "Already at home screen");
        return;
    }
    ESP_LOGI(TAG, "Returning to home screen");
    stack_top = 0;
    lv_obj_t* scr = screens[SCREEN_CLOCK];
    if (scr == NULL) {
        ESP_LOGE(TAG, "Clock screen not set!");
        return;
    }
    lvgl_port_lock(0);
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
// Shared UI helpers
// =============================================================================

/** @brief Back-button event callback — pops one screen off the stack. */
static void back_button_cb(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        ESP_LOGI(TAG, "Back button clicked");
        screen_manager_pop();
    }
}

/**
 * @brief Create the standard back button in the top-left corner.
 * @param parent  Parent screen object.
 * @return Back button object.
 */
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

/**
 * @brief Create a black full-screen LVGL screen with a centered title and back button.
 *
 * Shared by all settings screens so each create_* function starts with a
 * single call instead of 8+ setup calls.
 *
 * @param title  Title string shown at the top of the screen.
 * @return New LVGL screen object.
 */
static lv_obj_t* sm_new_screen(const char* title) {
    lv_obj_t* scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_t* lbl = lv_label_create(scr);
    lv_label_set_text(lbl, title);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
    lv_obj_align(lbl, LV_ALIGN_TOP_MID, 0, 20);
    create_back_button(scr);
    return scr;
}

// =============================================================================
// Settings menu screen
// =============================================================================

static screen_id_t wifi_screen_id             = SCREEN_WIFI_SETTINGS;
static screen_id_t brightness_screen_id       = SCREEN_BRIGHTNESS_SETTINGS;
static screen_id_t background_selector_id     = SCREEN_BACKGROUND_SELECTOR;
static screen_id_t text_color_settings_id     = SCREEN_TEXT_COLOR_SETTINGS;
static screen_id_t animation_preview_id       = SCREEN_ANIMATION_PREVIEW;
static screen_id_t about_screen_id            = SCREEN_ABOUT;

/** @brief Navigate to a settings sub-screen when a menu item is clicked. */
static void settings_menu_item_cb(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        screen_id_t* screen_id = (screen_id_t*)lv_event_get_user_data(e);
        ESP_LOGI(TAG, "Settings item clicked: %d", *screen_id);
        screen_manager_push(*screen_id);
    }
}

/** @brief Reboot the device via esp_restart after a brief UI settle delay. */
static void reboot_btn_cb(lv_event_t* e) {
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_CLICKED) {
        ESP_LOGI(TAG, "Reboot button clicked - initiating system restart");
        lvgl_port_unlock();
        vTaskDelay(pdMS_TO_TICKS(500));
        esp_restart();
    }
}

/**
 * @brief Add one item to a settings list with its navigation callback.
 * @param list      LVGL list to append to.
 * @param icon      Symbol icon string (e.g. LV_SYMBOL_WIFI).
 * @param label     Display text for the item.
 * @param screen_id Pointer to screen_id_t stored in static memory.
 */
static void sm_add_menu_item(lv_obj_t* list, const char* icon,
                              const char* label, screen_id_t* screen_id) {
    lv_obj_t* btn = lv_list_add_button(list, icon, label);
    lv_obj_add_event_cb(btn, settings_menu_item_cb, LV_EVENT_CLICKED, screen_id);
}

/**
 * @brief Populate the settings list with all navigation items and reboot.
 * @param list  LVGL list object to populate.
 */
static void sm_populate_settings_menu(lv_obj_t* list) {
    sm_add_menu_item(list, LV_SYMBOL_WIFI,     "WiFi Settings",    &wifi_screen_id);
    sm_add_menu_item(list, LV_SYMBOL_IMAGE,    "Brightness",       &brightness_screen_id);
    sm_add_menu_item(list, LV_SYMBOL_IMAGE,    "Background Image", &background_selector_id);
    sm_add_menu_item(list, LV_SYMBOL_EDIT,     "Text Color",       &text_color_settings_id);
    sm_add_menu_item(list, LV_SYMBOL_PLAY,     "Animation Preview",&animation_preview_id);
    sm_add_menu_item(list, LV_SYMBOL_SETTINGS, "About",            &about_screen_id);
    lv_obj_t* btn_reboot = lv_list_add_button(list, LV_SYMBOL_POWER, "Reboot Device");
    lv_obj_add_event_cb(btn_reboot, reboot_btn_cb, LV_EVENT_CLICKED, NULL);
}

/**
 * @brief Create the top-level settings menu list widget.
 * @param scr  Parent screen.
 * @return LVGL list object.
 */
static lv_obj_t* sm_create_menu_list(lv_obj_t* scr) {
    lv_obj_t* list = lv_list_create(scr);
    lv_obj_set_size(list, 700, 500);
    lv_obj_align(list, LV_ALIGN_CENTER, 0, 30);
    lv_obj_set_style_text_font(list, &lv_font_montserrat_24, 0);
    return list;
}

/**
 * @brief Create the settings menu screen.
 * @return New LVGL screen object.
 */
static lv_obj_t* create_settings_menu(void) {
    lv_obj_t* scr  = sm_new_screen("Settings");
    lv_obj_t* list = sm_create_menu_list(scr);
    sm_populate_settings_menu(list);
    return scr;
}

// =============================================================================
// WiFi settings screen
// =============================================================================

static char selected_ssid[33]   = "";
static char wifi_password[64]   = "";
static lv_obj_t* wifi_keyboard        = NULL;
static lv_obj_t* password_textarea    = NULL;
static lv_obj_t* wifi_list            = NULL;
static lv_obj_t* wifi_status_label    = NULL;

/**
 * @brief Add one AP entry to the WiFi network list.
 * @param ap  AP info struct from network_scan.
 */
static void wifi_scan_add_network(const wifi_ap_info_t* ap) {
    char btn_text[64];
    const char* lock_icon = (ap->authmode != WIFI_AUTH_OPEN) ? "* " : "";
    snprintf(btn_text, sizeof(btn_text), "%s%s (%d dBm)", lock_icon, ap->ssid, ap->rssi);
    lv_obj_t* btn = lv_list_add_button(wifi_list, NULL, btn_text);
    char* ssid_copy = (char*)malloc(33);
    strncpy(ssid_copy, ap->ssid, 32);
    ssid_copy[32] = '\0';
    lv_obj_set_user_data(btn, ssid_copy);
    lv_obj_add_event_cb(btn, wifi_network_select_cb, LV_EVENT_CLICKED, NULL);
}

/**
 * @brief Populate the network list and update status label with scan results.
 * @param ap_list  Array of AP info from network_scan.
 * @param found    Number of APs found.
 */
static void wifi_scan_populate_list(const wifi_ap_info_t* ap_list, uint16_t found) {
    char buf[64];
    snprintf(buf, sizeof(buf), "Found %d networks", found);
    if (wifi_status_label) lv_label_set_text(wifi_status_label, buf);
    for (uint16_t i = 0; i < found; i++) {
        wifi_scan_add_network(&ap_list[i]);
    }
}

/**
 * @brief Update UI and list after a scan: error path shows a message; success populates list.
 * @param err      Return code from network_scan.
 * @param ap_list  AP array (valid when err==ESP_OK && found>0).
 * @param found    Number of APs discovered.
 */
static void wifi_scan_handle_result(esp_err_t err, wifi_ap_info_t* ap_list, uint16_t found) {
    if (err != ESP_OK || found == 0) {
        ESP_LOGW(TAG, "WiFi scan: err=%d found=%d", (int)err, (int)found);
        if (wifi_status_label) lv_label_set_text(wifi_status_label, "No networks found");
        return;
    }
    ESP_LOGI(TAG, "Scan complete: %d networks", found);
    wifi_scan_populate_list(ap_list, found);
}

/**
 * @brief Clear the list, perform a WiFi scan, and populate results.
 *
 * Releases the LVGL lock around the blocking network_scan call.
 */
static void wifi_scan_run(void) {
    if (wifi_list != NULL) lv_obj_clean(wifi_list);
    if (wifi_status_label) lv_label_set_text(wifi_status_label, "Scanning...");
    wifi_ap_info_t ap_list[20];
    uint16_t found = 0;
    lvgl_port_unlock();
    esp_err_t err = network_scan(ap_list, 20, &found);
    lvgl_port_lock(0);
    wifi_scan_handle_result(err, ap_list, found);
}

/** @brief Scan button event — delegates to wifi_scan_run on click. */
static void wifi_scan_btn_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    ESP_LOGI(TAG, "WiFi scan button clicked");
    wifi_scan_run();
}

/** @brief Select a network from the list; reveal the password input. */
static void wifi_network_select_cb(lv_event_t* e) {
    lv_obj_t* btn = lv_event_get_target(e);
    char* ssid = (char*)lv_obj_get_user_data(btn);
    if (ssid == NULL) return;
    ESP_LOGI(TAG, "Selected network: %s", ssid);
    strncpy(selected_ssid, ssid, sizeof(selected_ssid) - 1);
    selected_ssid[sizeof(selected_ssid) - 1] = '\0';
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

/**
 * @brief Save WiFi credentials to NVS after a successful connection.
 * @param ssid      Network SSID string.
 * @param password  Network password string.
 */
static void wifi_save_credentials(const char* ssid, const char* password) {
    clock_settings_t cfg;
    if (settings_load(&cfg) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to load settings for credential save");
        return;
    }
    strncpy(cfg.wifi_ssid, ssid, sizeof(cfg.wifi_ssid) - 1);
    cfg.wifi_ssid[sizeof(cfg.wifi_ssid) - 1] = '\0';
    strncpy(cfg.wifi_password, password, sizeof(cfg.wifi_password) - 1);
    cfg.wifi_password[sizeof(cfg.wifi_password) - 1] = '\0';
    cfg.wifi_configured = true;
    esp_err_t err = settings_save(&cfg);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "WiFi credentials saved for SSID: %s", ssid);
    } else {
        ESP_LOGE(TAG, "Failed to save WiFi credentials: %s", esp_err_to_name(err));
    }
}

/**
 * @brief Hide password textarea and keyboard after user initiates a connection.
 */
static void wifi_hide_inputs(void) {
    if (password_textarea && wifi_keyboard) {
        lv_obj_add_flag(password_textarea, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(wifi_keyboard, LV_OBJ_FLAG_HIDDEN);
    }
}

/**
 * @brief Connect to the network and update the status label.
 *
 * Releases the LVGL lock around the blocking network_connect call.
 *
 * @param ssid      Network SSID to connect to.
 * @param password  Network password.
 */
static void wifi_connect_and_update(const char* ssid, const char* password) {
    wifi_hide_inputs();
    lvgl_port_unlock();
    esp_err_t err = network_connect(ssid, password);
    if (err == ESP_OK) wifi_save_credentials(ssid, password);
    lvgl_port_lock(0);
    const char* msg = (err == ESP_OK) ? "Connected! Saved to settings." : "Connection failed";
    if (wifi_status_label) lv_label_set_text(wifi_status_label, msg);
    ESP_LOGI(TAG, "WiFi connect result for %s: %d", ssid, (int)err);
}

/** @brief Connect button callback — reads password and initiates connection. */
static void wifi_connect_btn_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    if (selected_ssid[0] == '\0') {
        ESP_LOGW(TAG, "Connect clicked but no network selected");
        return;
    }
    const char* pwd = lv_textarea_get_text(password_textarea);
    strncpy(wifi_password, pwd, sizeof(wifi_password) - 1);
    wifi_password[sizeof(wifi_password) - 1] = '\0';
    ESP_LOGI(TAG, "Connecting to %s...", selected_ssid);
    if (wifi_status_label) lv_label_set_text(wifi_status_label, "Connecting...");
    wifi_connect_and_update(selected_ssid, wifi_password);
}

/**
 * @brief Create and position the WiFi connection-status label.
 * @param scr  Parent screen.
 */
static void wifi_create_status_row(lv_obj_t* scr) {
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
}

/**
 * @brief Create the Scan Networks button.
 * @param scr  Parent screen.
 */
static void wifi_create_scan_btn(lv_obj_t* scr) {
    lv_obj_t* btn = lv_button_create(scr);
    lv_obj_set_size(btn, 200, 50);
    lv_obj_align(btn, LV_ALIGN_TOP_MID, 0, 110);
    lv_obj_add_event_cb(btn, wifi_scan_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t* lbl = lv_label_create(btn);
    lv_label_set_text(lbl, LV_SYMBOL_REFRESH " Scan Networks");
    lv_obj_center(lbl);
}

/**
 * @brief Create the scrollable network list (sets global wifi_list).
 * @param scr  Parent screen.
 */
static void wifi_create_network_list(lv_obj_t* scr) {
    wifi_list = lv_list_create(scr);
    lv_obj_set_size(wifi_list, 700, 250);
    lv_obj_align(wifi_list, LV_ALIGN_CENTER, 0, 20);
}

/**
 * @brief Create the hidden password text area (sets global password_textarea).
 * @param scr  Parent screen.
 */
static void wifi_create_password_input(lv_obj_t* scr) {
    password_textarea = lv_textarea_create(scr);
    lv_obj_set_size(password_textarea, 600, 50);
    lv_obj_align(password_textarea, LV_ALIGN_BOTTOM_MID, 0, -280);
    lv_textarea_set_placeholder_text(password_textarea, "Enter password...");
    lv_textarea_set_password_mode(password_textarea, true);
    lv_obj_add_flag(password_textarea, LV_OBJ_FLAG_HIDDEN);
}

/**
 * @brief Create the hidden on-screen keyboard (sets global wifi_keyboard).
 * @param scr  Parent screen.
 */
static void wifi_create_keyboard(lv_obj_t* scr) {
    wifi_keyboard = lv_keyboard_create(scr);
    lv_keyboard_set_textarea(wifi_keyboard, password_textarea);
    lv_obj_set_size(wifi_keyboard, 750, 220);
    lv_obj_align(wifi_keyboard, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_add_flag(wifi_keyboard, LV_OBJ_FLAG_HIDDEN);
}

/**
 * @brief Create the Connect button anchored next to the password text area.
 * @param scr  Parent screen.
 */
static void wifi_create_connect_btn(lv_obj_t* scr) {
    lv_obj_t* btn = lv_button_create(scr);
    lv_obj_set_size(btn, 120, 50);
    lv_obj_align_to(btn, password_textarea, LV_ALIGN_OUT_RIGHT_MID, 10, 0);
    lv_obj_add_event_cb(btn, wifi_connect_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t* lbl = lv_label_create(btn);
    lv_label_set_text(lbl, "Connect");
    lv_obj_center(lbl);
}

/**
 * @brief Create the WiFi settings screen.
 * @return New LVGL screen object.
 */
static lv_obj_t* create_wifi_settings(void) {
    lv_obj_t* scr = sm_new_screen("WiFi Settings");
    wifi_create_status_row(scr);
    wifi_create_scan_btn(scr);
    wifi_create_network_list(scr);
    wifi_create_password_input(scr);
    wifi_create_keyboard(scr);
    wifi_create_connect_btn(scr);
    return scr;
}

// =============================================================================
// Brightness settings screen
// =============================================================================

/**
 * @brief Persist the new brightness value to NVS.
 *
 * Releases the LVGL lock around the NVS write.
 *
 * @param value  Brightness percentage (10–100).
 */
static void brightness_save_setting(int32_t value) {
    lvgl_port_unlock();
    clock_settings_t cfg;
    if (settings_load(&cfg) == ESP_OK) {
        cfg.brightness = (uint8_t)value;
        esp_err_t err = settings_save(&cfg);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "Brightness %ld%% saved", value);
        } else {
            ESP_LOGW(TAG, "Failed to save brightness: %s", esp_err_to_name(err));
        }
    }
    lvgl_port_lock(0);
}

/** @brief Slider value-changed callback — sets hardware brightness and saves. */
static void brightness_slider_cb(lv_event_t* e) {
    lv_obj_t* slider = lv_event_get_target(e);
    int32_t value    = lv_slider_get_value(slider);
    ESP_LOGI(TAG, "Brightness changed to %ld%%", value);
    bsp_display_brightness_set(value);
    lv_obj_t* label = (lv_obj_t*)lv_event_get_user_data(e);
    if (label) {
        char buf[32];
        snprintf(buf, sizeof(buf), "Brightness: %ld%%", value);
        lv_label_set_text(label, buf);
    }
    brightness_save_setting(value);
}

/**
 * @brief Create the brightness percentage label.
 * @param scr  Parent screen.
 * @return Label object (passed to slider as user data).
 */
static lv_obj_t* bright_create_label(lv_obj_t* scr) {
    lv_obj_t* lbl = lv_label_create(scr);
    lv_label_set_text(lbl, "Brightness: 50%");
    lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_24, 0);
    lv_obj_align(lbl, LV_ALIGN_CENTER, 0, -100);
    return lbl;
}

/**
 * @brief Create and configure the brightness slider.
 * @param scr    Parent screen.
 * @param label  Label object to update on value change (passed as user data).
 */
static void bright_create_slider(lv_obj_t* scr, lv_obj_t* label) {
    lv_obj_t* slider = lv_slider_create(scr);
    lv_obj_set_width(slider, 600);
    lv_slider_set_range(slider, 10, 100);
    lv_slider_set_value(slider, 50, LV_ANIM_OFF);
    lv_obj_align(slider, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_event_cb(slider, brightness_slider_cb, LV_EVENT_VALUE_CHANGED, label);
}

/**
 * @brief Create the brightness settings screen.
 * @return New LVGL screen object.
 */
static lv_obj_t* create_brightness_settings(void) {
    lv_obj_t* scr   = sm_new_screen("Brightness");
    lv_obj_t* label = bright_create_label(scr);
    bright_create_slider(scr, label);
    lv_obj_t* instr = lv_label_create(scr);
    lv_label_set_text(instr, "Slide to adjust screen brightness");
    lv_obj_set_style_text_color(instr, lv_color_white(), 0);
    lv_obj_align(instr, LV_ALIGN_CENTER, 0, 100);
    return scr;
}

// =============================================================================
// Text color settings screen
// =============================================================================

typedef struct {
    uint32_t    color;  ///< RGB888 color value
    const char* name;   ///< Display name
} color_preset_t;

static const color_preset_t COLOR_PRESETS[] = {
    {0x000000, "Black"},
    {0xFFFFFF, "White"},
    {0xFF0000, "Red"},
    {0x00FF00, "Green"},
    {0x0000FF, "Blue"},
    {0xFFFF00, "Yellow"},
    {0x00FFFF, "Cyan"},
    {0xFF00FF, "Magenta"},
    {0xFF8800, "Orange"},
};
#define NUM_COLOR_PRESETS (sizeof(COLOR_PRESETS) / sizeof(COLOR_PRESETS[0]))

/**
 * @brief Show a success message box with msg, wait 1.5 s, then pop the screen.
 *
 * Shared by color and background selection callbacks.
 * LVGL lock must be held by caller.
 *
 * @param msg  Body text shown inside the message box.
 */
static void sm_show_success_and_pop(const char* msg) {
    lv_obj_t* mbox = lv_msgbox_create(NULL);
    lv_msgbox_add_title(mbox, "Success");
    lv_msgbox_add_text(mbox, msg);
    lv_msgbox_add_close_button(mbox);
    lv_obj_center(mbox);
    vTaskDelay(pdMS_TO_TICKS(1500));
    screen_manager_pop();
}

/**
 * @brief Load settings, set text_color field, and save back to NVS.
 *
 * Called while LVGL lock is NOT held.
 *
 * @param color_value  RGB888 color to persist.
 * @return ESP_OK on success.
 */
static esp_err_t color_update_settings(uint32_t color_value) {
    clock_settings_t cfg;
    esp_err_t err = settings_load(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to load settings for color update");
        return err;
    }
    cfg.text_color = color_value;
    return settings_save(&cfg);
}

/**
 * @brief Persist a color selection, refresh the UI, and navigate back.
 *
 * Releases the LVGL lock around NVS operations.
 *
 * @param color_value  RGB888 color to apply.
 */
static void color_select_save(uint32_t color_value) {
    lvgl_port_unlock();
    esp_err_t err = color_update_settings(color_value);
    lvgl_port_lock(0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Color save failed: %d", (int)err);
        return;
    }
    ESP_LOGI(TAG, "Text color saved: 0x%06lX", color_value);
    ui_refresh_text_color();
    sm_show_success_and_pop("Text color updated!");
}

/** @brief Clicked callback for a color preset button. */
static void color_select_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    uint32_t color_value = (uint32_t)(uintptr_t)lv_event_get_user_data(e);
    ESP_LOGI(TAG, "Text color selected: 0x%06lX", color_value);
    color_select_save(color_value);
}

/**
 * @brief Add a single color preset entry to the color list.
 * @param list          LVGL list object.
 * @param preset        Preset descriptor.
 * @param current_color Currently saved color (highlighted if matching).
 */
static void tc_add_color_preset(lv_obj_t* list, const color_preset_t* preset,
                                 uint32_t current_color) {
    lv_obj_t* btn = lv_list_add_button(list, LV_SYMBOL_BULLET, preset->name);
    lv_obj_add_event_cb(btn, color_select_cb, LV_EVENT_CLICKED,
                        (void*)(uintptr_t)preset->color);
    if (preset->color == current_color) {
        lv_obj_add_state(btn, LV_STATE_FOCUSED);
    }
}

/**
 * @brief Create the color preset list and populate all entries.
 * @param scr           Parent screen.
 * @param current_color Currently saved color (used to highlight the active preset).
 */
static void tc_create_preset_list(lv_obj_t* scr, uint32_t current_color) {
    lv_obj_t* list = lv_list_create(scr);
    lv_obj_set_size(list, 700, 450);
    lv_obj_align(list, LV_ALIGN_CENTER, 0, 30);
    lv_obj_set_style_text_font(list, &lv_font_montserrat_20, 0);
    for (int i = 0; i < (int)NUM_COLOR_PRESETS; i++) {
        tc_add_color_preset(list, &COLOR_PRESETS[i], current_color);
    }
}

/**
 * @brief Create the text color settings screen.
 * @return New LVGL screen object.
 */
static lv_obj_t* create_text_color_settings(void) {
    lv_obj_t* scr = sm_new_screen("Text Color");
    clock_settings_t cfg;
    uint32_t current_color = 0xFFFFFF;
    if (settings_load(&cfg) == ESP_OK) {
        current_color = cfg.text_color;
    }
    tc_create_preset_list(scr, current_color);
    return scr;
}

// =============================================================================
// Background selector screen
// =============================================================================

/**
 * @brief Persist background_image path to NVS.
 *
 * Releases the LVGL lock around NVS operations.
 *
 * @param filename  POSIX-relative path on SD card (e.g. "/bg.gif").
 * @return ESP_OK on success, or error code.
 */
static esp_err_t bg_settings_update(const char* filename) {
    lvgl_port_unlock();
    clock_settings_t cfg;
    esp_err_t err = settings_load(&cfg);
    if (err == ESP_OK) {
        snprintf(cfg.background_image, sizeof(cfg.background_image), "A:%s", filename);
        err = settings_save(&cfg);
    }
    lvgl_port_lock(0);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Background saved: A:%s", filename);
    } else {
        ESP_LOGE(TAG, "bg_settings_update failed: %d", (int)err);
    }
    return err;
}

/**
 * @brief Save, refresh, confirm, and navigate back after background selection.
 * @param filename  POSIX-relative path on SD card.
 */
static void background_select_apply(const char* filename) {
    esp_err_t err = bg_settings_update(filename);
    if (err != ESP_OK) return;
    ui_refresh_background();
    sm_show_success_and_pop("Background updated!");
}

/** @brief Clicked callback for a background file list item. */
static void background_select_cb(lv_event_t* e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    const char* filename = (const char*)lv_event_get_user_data(e);
    if (filename == NULL) return;
    ESP_LOGI(TAG, "Background selected: %s", filename);
    background_select_apply(filename);
}

/**
 * @brief Return true if the file extension is a supported background format.
 * @param ext  Extension string including the dot (e.g. ".png").
 */
static bool bg_is_image_file(const char* ext) {
    return (strcasecmp(ext, ".png") == 0 || strcasecmp(ext, ".gif") == 0);
}

/**
 * @brief Try to add a directory entry to the background file list.
 *
 * Skips directories and non-image files.  Duplicates the filepath into heap
 * so the callback user-data pointer remains valid after the stack frame exits.
 *
 * @param list   LVGL list object to append to.
 * @param entry  Directory entry from readdir.
 * @return true if the entry was added, false if skipped.
 */
static bool bg_scan_add_file(lv_obj_t* list, struct dirent* entry) {
    if (entry->d_type == DT_DIR) return false;
    const char* ext = strrchr(entry->d_name, '.');
    if (!ext || !bg_is_image_file(ext)) return false;
    bool is_gif = (strcasecmp(ext, ".gif") == 0);
    const char* icon = is_gif ? LV_SYMBOL_LOOP : LV_SYMBOL_IMAGE;
    char filepath[256];
    snprintf(filepath, sizeof(filepath), "/%s", entry->d_name);
    lv_obj_t* btn = lv_list_add_button(list, icon, entry->d_name);
    lv_obj_add_event_cb(btn, background_select_cb, LV_EVENT_CLICKED,
                        (void*)strdup(filepath));
    return true;
}

/**
 * @brief Scan /sdcard for image files and add them to the list.
 * @param list  LVGL list object to populate.
 * @return Number of files added, or negative value on directory open error.
 */
static int bg_scan_directory(lv_obj_t* list) {
    DIR* dir = opendir("/sdcard");
    if (dir == NULL) {
        ESP_LOGE(TAG, "Failed to open /sdcard directory");
        return -1;
    }
    int count = 0;
    struct dirent* entry;
    while ((entry = readdir(dir)) != NULL) {
        if (bg_scan_add_file(list, entry)) count++;
    }
    closedir(dir);
    return count;
}

/**
 * @brief Add a non-clickable warning item to a list.
 * @param list  LVGL list object.
 * @param msg   Warning message string.
 */
static void bg_add_warning_item(lv_obj_t* list, const char* msg) {
    lv_obj_t* btn = lv_list_add_button(list, LV_SYMBOL_WARNING, msg);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_CLICKABLE);
}

/**
 * @brief Create the background file list widget.
 * @param scr  Parent screen.
 * @return LVGL list object.
 */
static lv_obj_t* bg_create_file_list(lv_obj_t* scr) {
    lv_obj_t* list = lv_list_create(scr);
    lv_obj_set_size(list, 700, 500);
    lv_obj_align(list, LV_ALIGN_CENTER, 0, 30);
    lv_obj_set_style_text_font(list, &lv_font_montserrat_20, 0);
    return list;
}

/**
 * @brief Create the background selector screen.
 * @return New LVGL screen object.
 */
static lv_obj_t* create_background_selector(void) {
    lv_obj_t* scr  = sm_new_screen("Select Background");
    lv_obj_t* list = bg_create_file_list(scr);
    if (!sdcard_is_mounted()) {
        bg_add_warning_item(list, "SD card not mounted");
        return scr;
    }
    int count = bg_scan_directory(list);
    if (count < 0) {
        bg_add_warning_item(list, "Failed to read SD card");
    } else if (count == 0) {
        bg_add_warning_item(list, "No PNG/GIF files found");
    } else {
        ESP_LOGI(TAG, "Found %d background files on SD card", count);
    }
    return scr;
}

// =============================================================================
// Animation preview screen
// =============================================================================

static uint8_t *s_lottie_buf = NULL;

#define LOTTIE_FILE_PATH   "/sdcard/hummingbird.json"
#define LOTTIE_LOAD_STACK  (64 * 1024)  /* 64 KB — hummingbird.json parse is deep recursive (approve used 29.4/32 KB) */
#define LOTTIE_W           200
#define LOTTIE_H           200

/** @brief Arguments passed to the lottie_load_task. */
typedef struct {
    lv_obj_t *widget;
    char      path[256];
} lottie_load_arg_t;

/**
 * @brief Log entry heap/stack stats for lottie_load_task.
 */
static void lottie_log_entry_stats(const char* path, const void* stack_probe)
{
    ESP_LOGI(TAG, "lottie_load_task: start path='%s' stack_addr=%p", path, stack_probe);
    ESP_LOGI(TAG, "lottie_load_task: heap=%lu SPIRAM=%lu internal=%lu stack_hw=%lu",
             (unsigned long)esp_get_free_heap_size(),
             (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
             (unsigned long)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned long)uxTaskGetStackHighWaterMark(NULL) * sizeof(StackType_t));
}

/**
 * @brief Log exit heap stats for lottie_load_task.
 */
static void lottie_log_exit_stats(void)
{
    ESP_LOGI(TAG, "lottie_load_task: complete — heap=%lu SPIRAM=%lu",
             (unsigned long)esp_get_free_heap_size(),
             (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}

/**
 * @brief Stat @p path and log the result.
 * @return true if the file exists, false otherwise.
 */
static bool lottie_load_check_file(const char* path)
{
    struct stat st;
    if (stat(path, &st) != 0) {
        ESP_LOGE(TAG, "lottie_load_task: file not found: '%s' (errno=%d %s)",
                 path, errno, strerror(errno));
        return false;
    }
    ESP_LOGI(TAG, "lottie_load_task: file found, size=%lld bytes", (long long)st.st_size);
    return true;
}

/**
 * @brief Acquire the LVGL lock and call lv_lottie_set_src_file on @p widget.
 *
 * Logs stack watermark before and after so ThorVG stack depth is visible.
 */
static void lottie_load_set_src(lv_obj_t* widget, const char* path)
{
    ESP_LOGI(TAG, "lottie_load_task: acquiring LVGL lock");
    lvgl_port_lock(0);
    ESP_LOGI(TAG, "lottie_load_task: calling lv_lottie_set_src_file (tvg_picture_load + first render)");
    lv_lottie_set_src_file(widget, path);
    ESP_LOGI(TAG, "lottie_load_task: lv_lottie_set_src_file returned");
    ESP_LOGI(TAG, "lottie_load_task: stack_hw after load=%lu",
             (unsigned long)uxTaskGetStackHighWaterMark(NULL) * sizeof(StackType_t));
    lvgl_port_unlock();
}

/**
 * @brief Dedicated task: loads ThorVG/Lottie animation on a large stack.
 *
 * ThorVG's JSON parser uses deep recursion that overflows the 16 KB LVGL
 * task stack. This task runs with LOTTIE_LOAD_STACK bytes so the parse
 * completes safely.  The LVGL lock is held for the duration of set_src_file
 * so ThorVG and LVGL are not accessed concurrently.
 */
static void lottie_load_task(void *arg)
{
    lottie_load_arg_t *a = (lottie_load_arg_t *)arg;
    lv_obj_t *widget     = a->widget;
    char path[256];
    strlcpy(path, a->path, sizeof(path));
    free(a);

    volatile uint8_t _stack_probe = 0;
    lottie_log_entry_stats(path, (const void*)&_stack_probe);

    if (!lottie_load_check_file(path)) {
        vTaskDeleteWithCaps(NULL);
        return;
    }
    lottie_load_set_src(widget, path);
    lottie_log_exit_stats();
    vTaskDeleteWithCaps(NULL);
}

/**
 * @brief Delete @p widget and return true if its render task failed to start.
 *
 * The render task runs on a 96 KB SPIRAM stack.  If PSRAM was exhausted at
 * widget creation time this returns true so the caller can propagate NULL.
 */
static bool anim_delete_if_render_failed(lv_obj_t* widget)
{
    if (!lv_lottie_render_failed(widget)) return false;
    ESP_LOGE(TAG, "anim: render task failed to start (insufficient PSRAM?) — showing text fallback");
    lv_obj_delete(widget);
    return true;
}

/**
 * @brief Create the lv_lottie widget on @p scr and verify the render task started.
 *
 * @return Widget pointer, or NULL if creation failed or render task did not start.
 */
static lv_obj_t* anim_create_lottie_widget_obj(lv_obj_t* scr)
{
    ESP_LOGI(TAG, "anim: heap=%lu SPIRAM=%lu",
             (unsigned long)esp_get_free_heap_size(),
             (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    ESP_LOGI(TAG, "anim: calling lv_lottie_create");
    lv_obj_t* widget = lv_lottie_create(scr);
    if (!widget) {
        ESP_LOGE(TAG, "anim: lv_lottie_create returned NULL");
        return NULL;
    }
    ESP_LOGI(TAG, "anim: lv_lottie_create OK, widget=%p", widget);
    if (anim_delete_if_render_failed(widget)) return NULL;
    return widget;
}

/**
 * @brief Allocate or reuse the global SPIRAM render buffer.
 *
 * On alloc failure deletes @p widget and returns false so the caller can
 * propagate NULL without a second delete.
 *
 * @return true if s_lottie_buf is valid, false on alloc failure.
 */
static bool anim_ensure_render_buf(lv_obj_t* widget)
{
    if (s_lottie_buf) {
        ESP_LOGI(TAG, "anim: reusing existing SPIRAM render buf at %p", s_lottie_buf);
        return true;
    }
    /* lv_lottie_set_buffer computes stride = lv_draw_buf_width_to_stride(w, ARGB8888_PREMULTIPLIED)
     * which includes 64-byte PPA alignment padding (e.g. 200px → 832 B/row, not 800 B/row).
     * ThorVG writes stride×h bytes into this buffer.  Allocating only w×h×4 underestimates
     * by (stride - w*4) * h = 32 * 200 = 6400 bytes → SPIRAM heap overflow → abort. */
    uint32_t stride = lv_draw_buf_width_to_stride(LOTTIE_W, LV_COLOR_FORMAT_ARGB8888_PREMULTIPLIED);
    size_t buf_sz = (size_t)stride * LOTTIE_H;
    ESP_LOGI(TAG, "anim: allocating SPIRAM render buf %zu B (stride=%lu, 64-byte aligned)",
             buf_sz, (unsigned long)stride);
    s_lottie_buf = heap_caps_aligned_alloc(64, buf_sz, MALLOC_CAP_SPIRAM);
    if (!s_lottie_buf) {
        ESP_LOGE(TAG, "anim: SPIRAM alloc FAILED — SPIRAM free=%lu",
                 (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        lv_obj_delete(widget);
        return false;
    }
    ESP_LOGI(TAG, "anim: SPIRAM render buf at %p", s_lottie_buf);
    return true;
}

/**
 * @brief Create and configure the Lottie widget with buffer — NO animation load.
 *
 * Runs in the LVGL task under the LVGL lock.  Allocation and LVGL object
 * setup are the only operations here; the heavy ThorVG parse is deferred to
 * lottie_load_task so it runs on a large stack.
 *
 * @param scr  Parent screen.
 * @return Lottie widget, or NULL on allocation failure.
 */
static lv_obj_t* anim_create_widget_and_buffer(lv_obj_t* scr)
{
    lv_obj_t* widget = anim_create_lottie_widget_obj(scr);
    if (!widget) return NULL;
    if (!anim_ensure_render_buf(widget)) return NULL;

    ESP_LOGI(TAG, "anim: calling lv_obj_set_size(%d, %d)", LOTTIE_W, LOTTIE_H);
    lv_obj_set_size(widget, LOTTIE_W, LOTTIE_H);

    /* lv_lottie_set_buffer triggers tvg_canvas_draw on the empty canvas — log before/after */
    ESP_LOGI(TAG, "anim: calling lv_lottie_set_buffer — triggers first tvg_canvas_draw (empty canvas)");
    lv_lottie_set_buffer(widget, LOTTIE_W, LOTTIE_H, s_lottie_buf);
    ESP_LOGI(TAG, "anim: lv_lottie_set_buffer returned");

    lv_obj_align(widget, LV_ALIGN_CENTER, 0, 0);
    return widget;
}

/**
 * @brief Spawn lottie_load_task with a 32 KB stack to load the animation file.
 *
 * @param widget  Lottie widget to load into (already created and buffered).
 * @param path    POSIX path to the Lottie JSON file on SD card.
 */
static void anim_spawn_load_task(lv_obj_t *widget, const char *path)
{
    lottie_load_arg_t *a = malloc(sizeof(*a));
    if (!a) {
        ESP_LOGE(TAG, "anim: failed to alloc load task arg");
        return;
    }
    a->widget = widget;
    strlcpy(a->path, path, sizeof(a->path));

    /* Force task stack into internal DRAM — with CONFIG_SPIRAM_USE_MALLOC=y,
     * plain xTaskCreate() allocates the 32 KB stack from SPIRAM (>ALWAYSINTERNAL).
     * FreeRTOS context saves during I/O interrupts (SDMMC) crash when the stack
     * is in PSRAM.  MALLOC_CAP_INTERNAL ensures DRAM regardless of SPIRAM config. */
    ESP_LOGI(TAG, "anim: spawning lottie_load_task (stack=%d B, DRAM-forced)", LOTTIE_LOAD_STACK);
    BaseType_t ret = xTaskCreateWithCaps(lottie_load_task, "lottie_load",
                                         LOTTIE_LOAD_STACK, a, 5, NULL,
                                         MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "anim: xTaskCreateWithCaps lottie_load FAILED (ret=%d)", (int)ret);
        free(a);
    } else {
        ESP_LOGI(TAG, "anim: lottie_load_task spawned");
    }
}


/**
 * @brief Create a centered red error label.
 * @param scr  Parent screen.
 * @param msg  Error message string.
 * @return Label object.
 */
static lv_obj_t* anim_create_error_label(lv_obj_t* scr, const char* msg) {
    lv_obj_t* lbl = lv_label_create(scr);
    lv_label_set_text(lbl, msg);
    lv_obj_set_style_text_color(lbl, lv_color_make(255, 100, 100), 0);
    lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 0);
    return lbl;
}

/**
 * @brief Create a centered orange warning label.
 * @param scr  Parent screen.
 * @param msg  Warning message string.
 * @return Label object.
 *
 * Only compiled when LV_USE_LOTTIE is disabled (used in the #else branch
 * of create_animation_preview to report the missing feature).
 */
#if !LV_USE_LOTTIE
static lv_obj_t* anim_create_warning_label(lv_obj_t* scr, const char* msg) {
    lv_obj_t* lbl = lv_label_create(scr);
    lv_label_set_text(lbl, msg);
    lv_obj_set_style_text_color(lbl, lv_color_make(255, 200, 100), 0);
    lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 0);
    return lbl;
}
#endif


#if LV_USE_LOTTIE
/**
 * @brief Populate @p scr with the Lottie widget (or an error label).
 *
 * Factored out of create_animation_preview to keep that function below the
 * ABC complexity gate while preserving the LV_USE_LOTTIE guard.
 */
static void anim_create_lottie_content(lv_obj_t* scr, size_t free_heap)
{
    if (free_heap < 500000) {
        anim_create_error_label(scr,
            "ERROR: Insufficient memory\nRequires 500 KB free heap");
        return;
    }
    lv_obj_t* widget = anim_create_widget_and_buffer(scr);
    if (widget) {
        anim_spawn_load_task(widget, LOTTIE_FILE_PATH);
    } else {
        /* NULL means SPIRAM buffer alloc failed or render task could not start. */
        anim_create_error_label(scr,
            "Animation unavailable\n(insufficient PSRAM for render task)");
    }
}
#endif

/**
 * @brief Create the animation preview screen.
 *
 * Widget creation runs in the LVGL task (lightweight). The ThorVG JSON parse
 * is deferred to lottie_load_task (32 KB stack) to avoid overflowing the
 * LVGL task's 16 KB stack.
 *
 * @return New LVGL screen object.
 */
static lv_obj_t* create_animation_preview(void) {
    lv_obj_t* scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    size_t free_heap = esp_get_free_heap_size();
    ESP_LOGI(TAG, "Animation preview — free heap: %lu SPIRAM: %lu",
             (unsigned long)free_heap,
             (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
#if LV_USE_LOTTIE
    anim_create_lottie_content(scr, free_heap);
#else
    anim_create_warning_label(scr,
        "Lottie not enabled\nCONFIG_LV_USE_LOTTIE is not set");
#endif
    create_back_button(scr);
    return scr;
}

// =============================================================================
// About screen
// =============================================================================

/**
 * @brief Resolve the device IP address into a string buffer.
 *
 * Falls back to "N/A" if the interface is unavailable or not connected.
 *
 * @param buf   Destination buffer.
 * @param size  Size of buf in bytes.
 */
static void about_get_ip_str(char* buf, size_t size) {
    esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t ip_info;
    strlcpy(buf, "N/A", size);
    if (netif && esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
        snprintf(buf, size, IPSTR, IP2STR(&ip_info.ip));
    }
}

/**
 * @brief Build the About screen info string.
 *
 * Collects MAC, IP, and heap statistics into a single formatted buffer.
 *
 * @param buf   Destination buffer.
 * @param size  Size of buf in bytes.
 */
static void about_format_info(char* buf, size_t size) {
    size_t free_heap = esp_get_free_heap_size();
    size_t min_heap  = esp_get_minimum_free_heap_size();
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char ip_str[16];
    about_get_ip_str(ip_str, sizeof(ip_str));
    snprintf(buf, size,
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
        (unsigned long)(min_heap / 1024));
}

/**
 * @brief Create the About screen with device information.
 * @return New LVGL screen object.
 */
static lv_obj_t* create_about_screen(void) {
    lv_obj_t* scr = sm_new_screen("About");
    char info[512];
    about_format_info(info, sizeof(info));
    lv_obj_t* lbl = lv_label_create(scr);
    lv_label_set_text(lbl, info);
    lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
    lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 20);
    return scr;
}
