/**
 * @file state_settings.cpp
 * v2: colours bound to the theme (ui_bind_*): repainted live on a theme change.
 * v3: the settings screens are settings_app (shared with the phone panel); this file is the
 *     clock's settings backend, its device-only screens (Wi-Fi, animation preview) and the
 *     Settings FSM state.
 * v4: the Phone screen (QR pairing for the phone panel).
 * v5: no global setters; the theme editor keeps named themes.
 * @version 5
 * @brief Settings state: the clock's settings backend (NVS settings, SD lists, the schedule
 *        store, the display) for settings_app, the Wi-Fi and animation preview screens, and
 *        the FSM state that opens and closes the settings.
 */

#include "display_states.h"
#include "display_scheduler.h"
#include "display_fsm.h"
#include "clock_animation.h"

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
#include "ui_theme_bind.h"
#include "theme_store.h"
#include "settings_app.h"
#include "settings_frame.h"
#include "sched_store.h"
#include "phone_view.h"
#include "auth.h"
#include "settings_lists.h"
}

#include <cstring>
#include <cerrno>
#include <ctime>
#include <dirent.h>
#include <strings.h>
#include <sys/stat.h>

static const char *TAG = "state_settings";


static lv_obj_t *s_settings_root = nullptr;  // root container for all settings

// ============================================================================
// WiFi (device only)
// ============================================================================

static lv_obj_t *s_wifi_keyboard    = nullptr;
static lv_obj_t *s_password_textarea = nullptr;
static lv_obj_t *s_wifi_list        = nullptr;
static lv_obj_t *s_wifi_status_label = nullptr;
static char s_selected_ssid[33]      = "";
static char s_wifi_password[64]      = "";

// settings_update() mutators: each sets one field on the freshly loaded settings under the
// settings lock (atomic read-modify-write; other tasks also write settings).
/**
 * @brief settings_update mutator: store the selected SSID/password and mark Wi-Fi configured.
 * @version 1
 * @utility
 * @return true (always save).
 */
static bool wifi_creds_mutator(clock_settings_t *cfg, void *)
{
    strlcpy(cfg->wifi_ssid, s_selected_ssid, sizeof(cfg->wifi_ssid));
    strlcpy(cfg->wifi_password, s_wifi_password, sizeof(cfg->wifi_password));
    cfg->wifi_configured = true;
    return true;
}

/**
 * @brief Append a network button (lock marker, SSID, RSSI) to the WiFi list, storing an lv_malloc'd SSID copy as user data.
 * @version 1
 * @utility
 */
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

/**
 * @brief Update the status label with the scan count and add a selectable button per found network.
 * @version 1
 * @req REQ-NET-002
 */
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

/**
 * @brief Clear the list, run a blocking WiFi scan with the LVGL lock released, then populate results or report none found.
 * @version 1
 * @req REQ-NET-002
 */
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

/**
 * @brief Scan button handler: run a WiFi scan.
 * @version 1
 * @callback
 * @req REQ-NET-002
 */
static void wifi_scan_btn_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    ESP_LOGI(TAG, "WiFi scan button clicked");
    wifi_scan_run();
}

/**
 * @brief Network button handler: record the selected SSID and reveal the password textarea and keyboard.
 * @version 1
 * @callback
 * @req REQ-NET-002
 */
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

/**
 * @brief Connect button handler: connect to the selected SSID with the LVGL lock released and persist credentials to NVS on success.
 * v2: atomic read-modify-write via settings_update (no lost concurrent writes).
 * @version 2
 * @callback
 * @req REQ-NET-002
 * @req REQ-UI-001
 */
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
        if (settings_update(wifi_creds_mutator, nullptr) == ESP_OK) {
            ESP_LOGI(TAG, "WiFi credentials saved for SSID: %s", s_selected_ssid);
        }
    }
    lvgl_port_lock(0);

    const char *msg = (err == ESP_OK) ? "Connected! Saved to settings." : "Connection failed";
    if (s_wifi_status_label) lv_label_set_text(s_wifi_status_label, msg);
    ESP_LOGI(TAG, "WiFi connect result for %s: %d", s_selected_ssid, (int)err);
}

