// main/main.cpp

#include <stdio.h>
#include <string.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_err.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_sntp.h"
#include "esp_wifi.h"
#include "esp_spiffs.h"
#include "lwip/ip4_addr.h"

#include "lvgl.h"
#include "bsp/esp-bsp.h"
#include "bsp/display.h"

static const char *TAG = "main";

// Event group & bit for signalling Wi-Fi up
static EventGroupHandle_t s_wifi_event_group;
static const int WIFI_CONNECTED_BIT = BIT0;

// IPSTR/IP2STR macros for logging an IPv4 address
#ifndef IPSTR
#define IPSTR "%d.%d.%d.%d"
#endif
#ifndef IP2STR
#define IP2STR(addr) \
    ((addr)->addr & 0xff), \
    (((addr)->addr >> 8) & 0xff), \
    (((addr)->addr >> 16) & 0xff), \
    (((addr)->addr >> 24) & 0xff)
#endif


#define DEFAULT_FD_NUM      2
#define DEFAULT_MOUNT_POINT ""


esp_err_t bsp_spiffs_init(char *partition_label, char *mount_point, size_t max_files)
{
    esp_vfs_spiffs_conf_t conf = {
        .base_path = mount_point,
        .partition_label = partition_label,
        .max_files = max_files,
        .format_if_mount_failed = false,
    };

    esp_err_t ret_val = esp_vfs_spiffs_register(&conf);

    if (ESP_OK != ret_val) {
        ESP_LOGE(TAG, "SPIFFS register failed: %d", ret_val);
        return ret_val;
    }

    size_t total = 0, used = 0;

    ret_val = esp_spiffs_info(conf.partition_label, &total, &used);
    if (ret_val != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get SPIFFS partition information (%s)", esp_err_to_name(ret_val));
    }
    else {
        ESP_LOGI(TAG, "Partition size: total: %d, used: %d", total, used);
    }

    return ret_val;
}

esp_err_t bsp_spiffs_init_default(void)
{
    return bsp_spiffs_init(NULL, DEFAULT_MOUNT_POINT, DEFAULT_FD_NUM);
}

esp_err_t bsp_spiffs_deinit(char *partition_label)
{
    return esp_vfs_spiffs_unregister(partition_label);
}

esp_err_t bsp_spiffs_deinit_default(void)
{
    return bsp_spiffs_deinit(NULL);
}

static void event_handler(void* arg, esp_event_base_t event_base,
                          int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT) {
        if (event_id == WIFI_EVENT_STA_START) {
            esp_wifi_connect();
        } else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
            ESP_LOGI(TAG, "Wi-Fi disconnected, retrying...");
            esp_wifi_connect();
        }
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* evt = (ip_event_got_ip_t*)event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&evt->ip_info.ip));
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);

        // Now DNS is ready—start SNTP
        ESP_LOGI(TAG, "Starting SNTP");
        esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
        esp_sntp_setservername(0, "pool.ntp.org");
        esp_sntp_init();
    }
}

// Instead of checking esp_sntp_get_sync_status(), we poll the calendar time directly.
// We assume that tm_year < 120 (i.e., year < 2020) means “not yet set.”
static void wait_for_sntp_sync(void)
{
    const int max_retries = 10;
    const TickType_t delay = pdMS_TO_TICKS(2000);
    time_t now;
    struct tm timeinfo;
    int retry = 0;

    while (retry < max_retries) {
        time(&now);
        localtime_r(&now, &timeinfo);

        if (timeinfo.tm_year >= (2020 - 1900)) {
            ESP_LOGI(TAG,
                     "System time set: %04d-%02d-%02d %02d:%02d:%02d",
                     timeinfo.tm_year + 1900,
                     timeinfo.tm_mon  + 1,
                     timeinfo.tm_mday,
                     timeinfo.tm_hour,
                     timeinfo.tm_min,
                     timeinfo.tm_sec);
            return;
        }

        ESP_LOGI(TAG,
                 "Waiting for system time to be set... (%d/%d)",
                 retry + 1, max_retries);
        retry++;
        vTaskDelay(delay);
    }

    ESP_LOGW(TAG, "System time not set after %d retries, proceeding anyway", max_retries);
}

static void show_splash_screen(uint32_t display_delay_ms = 3000)
{
    lvgl_port_lock(0);

    // Clean the active screen
    lv_obj_t *scr = lv_scr_act();
    lv_obj_clean(scr);

    // Create an LVGL image object
    LV_IMG_DECLARE(splash);
    lv_obj_t *img = lv_img_create(lv_scr_act());
    lv_img_set_src(img, &splash);
    lv_obj_align(img, LV_ALIGN_CENTER, 0, 0);
    lvgl_port_unlock();

}

static lv_obj_t *lbl_hm, *lbl_ampm, *lbl_sec;

