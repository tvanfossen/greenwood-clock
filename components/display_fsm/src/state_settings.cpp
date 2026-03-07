// components/display_fsm/src/state_settings.cpp
//
// Settings state — manages settings sub-screens with an internal navigation
// stack.  All sub-screens are LVGL containers on the shared s_screen, not
// separate LVGL screens.  The FSM sees only one state: Settings.
//
// Migrated from components/ui/screen_manager.c (Phase 1.7).

#include "display_states.h"
#include "display_scheduler.h"
#include "display_fsm.h"

#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_system.h"     // esp_restart
#include "esp_mac.h"        // MAC address
#include "esp_netif.h"      // IP address
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

extern "C" {
#include "network.h"
#include "settings.h"
#include "sdcard.h"
#include "ui.h"             // ui_refresh_background, ui_refresh_text_color
#include "bsp/display.h"    // bsp_display_brightness_set
}

#include <cstring>
#include <cerrno>
#include <dirent.h>
#include <sys/stat.h>

static const char *TAG = "state_settings";

// ============================================================================
// Internal sub-screen IDs and navigation stack
// ============================================================================

enum settings_sub_screen_t {
    SUB_MENU = 0,
    SUB_WIFI,
    SUB_BRIGHTNESS,
    SUB_BACKGROUND,
    SUB_TEXT_COLOR,
    SUB_ANIMATION,
    SUB_ABOUT,
    SUB_MAX
};

#define MAX_SUB_STACK 4

static settings_sub_screen_t s_sub_stack[MAX_SUB_STACK];
static int s_sub_top = -1;
static lv_obj_t *s_sub_containers[SUB_MAX] = {};

// Forward declarations
static lv_obj_t *create_sub_menu(lv_obj_t *parent);
static lv_obj_t *create_sub_wifi(lv_obj_t *parent);
static lv_obj_t *create_sub_brightness(lv_obj_t *parent);
static lv_obj_t *create_sub_background(lv_obj_t *parent);
static lv_obj_t *create_sub_text_color(lv_obj_t *parent);
static lv_obj_t *create_sub_animation(lv_obj_t *parent);
static lv_obj_t *create_sub_about(lv_obj_t *parent);

typedef lv_obj_t *(*sub_creator_fn_t)(lv_obj_t *parent);
static const sub_creator_fn_t SUB_CREATORS[SUB_MAX] = {
    [SUB_MENU]       = create_sub_menu,
    [SUB_WIFI]       = create_sub_wifi,
    [SUB_BRIGHTNESS] = create_sub_brightness,
    [SUB_BACKGROUND] = create_sub_background,
    [SUB_TEXT_COLOR]  = create_sub_text_color,
    [SUB_ANIMATION]  = create_sub_animation,
    [SUB_ABOUT]      = create_sub_about,
};

// ============================================================================
// Internal navigation
// ============================================================================

static lv_obj_t *s_settings_root = nullptr;  // root container for all settings

static void destroy_all_subs(void)
{
    for (int i = 0; i < SUB_MAX; i++) {
        if (s_sub_containers[i]) {
            lv_obj_del(s_sub_containers[i]);
            s_sub_containers[i] = nullptr;
        }
    }
    s_sub_top = -1;
}

static void show_sub(settings_sub_screen_t sub)
{
    if (sub >= SUB_MAX || !s_settings_root) return;

    // Hide current
    if (s_sub_top >= 0) {
        settings_sub_screen_t cur = s_sub_stack[s_sub_top];
        if (s_sub_containers[cur]) {
            lv_obj_add_flag(s_sub_containers[cur], LV_OBJ_FLAG_HIDDEN);
        }
    }

    // Create on demand
    if (!s_sub_containers[sub]) {
        s_sub_containers[sub] = SUB_CREATORS[sub](s_settings_root);
    }

    // Show
    lv_obj_clear_flag(s_sub_containers[sub], LV_OBJ_FLAG_HIDDEN);

    // Push stack
    if (s_sub_top < MAX_SUB_STACK - 1) {
        s_sub_top++;
        s_sub_stack[s_sub_top] = sub;
    }
    ESP_LOGI(TAG, "show_sub: %d (depth=%d)", sub, s_sub_top + 1);
}