/**
 * @brief The WiFi screen: status, scan, network list, hidden password entry, connect and
 *        keyboard.
 * v2: a settings_app screen (was a sub-screen of the old settings stack).
 * @version 2
 * @req REQ-UI-001
 * @req REQ-NET-002
 * @return The frame.
 */
static lv_obj_t *wifi_screen(lv_obj_t *parent)
{
    lv_obj_t *cont = settings_frame_create(parent, "WiFi Settings", settings_app_back_cb);
    lv_obj_add_flag(cont, LV_OBJ_FLAG_SCROLLABLE);   // the keyboard needs the room

    s_wifi_status_label = lv_label_create(cont);
    char ssid[33] = "";
    if (network_is_connected() && network_get_ssid(ssid) == ESP_OK) {
        char buf[128];
        snprintf(buf, sizeof(buf), "Connected to: %s", ssid);
        lv_label_set_text(s_wifi_status_label, buf);
    } else {
        lv_label_set_text(s_wifi_status_label, "Not connected");
    }
    ui_bind_text(s_wifi_status_label, UI_ROLE_TEXT);
    lv_obj_align(s_wifi_status_label, LV_ALIGN_TOP_MID, 0, 50);

    lv_obj_t *scan_btn = lv_button_create(cont);
    lv_obj_set_size(scan_btn, 200, 50);
    lv_obj_align(scan_btn, LV_ALIGN_TOP_MID, 0, 90);
    lv_obj_add_event_cb(scan_btn, wifi_scan_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *scan_lbl = lv_label_create(scan_btn);
    lv_label_set_text(scan_lbl, LV_SYMBOL_REFRESH " Scan");
    lv_obj_center(scan_lbl);

    s_wifi_list = lv_list_create(cont);
    lv_obj_set_size(s_wifi_list, 700, 200);
    lv_obj_align(s_wifi_list, LV_ALIGN_TOP_MID, 0, 150);

    s_password_textarea = lv_textarea_create(cont);
    lv_obj_set_size(s_password_textarea, 550, 50);
    lv_obj_align(s_password_textarea, LV_ALIGN_TOP_LEFT, 10, 360);
    lv_textarea_set_placeholder_text(s_password_textarea, "Enter password...");
    lv_textarea_set_password_mode(s_password_textarea, true);
    lv_obj_add_flag(s_password_textarea, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *conn_btn = lv_button_create(cont);
    lv_obj_set_size(conn_btn, 120, 50);
    lv_obj_align_to(conn_btn, s_password_textarea, LV_ALIGN_OUT_RIGHT_MID, 10, 0);
    lv_obj_add_event_cb(conn_btn, wifi_connect_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *conn_lbl = lv_label_create(conn_btn);
    lv_label_set_text(conn_lbl, "Connect");
    lv_obj_center(conn_lbl);

    s_wifi_keyboard = lv_keyboard_create(cont);
    lv_keyboard_set_textarea(s_wifi_keyboard, s_password_textarea);
    lv_obj_set_size(s_wifi_keyboard, 750, 220);
    lv_obj_align(s_wifi_keyboard, LV_ALIGN_TOP_MID, 0, 420);
    lv_obj_add_flag(s_wifi_keyboard, LV_OBJ_FLAG_HIDDEN);
    return cont;
}

// ============================================================================
// Animation preview (device only)
// ============================================================================

#if LV_USE_LOTTIE

#define LOTTIE_FILE_PATH   "/sdcard/lottie/hummingbird.json"
#define LOTTIE_W           200
#define LOTTIE_H           200

static uint8_t *s_lottie_buf = nullptr;

/**
 * @brief Create a centered Lottie widget and attach a shared 64-byte aligned SPIRAM render buffer.
 * @version 1
 * @utility
 * @return The Lottie widget, or nullptr if creation, render task start or buffer allocation fails.
 */
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

/**
 * @brief Queue an asynchronous Lottie file load for the widget via display_fsm_load_lottie().
 * @version 1
 * @utility
 */
static void anim_spawn_load_task(lv_obj_t *widget, const char *path)
{
    lottie_load_job_t job = {};
    job.widget     = widget;
    job.target_fps = 0;  // use default animation speed
    strlcpy(job.path, path, sizeof(job.path));
    display_fsm_load_lottie(&job);
    ESP_LOGI(TAG, "anim: Lottie load queued '%s'", path);
}

#endif // LV_USE_LOTTIE

/**
 * @brief The animation preview screen: a Lottie preview if enabled and the heap allows,
 *        otherwise why not.
 * v2: a settings_app screen.
 * @version 2
 * @utility
 * @return The frame.
 */
static lv_obj_t *preview_screen(lv_obj_t *parent)
{
    lv_obj_t *cont = settings_frame_create(parent, "Animation Preview", settings_app_back_cb);
    const char *problem = nullptr;
#if LV_USE_LOTTIE
    size_t free_heap = esp_get_free_heap_size();
    ESP_LOGI(TAG, "Animation preview: free heap %lu, SPIRAM %lu", (unsigned long)free_heap,
             (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    lv_obj_t *widget = free_heap >= 500000 ? anim_create_widget(cont) : nullptr;
    if (widget) anim_spawn_load_task(widget, LOTTIE_FILE_PATH);
    else problem = free_heap < 500000 ? "Not enough memory (needs 500 KB free)"
                                      : "Animation unavailable (no PSRAM for its render task)";
#else
    problem = "Lottie is not enabled (CONFIG_LV_USE_LOTTIE)";
#endif
    if (problem) {
        lv_obj_t *lbl = settings_text(cont, problem, &lv_font_montserrat_20, UI_ROLE_WARN);
        lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 0);
    }
    return cont;
}

// ============================================================================
// Phone (device only): pair a phone by QR code
// ============================================================================

#define PANEL_PATH "/www/panel/index.html"

static lv_timer_t *s_phone_timer = nullptr;

/**
 * @brief The clock's IPv4 address as text ("N/A" when it has none).
 * @version 1
 * @utility
 */
static void device_ip(char *out, size_t n)
{
    snprintf(out, n, "N/A");
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t ip_info;
    if (netif && esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
        snprintf(out, n, IPSTR, IP2STR(&ip_info.ip));
    }
}

/**
 * @brief Once a second: the code's time left and the connected phones.
 * @version 1
 * @callback
 */
static void phone_tick_cb(lv_timer_t *t)
{
    LV_UNUSED(t);
    phone_view_update(auth_pair_secs_left(), auth_sessions());
}

/**
 * @brief The Phone screen closes: withdraw the code (connected phones stay), stop the tick.
 * @version 1
 * @callback
 */
static void phone_closed_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    auth_pair_end();
    if (s_phone_timer) lv_timer_delete(s_phone_timer);
    s_phone_timer = nullptr;
}

/**
 * @brief Disconnect every phone and offer a fresh code.
 * @version 1
 * @callback
 */
static void phone_disconnect_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    auth_revoke_all();
    settings_app_refresh();   // a new screen, a new code
}

/**
 * @brief The Phone screen: offer a pairing code and show it as a QR code of the panel's
 *        address with the code after '#' (a fragment: the browser never sends it in a URL).
 * @version 1
 * @req REQ-UI-005
 * @return The frame.
 */
static lv_obj_t *phone_screen(lv_obj_t *parent)
{
    char code[AUTH_TOKEN_LEN], ip[16], url[128];
    auth_pair_begin(code);
    device_ip(ip, sizeof(ip));
    snprintf(url, sizeof(url), "http://%s" PANEL_PATH "#%s", ip, code);
    lv_obj_t *frame = phone_view_create(parent, url, settings_app_back_cb, phone_disconnect_cb);
    lv_obj_add_event_cb(frame, phone_closed_cb, LV_EVENT_DELETE, nullptr);
    if (!s_phone_timer) s_phone_timer = lv_timer_create(phone_tick_cb, 1000, nullptr);
    phone_tick_cb(nullptr);
    return frame;
}

// ============================================================================
// The clock's settings backend
// ============================================================================

/**
 * @brief A list to choose from (SD and theme files read off the LVGL lock).
 * v2: settings_lists_fill (shared with the phone panel's API).
 * @version 2
 * @callback
 * @return How many.
 */
static int be_list(settings_list_t which, settings_choice_t *out, int max)
{
    lvgl_port_unlock();
    int n = settings_lists_fill(which, out, max);
    lvgl_port_lock(0);
    return n;
}

/**
 * @brief The schedule.
 * @version 1
 * @callback
 * @return How many profiles.
 */
static int be_schedule_get(sched_rule_t *out, int max)
{
    return sched_store_get(out, max);
}

/**
 * @brief Save the schedule (SD write and the year's overlap check off the LVGL lock), then
 *        re-apply the showing profile.
 * @version 1
 * @callback
 */
static void be_schedule_set(const sched_rule_t *r, int n, settings_done_cb_t done, void *ctx)
{
    char why[128] = "";
    lvgl_port_unlock();
    esp_err_t err = sched_store_set(r, n, why, sizeof(why));
    lvgl_port_lock(0);
    if (err == ESP_OK) display_fsm_refresh_background();
    done(err == ESP_OK, why, ctx);
}

/**
 * @brief The device's sun.
 * @version 1
 * @callback
 * @return It.
 */
static const sched_sun_src_t *be_sun(void)
{
    return sched_store_sun();
}

/**
 * @brief Now, as a local date and minute.
 * @version 1
 * @callback
 */
static void be_now(sched_date_t *day, int *min)
{
    time_t now = time(nullptr);
    struct tm t;
    localtime_r(&now, &t);
    *day = { (uint16_t)(t.tm_year + 1900), (uint8_t)(t.tm_mon + 1), (uint8_t)t.tm_mday };
    *min = t.tm_hour * 60 + t.tm_min;
}

/**
 * @brief Keep the edited theme as the theme file @p stem (SD off the LVGL lock); the profile
 *        that uses it is saved by the schedule. Re-applies the theme when the showing one
 *        was replaced.
 * v2: a named theme file (was custom.json made global).
 * @version 2
 * @callback
 */
static void be_save_theme(const ui_theme_t *t, const char *stem, settings_done_cb_t done,
                          void *ctx)
{
    ui_theme_t copy = *t;
    lvgl_port_unlock();
    bool ok = theme_store_save(stem, &copy);
    if (ok) theme_store_rescan();
    lvgl_port_lock(0);
    ESP_LOGI(TAG, "theme editor: '%s' as %s.json: %s", copy.name, stem, ok ? "saved" : "save FAILED");
    if (ok) display_fsm_refresh_theme();
    done(ok, ok ? "" : "Could not write the theme to the SD card", ctx);
}

/**
 * @brief Version, platform, MAC, IP and heap.
 * v2: the IP from device_ip.
 * @version 2
 * @callback
 */
static void be_about(char *out, size_t n)
{
    uint8_t mac[6] = {0};
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char ip_str[16];
    device_ip(ip_str, sizeof(ip_str));
    snprintf(out, n,
             "Greenwood Clock\nVersion: 1.0.0\n\nPlatform: ESP32-P4\nBoard: Function EV Board\n\n"
             "MAC: %02X:%02X:%02X:%02X:%02X:%02X\nIP: %s\n\nFree Heap: %lu KB\nMin Heap: %lu KB",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5], ip_str,
             (unsigned long)(esp_get_free_heap_size() / 1024),
             (unsigned long)(esp_get_minimum_free_heap_size() / 1024));
}

/**
 * @brief Reboot: release the LVGL lock, wait 500 ms, restart.
 * @version 1
 * @callback
 */
static void be_reboot(void)
{
    ESP_LOGI(TAG, "Reboot from settings");
    lvgl_port_unlock();
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
}

/**
 * @brief Back from the menu: leave the settings.
 * @version 1
 * @callback
 */
static void be_leave(void)
{
    display_event_t evt = {};
    evt.type = DISPLAY_EVT_SETTINGS_BACK;
    display_fsm_send_event(&evt);
}

/**
 * @brief The clock's backend.
 * v2: the Phone screen.
 * v3: no global setters (profiles set everything).
 * @version 3
 * @utility
 * @return It (static).
 */
static const settings_backend_t *clock_backend(void)
{
    static settings_backend_t be;
    be.list = be_list;
    be.schedule_get = be_schedule_get;
    be.schedule_set = be_schedule_set;
    be.sun = be_sun;
    be.now = be_now;
    be.save_theme = be_save_theme;
    be.about = be_about;
    be.wifi_create = wifi_screen;
    be.phone_create = phone_screen;
    be.preview_create = preview_screen;
    be.reboot = be_reboot;
    be.leave = be_leave;
    return &be;
}

// ============================================================================
// Settings FSM state
// ============================================================================

/**
 * @brief FSM entry: pause the scheduler and open the settings (settings_app) at the menu,
 *        under the top-bar clock.
 * v2: the screens are settings_app over the clock's backend.
 * @version 2
 * @req REQ-UI-001
 * @req REQ-DISP-002
 */
void Settings::entry()
{
    set_state_info(DISPLAY_STATE_SETTINGS, "settings");
    topbar_clock();
    display_scheduler_get()->pause();

    s_settings_root = lv_obj_create(s_screen);
    lv_obj_set_size(s_settings_root, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_opa(s_settings_root, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_settings_root, 0, 0);
    lv_obj_set_style_pad_all(s_settings_root, 0, 0);
    lv_obj_clear_flag(s_settings_root, LV_OBJ_FLAG_SCROLLABLE);

    settings_frame_set_y(SETTINGS_FRAME_Y);   // under the top-bar clock
    settings_app_open(s_settings_root, clock_backend());
    fade_in(s_settings_root);
    ESP_LOGI(TAG, "Settings: entry");
}

/**
 * @brief FSM exit: forget the WiFi widgets, close the settings, resume the scheduler.
 * v2: settings_app_close.
 * @version 2
 * @req REQ-UI-001
 * @req REQ-DISP-002
 */
void Settings::exit()
{
    s_wifi_keyboard = nullptr;
    s_password_textarea = nullptr;
    s_wifi_list = nullptr;
    s_wifi_status_label = nullptr;
    s_selected_ssid[0] = '\0';

    settings_app_close();
    if (s_settings_root) {
        lv_obj_delete(s_settings_root);
        s_settings_root = nullptr;
    }
    display_scheduler_get()->resume();
    ESP_LOGI(TAG, "Settings: exit");
}

/**
 * @brief Ignore all gestures while in settings (navigation uses on-screen buttons).
 * @version 1
 * @req REQ-UI-001
 * @req REQ-DISP-003
 */
void Settings::react(EvGesture const &) { }

/**
 * @brief Settings back returns to clock.
 * @version 1
 * @req REQ-UI-001
 */
void Settings::react(EvSettingsBack const &)
{
    transit<ClockFull>();
}

/**
 * @brief Clock updates still work in settings: refresh the clock widget time.
 * @version 1
 * @req REQ-UI-001
 * @req REQ-CLOCK-002
 */
void Settings::react(EvClockUpdate const &e)
{
    if (s_clock) {
        clock_widget_update(s_clock, &e.time);
    }
}

/**
 * @brief Block external state-changing events while in settings; surprise messages are ignored.
 *
 * Settings callbacks release the LVGL lock during blocking I/O
 * (wifi scan, NVS writes).  If the FSM task processes a
 * transit-inducing event in that window, Settings::exit() would
 * destroy widgets the LVGL task is still using → crash.
 * @version 1
 * @req REQ-UI-001
 * @req REQ-DISP-001
 */
void Settings::react(EvSurpriseMessage const &)
{
    ESP_LOGW(TAG, "Surprise message ignored while in settings");
}

/**
 * @brief Force-state requests are ignored while in settings (see EvSurpriseMessage handler).
 * @version 1
 * @req REQ-UI-001
 * @req REQ-DISP-001
 */
void Settings::react(EvForceState const &)
{
    ESP_LOGW(TAG, "Force-state ignored while in settings");
}