// 2) Define an LVGL timer callback
static void update_clock_cb(lv_timer_t* timer) {

    lvgl_port_lock(0);
    time_t now = time(NULL);
    struct tm ti;
    localtime_r(&now, &ti);

    // 12-hour conversion + AM/PM
    int h12 = ti.tm_hour % 12;
    if (h12 == 0) h12 = 12;
    const char *ampm = (ti.tm_hour < 12) ? "AM" : "PM";

    // Format
    static char buf_hm[8];
    static char buf_sec[4];
    snprintf(buf_hm, sizeof(buf_hm), "%02d:%02d", h12, ti.tm_min);
    snprintf(buf_sec, sizeof(buf_sec), "%02d", ti.tm_sec);

    // Apply
    lv_label_set_text(lbl_hm,    buf_hm);
    lv_label_set_text(lbl_ampm,  ampm);
    lv_label_set_text(lbl_sec,   buf_sec);
    lvgl_port_unlock();
}

extern "C" void app_main(void)
{
    // 1) NVS init
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    

    ESP_ERROR_CHECK(bsp_spiffs_init_default());
    ESP_LOGI(TAG, "SPIFFS mount successfully");

    // Start the LCD (from esp32-p4-function-ev BSP)
    bsp_display_cfg_t disp_cfg = {
        .lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG(),
        .buffer_size   = BSP_LCD_DRAW_BUFF_SIZE,
        .double_buffer = BSP_LCD_DRAW_BUFF_DOUBLE,
        .hw_cfg = {
            .hdmi_resolution = BSP_HDMI_RES_NONE,
            .dsi_bus = {
                .phy_clk_src = MIPI_DSI_PHY_CLK_SRC_DEFAULT,
                .lane_bit_rate_mbps = BSP_LCD_MIPI_DSI_LANE_BITRATE_MBPS,
            }
        },
        .flags         = {
            .buff_dma    = true,
            .buff_spiram = false,
            .sw_rotate   = false,
        }
    };
    bsp_display_start_with_config(&disp_cfg);
    bsp_display_backlight_on();

    show_splash_screen();

    // 2) TCP/IP & event loop
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // 3) Default STA netif
    esp_netif_create_default_wifi_sta();

    // 4) Init Wi-Fi (uses esp_wifi_remote under the hood)
    wifi_init_config_t wifi_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wifi_cfg));

    // 5) Register handlers
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL, NULL));

    // 6) Create event group & start Wi-Fi
    s_wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    wifi_config_t sta_cfg = {
        .sta = {
            .ssid     = "Dudeybear",
            .password = "Entropy! 23",
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_cfg));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "Wi-Fi started, waiting for IP...");

    // 7) Wait for connection & IP
    xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT,
                        pdFALSE, pdTRUE, portMAX_DELAY);
    ESP_LOGI(TAG, "Wi-Fi connected");

    // 8) Timezone & SNTP sync
    setenv("TZ", "EST5EDT,M3.2.0/2,M11.1.0/2", 1);
    tzset();
    wait_for_sntp_sync();

    lvgl_port_lock(0);
    lv_obj_t* scr = lv_scr_act();
    lbl_hm   = lv_label_create(scr);
    lbl_ampm = lv_label_create(scr);
    lbl_sec  = lv_label_create(scr);
    
    LV_FONT_DECLARE(wg_sunrise_128);
    // 2) Pick fonts & color
    lv_obj_set_style_text_font(lbl_hm,   &wg_sunrise_128, 0);
    lv_obj_set_style_text_color(lbl_hm,  lv_color_white(), 0);

    lv_obj_set_style_text_font(lbl_ampm, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(lbl_ampm,lv_color_white(), 0);

    lv_obj_set_style_text_font(lbl_sec,  &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(lbl_sec, lv_color_white(), 0);

    // 1) Center “HH:MM” near the top‐center of the screen:
    lv_obj_align(lbl_hm, LV_ALIGN_CENTER, 0, -60);

    // 2) Put “AM/PM” just to the top‐right of the HH:MM label
    lv_obj_align_to(
        lbl_ampm,
        lbl_hm,
        LV_ALIGN_OUT_RIGHT_TOP, // place the top‐left of this at the top‐right of HH:MM
        8,                       // 8 px gap
        0
    );

    // 3) Put “SS” just below the HH:MM label, centered
    lv_obj_align_to(
        lbl_sec,
        lbl_hm,
        LV_ALIGN_OUT_BOTTOM_MID, // place the top‐middle of SS at the bottom‐middle of HH:MM
        0,
        8                        // 8 px gap
    );

    lvgl_port_unlock();

    lv_timer_create(update_clock_cb, 1000, NULL);

    // 9) Periodically print the current time every second
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));

    }
}