static void pop_sub(void)
{
    if (s_sub_top <= 0) {
        // At menu level — exit settings entirely
        display_event_t evt = {};
        evt.type = DISPLAY_EVT_SETTINGS_BACK;
        display_fsm_send_event(&evt);
        return;
    }

    // Hide + destroy current
    settings_sub_screen_t cur = s_sub_stack[s_sub_top];
    if (s_sub_containers[cur]) {
        lv_obj_del(s_sub_containers[cur]);
        s_sub_containers[cur] = nullptr;
    }
    s_sub_top--;

    // Show previous
    settings_sub_screen_t prev = s_sub_stack[s_sub_top];
    if (s_sub_containers[prev]) {
        lv_obj_clear_flag(s_sub_containers[prev], LV_OBJ_FLAG_HIDDEN);
    }
    ESP_LOGI(TAG, "pop_sub: now at %d (depth=%d)", prev, s_sub_top + 1);
}

// ============================================================================
// Shared UI helpers
// ============================================================================

static void back_btn_cb(lv_event_t *e)
{
    (void)e;
    pop_sub();
}

/** Create a styled container with title and back button.  All sub-screens
 *  share the same visual style: dark bg, rounded corners, centered. */
static lv_obj_t *make_sub_container(lv_obj_t *parent, const char *title)
{
    lv_obj_t *cont = lv_obj_create(parent);
    lv_obj_set_size(cont, 800, 500);
    lv_obj_align(cont, LV_ALIGN_CENTER, -80, 20);
    lv_obj_set_style_bg_color(cont, lv_color_hex(0x1a1a2e), 0);
    lv_obj_set_style_bg_opa(cont, LV_OPA_90, 0);
    lv_obj_set_style_border_width(cont, 1, 0);
    lv_obj_set_style_border_color(cont, lv_color_hex(0x4a6fa5), 0);
    lv_obj_set_style_radius(cont, 16, 0);
    lv_obj_set_style_pad_all(cont, 20, 0);
    lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);

    // Title
    lv_obj_t *lbl = lv_label_create(cont);
    lv_label_set_text(lbl, title);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
    lv_obj_align(lbl, LV_ALIGN_TOP_MID, 0, 0);

    // Back button
    lv_obj_t *btn = lv_btn_create(cont);
    lv_obj_set_size(btn, 120, 50);
    lv_obj_align(btn, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_add_event_cb(btn, back_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *btn_lbl = lv_label_create(btn);
    lv_label_set_text(btn_lbl, LV_SYMBOL_LEFT " Back");
    lv_obj_center(btn_lbl);

    return cont;
}

/** Show success message box, wait briefly, then pop. */
static void show_success_and_pop(const char *msg)
{
    lv_obj_t *mbox = lv_msgbox_create(NULL);
    lv_msgbox_add_title(mbox, "Success");
    lv_msgbox_add_text(mbox, msg);
    lv_msgbox_add_close_button(mbox);
    lv_obj_center(mbox);

    // Brief delay so user sees the message, then pop
    // Note: we're in FSM task context with LVGL lock held.
    // Release lock for delay, re-acquire.
    lvgl_port_unlock();
    vTaskDelay(pdMS_TO_TICKS(1500));
    lvgl_port_lock(0);

    if (mbox) lv_obj_del(mbox);
    pop_sub();
}

// ============================================================================
// Settings menu
// ============================================================================

static void menu_item_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    settings_sub_screen_t sub = (settings_sub_screen_t)(uintptr_t)lv_event_get_user_data(e);
    show_sub(sub);
}

static void reboot_btn_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    ESP_LOGI(TAG, "Reboot button clicked");
    lvgl_port_unlock();
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
}

static void menu_add_item(lv_obj_t *list, const char *icon,
                           const char *label, settings_sub_screen_t sub)
{
    lv_obj_t *btn = lv_list_add_button(list, icon, label);
    lv_obj_add_event_cb(btn, menu_item_cb, LV_EVENT_CLICKED,
                        (void *)(uintptr_t)sub);
}

