// main/main.cpp

#include "esp_log.h"
#include "esp_err.h"
#include "esp_spiffs.h"
#include "esp_system.h"
#include "esp_task_wdt.h"
#include "bsp/esp-bsp.h"
#include "bsp/display.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "network.h"
#include "time_sync.h"
#include "ui.h"
#include "weather.h"
#include "secrets.h"
#include "settings.h"
#include "ota.h"
#include "sdcard.h"
#include "http_api.h"
#include "debug_log.h"
#include "lvgl.h"
#include "libs/fsdrv/lv_fsdrv.h"

static const char* TAG = "main";

// LVGL assert handler - reset instead of hanging
extern "C" void lv_assert_handler(void) {
    ESP_LOGE(TAG, "========== LVGL ASSERT FAILED ==========");
    ESP_LOGE(TAG, "Heap free: %lu bytes", (unsigned long)esp_get_free_heap_size());
    ESP_LOGE(TAG, "LVGL assertion triggered, resetting device...");
    vTaskDelay(pdMS_TO_TICKS(1000));  // Give time for log to flush
    esp_restart();
}

// Panic handler to log crash information
static void panic_handler_hook(void) {
    ESP_LOGE(TAG, "========== PANIC DETECTED ==========");
    ESP_LOGE(TAG, "Heap free: %lu bytes", (unsigned long)esp_get_free_heap_size());
    ESP_LOGE(TAG, "Min heap ever: %lu bytes", (unsigned long)esp_get_minimum_free_heap_size());

    // Log stack watermarks for all tasks
    char task_list[1024];
    vTaskList(task_list);
    ESP_LOGE(TAG, "Task list:\n%s", task_list);

    ESP_LOGE(TAG, "====================================");
}

#define DEFAULT_FD_NUM      2
#define DEFAULT_MOUNT_POINT ""


esp_err_t bsp_spiffs_init(const char *partition_label, const char *mount_point, size_t max_files)
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

