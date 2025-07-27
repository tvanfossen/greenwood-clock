// main/main.cpp

#include "esp_log.h"
#include "esp_err.h"
#include "esp_spiffs.h"
#include "bsp/esp-bsp.h"
#include "bsp/display.h"

#include "network.h"
#include "time_sync.h"
#include "ui.h"
#include "weather.h"
#include "secrets.h"

static const char* TAG = "main";

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

extern "C" void app_main()
{
    esp_err_t err;
    time_t now;
    struct tm ti;

    ESP_LOGI(TAG, "=== app_main starting ===");

    // 1) Mount SPIFFS
    ESP_LOGI(TAG, "[1] Mounting SPIFFS...");
    err = bsp_spiffs_init_default();
    ESP_LOGI(TAG, "[1] %s", err == ESP_OK
             ? "SPIFFS mounted successfully"
             : esp_err_to_name(err));

    // 2) Configure & start display
    ESP_LOGI(TAG, "[2] Configuring display…");
    bsp_display_cfg_t disp_cfg = {
        .lvgl_port_cfg  = ESP_LVGL_PORT_INIT_CONFIG(),
        .buffer_size    = BSP_LCD_DRAW_BUFF_SIZE,
        .double_buffer  = BSP_LCD_DRAW_BUFF_DOUBLE,
        .hw_cfg = {
            .hdmi_resolution    = BSP_HDMI_RES_NONE,
            .dsi_bus = {
                .phy_clk_src         = MIPI_DSI_PHY_CLK_SRC_DEFAULT,
                .lane_bit_rate_mbps  = BSP_LCD_MIPI_DSI_LANE_BITRATE_MBPS,
            }
        },
        .flags = { .buff_dma = false, .buff_spiram = true, .sw_rotate = false }
    };
    bsp_display_start_with_config(&disp_cfg);
    bsp_display_backlight_on();
    bsp_display_brightness_set(50);
    if (lv_indev_t* t = lv_indev_get_next(NULL)) {
        lv_indev_enable(t, false);
        ESP_LOGI(TAG, "[2] Touch input disabled");
    }

    // 3) Splash
    ESP_LOGI(TAG, "[3] Showing splash…");
    ui_show_splash();
    ESP_LOGI(TAG, "[3] Splash done");

    // 4) Wi‑Fi & SNTP
    ESP_LOGI(TAG, "[4] network_init…");
    err = network_init("Dudeybear", "Entropy! 23");
    ESP_LOGI(TAG, "[4] network_init: %s",
             err == ESP_OK ? "OK" : esp_err_to_name(err));
    ESP_LOGI(TAG, "[4] Waiting for SNTP…");
    network_wait_for_time();
    ESP_LOGI(TAG, "[4] SNTP sync complete");


    // 5) TZ
    const char *tz = "EST5EDT,M3.2.0/2,M11.1.0/2";
    ESP_LOGI(TAG, "[5] Setting TZ to %s", tz);
    time_sync_setup(tz);

    // 6) Clock UI
    time_sync_get_local(&now, &ti);
    ESP_LOGI(TAG, "[6] Local time %04d-%02d-%02d %02d:%02d:%02d",
             ti.tm_year+1900, ti.tm_mon+1, ti.tm_mday,
             ti.tm_hour, ti.tm_min, ti.tm_sec);
    weather_init();

    ESP_LOGI(TAG, "[6] Initializing clock UI…");
    ui_clock_init(&ti);
    ESP_LOGI(TAG, "[6] Clock UI up");
    
    // 7) Idle loop
    ESP_LOGI(TAG, "[8] Entering idle");
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