static lv_obj_t *create_sub_menu(lv_obj_t *parent)
{
    lv_obj_t *cont = make_sub_container(parent, "Settings");

    lv_obj_t *list = lv_list_create(cont);
    lv_obj_set_size(list, 700, 380);
    lv_obj_align(list, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_text_font(list, &lv_font_montserrat_24, 0);

    menu_add_item(list, LV_SYMBOL_WIFI,     "WiFi Settings",     SUB_WIFI);
    menu_add_item(list, LV_SYMBOL_IMAGE,    "Brightness",        SUB_BRIGHTNESS);
    menu_add_item(list, LV_SYMBOL_IMAGE,    "Background Image",  SUB_BACKGROUND);
    menu_add_item(list, LV_SYMBOL_EDIT,     "Text Color",        SUB_TEXT_COLOR);
    menu_add_item(list, LV_SYMBOL_PLAY,     "Animation Preview", SUB_ANIMATION);
    menu_add_item(list, LV_SYMBOL_SETTINGS, "About",             SUB_ABOUT);

    lv_obj_t *btn_reboot = lv_list_add_button(list, LV_SYMBOL_POWER, "Reboot Device");
    lv_obj_add_event_cb(btn_reboot, reboot_btn_cb, LV_EVENT_CLICKED, NULL);

    return cont;
}

// ============================================================================
// WiFi settings
// ============================================================================

static lv_obj_t *s_wifi_keyboard    = nullptr;
static lv_obj_t *s_password_textarea = nullptr;
static lv_obj_t *s_wifi_list        = nullptr;
static lv_obj_t *s_wifi_status_label = nullptr;
static char s_selected_ssid[33]      = "";
static char s_wifi_password[64]      = "";

static void wifi_scan_add_network(const wifi_ap_info_t *ap)
{
    char btn_text[64];
    const char *lock_icon = (ap->authmode != WIFI_AUTH_OPEN) ? "* " : "";
    snprintf(btn_text, sizeof(btn_text), "%s%s (%d dBm)", lock_icon, ap->ssid, ap->rssi);
    lv_obj_t *btn = lv_list_add_button(s_wifi_list, NULL, btn_text);
    char *ssid_copy = (char *)lv_malloc(33);
    if (ssid_copy) {
        strncpy(ssid_copy, ap->ssid, 32);
        ssid_copy[32] = '\0';
        lv_obj_set_user_data(btn, ssid_copy);
    }
}

static void wifi_network_select_cb(lv_event_t *e);

static void wifi_scan_populate(const wifi_ap_info_t *ap_list, uint16_t found)
{
    char buf[64];
    snprintf(buf, sizeof(buf), "Found %d networks", found);
    if (s_wifi_status_label) lv_label_set_text(s_wifi_status_label, buf);
    for (uint16_t i = 0; i < found; i++) {
        wifi_scan_add_network(&ap_list[i]);
        // Add select callback after button creation
        lv_obj_t *btn = lv_obj_get_child(s_wifi_list, lv_obj_get_child_count(s_wifi_list) - 1);
        if (btn) {
            lv_obj_add_event_cb(btn, wifi_network_select_cb, LV_EVENT_CLICKED, NULL);
        }
    }
}

static void wifi_scan_run(void)
{
    if (s_wifi_list) lv_obj_clean(s_wifi_list);
    if (s_wifi_status_label) lv_label_set_text(s_wifi_status_label, "Scanning...");

    wifi_ap_info_t ap_list[20];
    uint16_t found = 0;

    // Release LVGL lock around blocking network scan
    lvgl_port_unlock();
    esp_err_t err = network_scan(ap_list, 20, &found);
    lvgl_port_lock(0);

    if (err != ESP_OK || found == 0) {
        ESP_LOGW(TAG, "WiFi scan: err=%d found=%d", (int)err, (int)found);
        if (s_wifi_status_label) lv_label_set_text(s_wifi_status_label, "No networks found");
        return;
    }
    ESP_LOGI(TAG, "Scan complete: %d networks", found);
    wifi_scan_populate(ap_list, found);
}

static void wifi_scan_btn_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    ESP_LOGI(TAG, "WiFi scan button clicked");
    wifi_scan_run();
}

static void wifi_network_select_cb(lv_event_t *e)
{
    lv_obj_t *btn = (lv_obj_t *)lv_event_get_target(e);
    char *ssid = (char *)lv_obj_get_user_data(btn);
    if (!ssid) return;
    ESP_LOGI(TAG, "Selected network: %s", ssid);
    strncpy(s_selected_ssid, ssid, sizeof(s_selected_ssid) - 1);
    s_selected_ssid[sizeof(s_selected_ssid) - 1] = '\0';

    if (s_password_textarea && s_wifi_keyboard) {
        lv_obj_clear_flag(s_password_textarea, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_wifi_keyboard, LV_OBJ_FLAG_HIDDEN);
        lv_textarea_set_text(s_password_textarea, "");
        if (s_wifi_status_label) {
            char buf[64];
            snprintf(buf, sizeof(buf), "Enter password for: %s", s_selected_ssid);
            lv_label_set_text(s_wifi_status_label, buf);
        }
    }
}