esp_err_t bsp_spiffs_deinit(const char *partition_label)
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
    clock_settings_t cfg;

    ESP_LOGI(TAG, "=== app_main starting ===");

    // Register panic handler
    esp_register_shutdown_handler(panic_handler_hook);
    ESP_LOGI(TAG, "[0] Panic handler registered");

    // 0) Initialize settings
    ESP_LOGI(TAG, "[0] Initializing settings...");
    err = settings_init();
    ESP_LOGI(TAG, "[0] %s", err == ESP_OK
             ? "Settings initialized"
             : esp_err_to_name(err));

    ESP_LOGI(TAG, "[0] Loading settings...");
    err = settings_load(&cfg);
    ESP_LOGI(TAG, "[0] %s", err == ESP_OK
             ? "Settings loaded"
             : esp_err_to_name(err));

    // 1) Mount SPIFFS
    ESP_LOGI(TAG, "[1] Mounting SPIFFS...");
    err = bsp_spiffs_init_default();
    ESP_LOGI(TAG, "[1] %s", err == ESP_OK
             ? "SPIFFS mounted successfully"
             : esp_err_to_name(err));

    // 1.5) Initialize and mount SD card
    ESP_LOGI(TAG, "[1.5] Initializing SD card...");
    sdcard_init();
    err = sdcard_mount(false);  // Don't auto-format, just try to mount
    if (err == ESP_OK) {
        sdcard_info_t info;
        if (sdcard_get_info(&info) == ESP_OK) {
            ESP_LOGI(TAG, "[1.5] SD card mounted: %s", info.card_type);
            ESP_LOGI(TAG, "[1.5] Capacity: %.2f GB, Used: %.2f GB (%.0f%%)",
                     info.total_bytes / (1024.0 * 1024.0 * 1024.0),
                     info.used_bytes / (1024.0 * 1024.0 * 1024.0),
                     (info.used_bytes * 100.0) / info.total_bytes);

            // Create standard directories
            sdcard_create_directories();

            // Start debug logging to SD card
            ESP_LOGI(TAG, "[1.5] Starting debug log capture...");
            err = debug_log_init();
            if (err == ESP_OK) {
                ESP_LOGI(TAG, "[1.5] Debug logging started: %s", debug_log_get_path());
            } else {
                ESP_LOGW(TAG, "[1.5] Failed to start debug logging: %s", esp_err_to_name(err));
            }
        }
    } else {
        ESP_LOGW(TAG, "[1.5] SD card not available: %s", esp_err_to_name(err));
        if (err == ESP_FAIL) {
            ESP_LOGW(TAG, "[1.5] SD card may need formatting");
            ESP_LOGW(TAG, "[1.5] To format, build with sdcard_mount(true) or use UI format option");
        }
        ESP_LOGW(TAG, "[1.5] Continuing without SD card support");
    }

    // 2) Configure & start display
    ESP_LOGI(TAG, "[2] Configuring display…");
    bsp_display_cfg_t disp_cfg = {
        .lvgl_port_cfg  = ESP_LVGL_PORT_INIT_CONFIG(),
        .buffer_size    = 1024 * 600,  // Full-screen buffer for maximum rendering performance
        .double_buffer  = 1,  // Enable double buffering for smooth animations
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
    bsp_display_brightness_set(cfg.brightness);
    ESP_LOGI(TAG, "[2] Brightness set to %d%%", cfg.brightness);

    // Initialize LVGL POSIX filesystem driver for SD card access
    ESP_LOGI(TAG, "[2] Initializing LVGL POSIX filesystem driver");
    lv_fs_posix_init();
    ESP_LOGI(TAG, "[2] LVGL filesystem driver initialized");
    if (lv_indev_t* t = lv_indev_get_next(NULL)) {
        lv_indev_enable(t, cfg.enable_touch);
        ESP_LOGI(TAG, "[2] Touch input %s", cfg.enable_touch ? "enabled" : "disabled");
    } else {
        ESP_LOGW(TAG, "[2] No touch input device found");
    }

    // 3) Splash
    ESP_LOGI(TAG, "[3] Showing splash…");
    ui_show_splash();
    ESP_LOGI(TAG, "[3] Splash done");

    // 4) Wi‑Fi & SNTP
    ESP_LOGI(TAG, "[4] network_init…");
    if (cfg.wifi_configured && cfg.wifi_ssid[0] != '\0') {
        ESP_LOGI(TAG, "[4] Using WiFi credentials from settings");
        err = network_init(cfg.wifi_ssid, cfg.wifi_password);
        ESP_LOGI(TAG, "[4] network_init: %s",
                 err == ESP_OK ? "OK" : esp_err_to_name(err));
        ESP_LOGI(TAG, "[4] Waiting for SNTP…");
        network_wait_for_time();
        ESP_LOGI(TAG, "[4] SNTP sync complete");
    } else {
        ESP_LOGW(TAG, "[4] WiFi not configured! Please configure via touchscreen settings.");
        ESP_LOGW(TAG, "[4] Initializing WiFi infrastructure for scanning...");
        // Initialize network infrastructure but don't connect
        err = network_init_infrastructure();
        ESP_LOGI(TAG, "[4] network_init_infrastructure: %s",
                 err == ESP_OK ? "OK" : esp_err_to_name(err));
    }

    // 5) TZ
    ESP_LOGI(TAG, "[5] Setting TZ to %s", cfg.timezone);
    time_sync_setup(cfg.timezone);

    // 5.5) Initialize OTA and check if new firmware needs validation
    ESP_LOGI(TAG, "[5.5] Initializing OTA...");
    ota_init();
    ota_mark_app_valid();  // Mark this boot as successful
    ESP_LOGI(TAG, "[5.5] Running partition: %s, version: %s",
             ota_get_running_partition(),
             ota_get_current_version());

    // 6) Clock UI
    time_sync_get_local(&now, &ti);
    ESP_LOGI(TAG, "[6] Local time %04d-%02d-%02d %02d:%02d:%02d",
             ti.tm_year+1900, ti.tm_mon+1, ti.tm_mday,
             ti.tm_hour, ti.tm_min, ti.tm_sec);
    weather_init();

    // 6) HTTP API Server
    ESP_LOGI(TAG, "[6] Starting HTTP API server...");
    err = http_api_start();
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "[6] HTTP API server started on port 80");
    } else {
        ESP_LOGW(TAG, "[6] Failed to start HTTP API server: %s", esp_err_to_name(err));
    }

    ESP_LOGI(TAG, "[7] Initializing clock UI…");
    ui_clock_init(&ti, &cfg);
    ESP_LOGI(TAG, "[7] Clock UI up");
    
    // 7) Idle loop with heap monitoring
    ESP_LOGI(TAG, "[7] Entering idle loop with heap monitoring");
    size_t last_free_heap = esp_get_free_heap_size();
    uint32_t loop_count = 0;

    while (true) {
        vTaskDelay(pdMS_TO_TICKS(60000));  // Every 60 seconds
        loop_count++;

        size_t free_heap = esp_get_free_heap_size();
        size_t min_heap = esp_get_minimum_free_heap_size();

        ESP_LOGI(TAG, "[HEALTH] Loop %lu: Heap free=%lu, min=%lu, delta=%ld",
                 (unsigned long)loop_count,
                 (unsigned long)free_heap,
                 (unsigned long)min_heap,
                 (long)(free_heap - last_free_heap));

        if (free_heap < 20000) {
            ESP_LOGW(TAG, "[HEALTH] LOW HEAP WARNING! Free: %lu bytes", (unsigned long)free_heap);
        }

        last_free_heap = free_heap;
    }
}