static void wifi_connect_btn_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    if (s_selected_ssid[0] == '\0') {
        ESP_LOGW(TAG, "Connect clicked but no network selected");
        return;
    }
    const char *pwd = lv_textarea_get_text(s_password_textarea);
    strncpy(s_wifi_password, pwd, sizeof(s_wifi_password) - 1);
    s_wifi_password[sizeof(s_wifi_password) - 1] = '\0';
    ESP_LOGI(TAG, "Connecting to %s...", s_selected_ssid);
    if (s_wifi_status_label) lv_label_set_text(s_wifi_status_label, "Connecting...");

    // Hide password input
    if (s_password_textarea && s_wifi_keyboard) {
        lv_obj_add_flag(s_password_textarea, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_wifi_keyboard, LV_OBJ_FLAG_HIDDEN);
    }

    // Release LVGL lock for blocking connect
    lvgl_port_unlock();
    esp_err_t err = network_connect(s_selected_ssid, s_wifi_password);
    if (err == ESP_OK) {
        clock_settings_t cfg;
        if (settings_load(&cfg) == ESP_OK) {
            strncpy(cfg.wifi_ssid, s_selected_ssid, sizeof(cfg.wifi_ssid) - 1);
            cfg.wifi_ssid[sizeof(cfg.wifi_ssid) - 1] = '\0';
            strncpy(cfg.wifi_password, s_wifi_password, sizeof(cfg.wifi_password) - 1);
            cfg.wifi_password[sizeof(cfg.wifi_password) - 1] = '\0';
            cfg.wifi_configured = true;
            settings_save(&cfg);
            ESP_LOGI(TAG, "WiFi credentials saved for SSID: %s", s_selected_ssid);
        }
    }
    lvgl_port_lock(0);

    const char *msg = (err == ESP_OK) ? "Connected! Saved to settings." : "Connection failed";
    if (s_wifi_status_label) lv_label_set_text(s_wifi_status_label, msg);
    ESP_LOGI(TAG, "WiFi connect result for %s: %d", s_selected_ssid, (int)err);
}

static lv_obj_t *create_sub_wifi(lv_obj_t *parent)
{
    lv_obj_t *cont = make_sub_container(parent, "WiFi Settings");
    // Allow scrolling for this screen (keyboard needs space)
    lv_obj_add_flag(cont, LV_OBJ_FLAG_SCROLLABLE);

    // Status label
    s_wifi_status_label = lv_label_create(cont);
    char ssid[33] = "";
    if (network_is_connected() && network_get_ssid(ssid) == ESP_OK) {
        char buf[128];
        snprintf(buf, sizeof(buf), "Connected to: %s", ssid);
        lv_label_set_text(s_wifi_status_label, buf);
    } else {
        lv_label_set_text(s_wifi_status_label, "Not connected");
    }
    lv_obj_set_style_text_color(s_wifi_status_label, lv_color_white(), 0);
    lv_obj_align(s_wifi_status_label, LV_ALIGN_TOP_MID, 0, 50);

    // Scan button
    lv_obj_t *scan_btn = lv_button_create(cont);
    lv_obj_set_size(scan_btn, 200, 50);
    lv_obj_align(scan_btn, LV_ALIGN_TOP_MID, 0, 90);
    lv_obj_add_event_cb(scan_btn, wifi_scan_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *scan_lbl = lv_label_create(scan_btn);
    lv_label_set_text(scan_lbl, LV_SYMBOL_REFRESH " Scan");
    lv_obj_center(scan_lbl);

    // Network list
    s_wifi_list = lv_list_create(cont);
    lv_obj_set_size(s_wifi_list, 700, 200);
    lv_obj_align(s_wifi_list, LV_ALIGN_TOP_MID, 0, 150);

    // Password textarea (hidden initially)
    s_password_textarea = lv_textarea_create(cont);
    lv_obj_set_size(s_password_textarea, 550, 50);
    lv_obj_align(s_password_textarea, LV_ALIGN_TOP_LEFT, 10, 360);
    lv_textarea_set_placeholder_text(s_password_textarea, "Enter password...");
    lv_textarea_set_password_mode(s_password_textarea, true);
    lv_obj_add_flag(s_password_textarea, LV_OBJ_FLAG_HIDDEN);

    // Connect button
    lv_obj_t *conn_btn = lv_button_create(cont);
    lv_obj_set_size(conn_btn, 120, 50);
    lv_obj_align_to(conn_btn, s_password_textarea, LV_ALIGN_OUT_RIGHT_MID, 10, 0);
    lv_obj_add_event_cb(conn_btn, wifi_connect_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *conn_lbl = lv_label_create(conn_btn);
    lv_label_set_text(conn_lbl, "Connect");
    lv_obj_center(conn_lbl);

    // On-screen keyboard (hidden initially)
    s_wifi_keyboard = lv_keyboard_create(cont);
    lv_keyboard_set_textarea(s_wifi_keyboard, s_password_textarea);
    lv_obj_set_size(s_wifi_keyboard, 750, 220);
    lv_obj_align(s_wifi_keyboard, LV_ALIGN_TOP_MID, 0, 420);
    lv_obj_add_flag(s_wifi_keyboard, LV_OBJ_FLAG_HIDDEN);

    return cont;
}

// ============================================================================
// Brightness settings
// ============================================================================

static void brightness_slider_cb(lv_event_t *e)
{
    lv_obj_t *slider = (lv_obj_t *)lv_event_get_target(e);
    int32_t value = lv_slider_get_value(slider);
    ESP_LOGI(TAG, "Brightness changed to %ld%%", value);
    bsp_display_brightness_set(value);

    lv_obj_t *label = (lv_obj_t *)lv_event_get_user_data(e);
    if (label) {
        char buf[32];
        snprintf(buf, sizeof(buf), "Brightness: %ld%%", value);
        lv_label_set_text(label, buf);
    }

    // Save to NVS (release LVGL lock)
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

static lv_obj_t *create_sub_brightness(lv_obj_t *parent)
{
    lv_obj_t *cont = make_sub_container(parent, "Brightness");

    // Current brightness label
    lv_obj_t *lbl = lv_label_create(cont);
    clock_settings_t cfg;
    int current = 50;
    if (settings_load(&cfg) == ESP_OK) {
        current = cfg.brightness;
    }
    char buf[32];
    snprintf(buf, sizeof(buf), "Brightness: %d%%", current);
    lv_label_set_text(lbl, buf);
    lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_24, 0);
    lv_obj_align(lbl, LV_ALIGN_CENTER, 0, -60);

    // Slider
    lv_obj_t *slider = lv_slider_create(cont);
    lv_obj_set_width(slider, 600);
    lv_slider_set_range(slider, 10, 100);
    lv_slider_set_value(slider, current, LV_ANIM_OFF);
    lv_obj_align(slider, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_event_cb(slider, brightness_slider_cb, LV_EVENT_VALUE_CHANGED, lbl);

    // Instruction text
    lv_obj_t *instr = lv_label_create(cont);
    lv_label_set_text(instr, "Slide to adjust screen brightness");
    lv_obj_set_style_text_color(instr, lv_color_hex(0x808080), 0);
    lv_obj_align(instr, LV_ALIGN_CENTER, 0, 60);

    return cont;
}

// ============================================================================
// Background selector
// ============================================================================

static bool bg_is_image_file(const char *ext)
{
    return (strcasecmp(ext, ".png") == 0 || strcasecmp(ext, ".gif") == 0);
}

static void background_select_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    const char *filename = (const char *)lv_event_get_user_data(e);
    if (!filename) return;
    ESP_LOGI(TAG, "Background selected: %s", filename);

    // Save to NVS
    lvgl_port_unlock();
    clock_settings_t cfg;
    esp_err_t err = settings_load(&cfg);
    if (err == ESP_OK) {
        snprintf(cfg.background_image, sizeof(cfg.background_image), "A:%s", filename);
        err = settings_save(&cfg);
    }
    lvgl_port_lock(0);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Background save failed: %d", (int)err);
        return;
    }
    ESP_LOGI(TAG, "Background saved: A:%s", filename);
    ui_refresh_background();
    show_success_and_pop("Background updated!");
}

static int bg_scan_directory(lv_obj_t *list)
{
    DIR *dir = opendir("/sdcard");
    if (!dir) {
        ESP_LOGE(TAG, "Failed to open /sdcard directory");
        return -1;
    }
    int count = 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_type == DT_DIR) continue;
        const char *ext = strrchr(entry->d_name, '.');
        if (!ext || !bg_is_image_file(ext)) continue;

        bool is_gif = (strcasecmp(ext, ".gif") == 0);
        const char *icon = is_gif ? LV_SYMBOL_LOOP : LV_SYMBOL_IMAGE;
        char filepath[256];
        snprintf(filepath, sizeof(filepath), "/%s", entry->d_name);
        lv_obj_t *btn = lv_list_add_button(list, icon, entry->d_name);
        // strdup the path so callback has valid pointer
        lv_obj_add_event_cb(btn, background_select_cb, LV_EVENT_CLICKED,
                            (void *)strdup(filepath));
        count++;
    }
    closedir(dir);
    return count;
}

static lv_obj_t *create_sub_background(lv_obj_t *parent)
{
    lv_obj_t *cont = make_sub_container(parent, "Select Background");

    lv_obj_t *list = lv_list_create(cont);
    lv_obj_set_size(list, 700, 380);
    lv_obj_align(list, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_text_font(list, &lv_font_montserrat_20, 0);

    if (!sdcard_is_mounted()) {
        lv_obj_t *btn = lv_list_add_button(list, LV_SYMBOL_WARNING, "SD card not mounted");
        lv_obj_clear_flag(btn, LV_OBJ_FLAG_CLICKABLE);
        return cont;
    }

    int count = bg_scan_directory(list);
    if (count < 0) {
        lv_obj_t *btn = lv_list_add_button(list, LV_SYMBOL_WARNING, "Failed to read SD card");
        lv_obj_clear_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    } else if (count == 0) {
        lv_obj_t *btn = lv_list_add_button(list, LV_SYMBOL_WARNING, "No PNG/GIF files found");
        lv_obj_clear_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    } else {
        ESP_LOGI(TAG, "Found %d background files on SD card", count);
    }

    return cont;
}

// ============================================================================
// Text color settings
// ============================================================================

struct color_preset_t {
    uint32_t    color;
    const char *name;
};

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
static constexpr int NUM_COLOR_PRESETS = sizeof(COLOR_PRESETS) / sizeof(COLOR_PRESETS[0]);

static void color_select_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    uint32_t color_value = (uint32_t)(uintptr_t)lv_event_get_user_data(e);
    ESP_LOGI(TAG, "Text color selected: 0x%06lX", color_value);

    lvgl_port_unlock();
    clock_settings_t cfg;
    esp_err_t err = settings_load(&cfg);
    if (err == ESP_OK) {
        cfg.text_color = color_value;
        err = settings_save(&cfg);
    }
    lvgl_port_lock(0);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Color save failed: %d", (int)err);
        return;
    }
    ESP_LOGI(TAG, "Text color saved: 0x%06lX", color_value);
    ui_refresh_text_color();
    show_success_and_pop("Text color updated!");
}

static lv_obj_t *create_sub_text_color(lv_obj_t *parent)
{
    lv_obj_t *cont = make_sub_container(parent, "Text Color");

    clock_settings_t cfg;
    uint32_t current_color = 0xFFFFFF;
    if (settings_load(&cfg) == ESP_OK) {
        current_color = cfg.text_color;
    }

    lv_obj_t *list = lv_list_create(cont);
    lv_obj_set_size(list, 700, 380);
    lv_obj_align(list, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_text_font(list, &lv_font_montserrat_20, 0);

    for (int i = 0; i < NUM_COLOR_PRESETS; i++) {
        lv_obj_t *btn = lv_list_add_button(list, LV_SYMBOL_BULLET, COLOR_PRESETS[i].name);
        lv_obj_add_event_cb(btn, color_select_cb, LV_EVENT_CLICKED,
                            (void *)(uintptr_t)COLOR_PRESETS[i].color);
        if (COLOR_PRESETS[i].color == current_color) {
            lv_obj_add_state(btn, LV_STATE_FOCUSED);
        }
    }

    return cont;
}

// ============================================================================
// Animation preview
// ============================================================================

#if LV_USE_LOTTIE

#define LOTTIE_FILE_PATH   "/sdcard/hummingbird.json"
#define LOTTIE_LOAD_STACK  (64 * 1024)
#define LOTTIE_W           200
#define LOTTIE_H           200

static uint8_t *s_lottie_buf = nullptr;

struct lottie_load_arg_t {
    lv_obj_t *widget;
    char      path[256];
};

static void lottie_load_task(void *arg)
{
    auto *a = static_cast<lottie_load_arg_t *>(arg);
    lv_obj_t *widget = a->widget;
    char path[256];
    strlcpy(path, a->path, sizeof(path));
    free(a);

    ESP_LOGI(TAG, "lottie_load_task: start path='%s'", path);

    struct stat st;
    if (stat(path, &st) != 0) {
        ESP_LOGE(TAG, "lottie_load_task: file not found: '%s' (errno=%d %s)",
                 path, errno, strerror(errno));
        vTaskDeleteWithCaps(NULL);
        return;
    }
    ESP_LOGI(TAG, "lottie_load_task: file found, size=%lld bytes", (long long)st.st_size);

    lvgl_port_lock(0);
    lv_lottie_set_src_file(widget, path);
    ESP_LOGI(TAG, "lottie_load_task: lv_lottie_set_src_file returned, stack_hw=%lu",
             (unsigned long)uxTaskGetStackHighWaterMark(NULL) * sizeof(StackType_t));
    lvgl_port_unlock();

    vTaskDeleteWithCaps(NULL);
}

static lv_obj_t *anim_create_widget(lv_obj_t *scr)
{
    lv_obj_t *widget = lv_lottie_create(scr);
    if (!widget) {
        ESP_LOGE(TAG, "anim: lv_lottie_create returned NULL");
        return nullptr;
    }
    if (lv_lottie_render_failed(widget)) {
        ESP_LOGE(TAG, "anim: render task failed to start");
        lv_obj_delete(widget);
        return nullptr;
    }

    // Allocate render buffer if needed
    if (!s_lottie_buf) {
        uint32_t stride = lv_draw_buf_width_to_stride(LOTTIE_W,
                            LV_COLOR_FORMAT_ARGB8888_PREMULTIPLIED);
        size_t buf_sz = (size_t)stride * LOTTIE_H;
        ESP_LOGI(TAG, "anim: allocating SPIRAM render buf %zu B (stride=%lu)",
                 buf_sz, (unsigned long)stride);
        s_lottie_buf = (uint8_t *)heap_caps_aligned_alloc(64, buf_sz, MALLOC_CAP_SPIRAM);
        if (!s_lottie_buf) {
            ESP_LOGE(TAG, "anim: SPIRAM alloc FAILED");
            lv_obj_delete(widget);
            return nullptr;
        }
    }

    lv_obj_set_size(widget, LOTTIE_W, LOTTIE_H);
    lv_lottie_set_buffer(widget, LOTTIE_W, LOTTIE_H, s_lottie_buf);
    lv_obj_align(widget, LV_ALIGN_CENTER, 0, 0);
    return widget;
}

static void anim_spawn_load_task(lv_obj_t *widget, const char *path)
{
    auto *a = static_cast<lottie_load_arg_t *>(malloc(sizeof(lottie_load_arg_t)));
    if (!a) {
        ESP_LOGE(TAG, "anim: failed to alloc load task arg");
        return;
    }
    a->widget = widget;
    strlcpy(a->path, path, sizeof(a->path));

    ESP_LOGI(TAG, "anim: spawning lottie_load_task (stack=%d B, DRAM-forced)", LOTTIE_LOAD_STACK);
    BaseType_t ret = xTaskCreateWithCaps(lottie_load_task, "lottie_load",
                                         LOTTIE_LOAD_STACK, a, 5, NULL,
                                         MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "anim: xTaskCreateWithCaps lottie_load FAILED (ret=%d)", (int)ret);
        free(a);
    }
}

#endif // LV_USE_LOTTIE

static lv_obj_t *create_sub_animation(lv_obj_t *parent)
{
    lv_obj_t *cont = make_sub_container(parent, "Animation Preview");

#if LV_USE_LOTTIE
    size_t free_heap = esp_get_free_heap_size();
    ESP_LOGI(TAG, "Animation preview — free heap: %lu SPIRAM: %lu",
             (unsigned long)free_heap,
             (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    if (free_heap < 500000) {
        lv_obj_t *lbl = lv_label_create(cont);
        lv_label_set_text(lbl, "ERROR: Insufficient memory\nRequires 500 KB free heap");
        lv_obj_set_style_text_color(lbl, lv_color_make(255, 100, 100), 0);
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 0);
        return cont;
    }

    lv_obj_t *widget = anim_create_widget(cont);
    if (widget) {
        anim_spawn_load_task(widget, LOTTIE_FILE_PATH);
    } else {
        lv_obj_t *lbl = lv_label_create(cont);
        lv_label_set_text(lbl, "Animation unavailable\n(insufficient PSRAM for render task)");
        lv_obj_set_style_text_color(lbl, lv_color_make(255, 100, 100), 0);
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 0);
    }
#else
    lv_obj_t *lbl = lv_label_create(cont);
    lv_label_set_text(lbl, "Lottie not enabled\nCONFIG_LV_USE_LOTTIE is not set");
    lv_obj_set_style_text_color(lbl, lv_color_make(255, 200, 100), 0);
    lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 0);
#endif

    return cont;
}

// ============================================================================
// About screen
// ============================================================================

static lv_obj_t *create_sub_about(lv_obj_t *parent)
{
    lv_obj_t *cont = make_sub_container(parent, "About");

    // Gather system info
    size_t free_heap = esp_get_free_heap_size();
    size_t min_heap = esp_get_minimum_free_heap_size();
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);

    char ip_str[16] = "N/A";
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t ip_info;
    if (netif && esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
        snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&ip_info.ip));
    }

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
        (unsigned long)(min_heap / 1024));

    lv_obj_t *lbl = lv_label_create(cont);
    lv_label_set_text(lbl, info);
    lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
    lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 20);

    return cont;
}

// ============================================================================
// Settings FSM state
// ============================================================================

// ============================================================================
// Settings FSM state — out-of-line method implementations
// ============================================================================

void Settings::entry()
{
    set_state_info(DISPLAY_STATE_SETTINGS, "settings");
    minimize_clock();
    display_scheduler_get()->pause();

    // Create root container (invisible, just a parent for sub-screens)
    s_settings_root = lv_obj_create(s_screen);
    lv_obj_set_size(s_settings_root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(s_settings_root, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_settings_root, 0, 0);
    lv_obj_set_style_pad_all(s_settings_root, 0, 0);
    lv_obj_clear_flag(s_settings_root, LV_OBJ_FLAG_SCROLLABLE);

    // Reset sub-screen state
    memset(s_sub_containers, 0, sizeof(s_sub_containers));
    s_sub_top = -1;

    // Show the menu
    show_sub(SUB_MENU);

    fade_in(s_settings_root);

    ESP_LOGI(TAG, "Settings: entry");
}

void Settings::exit()
{
    // Clear wifi globals
    s_wifi_keyboard = nullptr;
    s_password_textarea = nullptr;
    s_wifi_list = nullptr;
    s_wifi_status_label = nullptr;
    s_selected_ssid[0] = '\0';

    // Destroy all sub-screens
    destroy_all_subs();

    if (s_settings_root) {
        lv_obj_del(s_settings_root);
        s_settings_root = nullptr;
    }

    display_scheduler_get()->resume();
    ESP_LOGI(TAG, "Settings: exit");
}

// Swipe-up in settings does nothing (already in settings)
void Settings::react(EvGesture const &) { }

// Settings back returns to clock
void Settings::react(EvSettingsBack const &)
{
    transit<ClockFull>();
}

// Clock updates still work in settings
void Settings::react(EvClockUpdate const &e)
{
    if (s_clock) {
        clock_widget_update(s_clock, &e.time);
    }
}

// Block external state-changing events while in settings.
// Settings callbacks release the LVGL lock during blocking I/O
// (wifi scan, NVS writes).  If the FSM task processes a
// transit-inducing event in that window, Settings::exit() would
// destroy widgets the LVGL task is still using → crash.
void Settings::react(EvSurpriseMessage const &)
{
    ESP_LOGW(TAG, "Surprise message ignored while in settings");
}

void Settings::react(EvForceState const &)
{
    ESP_LOGW(TAG, "Force-state ignored while in settings");
}
