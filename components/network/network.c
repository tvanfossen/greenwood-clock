// components/network/network.c

#include "network.h"
#include <string.h>                // for strlcpy()
#include <stdlib.h>                // for malloc/free
#include <time.h>                  // for time_t
#include <sys/time.h>              // for settimeofday
#include "esp_log.h"
#include "esp_err.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_sntp.h"              // new SNTP APIs
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "nvs.h"

static const char *TAG = "network";

// NVS namespace for time storage
#define NVS_TIME_NAMESPACE "time_store"
#define NVS_TIME_KEY "last_sync"
static EventGroupHandle_t s_wifi_event_group;
static const int WIFI_CONNECTED_BIT = BIT0;
static bool s_wifi_initialized = false;
static bool s_wifi_connected = false;

// NTP server pool for fallback reliability
static const char* NTP_SERVERS[] = {
    "pool.ntp.org",
    "time.google.com",
    "time.cloudflare.com",
    "time.nist.gov"
};
#define NTP_SERVER_COUNT (sizeof(NTP_SERVERS) / sizeof(NTP_SERVERS[0]))

/**
 * @brief Save current time to NVS as fallback
 */
static esp_err_t save_time_to_nvs(time_t current_time) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_TIME_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to open NVS for time save: %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_set_i64(handle, NVS_TIME_KEY, (int64_t)current_time);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "Saved time to NVS: %lld", (long long)current_time);
        }
    }

    nvs_close(handle);
    return err;
}

/**
 * @brief Load last known time from NVS and set system time
 */
static esp_err_t load_time_from_nvs(void) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_TIME_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "NVS time namespace not found");
        return err;
    }

    int64_t saved_time = 0;
    err = nvs_get_i64(handle, NVS_TIME_KEY, &saved_time);
    nvs_close(handle);

    if (err == ESP_OK && saved_time > 0) {
        struct timeval tv = {
            .tv_sec = (time_t)saved_time,
            .tv_usec = 0
        };
        settimeofday(&tv, NULL);

        struct tm timeinfo;
        localtime_r(&saved_time, &timeinfo);
        ESP_LOGI(TAG, "Loaded time from NVS: %04d-%02d-%02d %02d:%02d:%02d",
                 timeinfo.tm_year+1900, timeinfo.tm_mon+1, timeinfo.tm_mday,
                 timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
        ESP_LOGW(TAG, "Using fallback time - may not be current!");
        return ESP_OK;
    } else {
        ESP_LOGW(TAG, "No valid time found in NVS");
        return ESP_FAIL;
    }
}

// This handler drives both WIFI_EVENT and IP_EVENT_STA_GOT_IP
static void on_wifi_event(void* arg, esp_event_base_t ev_base,
                          int32_t ev_id, void* ev_data)
{
    if (ev_base == WIFI_EVENT) {
        if (ev_id == WIFI_EVENT_STA_START) {
            esp_wifi_connect();
        } else if (ev_id == WIFI_EVENT_STA_DISCONNECTED) {
            ESP_LOGI(TAG, "Wi‑Fi disconnected, retrying...");
            esp_wifi_connect();
        }
    } else if (ev_base == IP_EVENT && ev_id == IP_EVENT_STA_GOT_IP) {
        ESP_LOGI(TAG, "Got IP, starting SNTP");
        s_wifi_connected = true;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);

        // Start SNTP with multiple servers for reliability
        esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
        for (int i = 0; i < NTP_SERVER_COUNT; i++) {
            esp_sntp_setservername(i, NTP_SERVERS[i]);
            ESP_LOGI(TAG, "NTP server %d: %s", i, NTP_SERVERS[i]);
        }
        esp_sntp_init();
    }
}

static void on_wifi_disconnect(void* arg, esp_event_base_t ev_base,
                                int32_t ev_id, void* ev_data)
{
    if (ev_base == WIFI_EVENT && ev_id == WIFI_EVENT_STA_DISCONNECTED) {
        s_wifi_connected = false;
        xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

esp_err_t network_init_infrastructure(void)
{
    if (s_wifi_initialized) {
        ESP_LOGW(TAG, "WiFi already initialized");
        return ESP_OK;
    }

    // 1) Init TCP/IP & default event loop
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // 2) Create default STA interface
    esp_netif_create_default_wifi_sta();

    // 3) Init Wi‑Fi
    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_cfg));

    // 4) Register event handlers (needed for SNTP auto-start)
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &on_wifi_event, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &on_wifi_event, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &on_wifi_disconnect, NULL, NULL));

    // 5) Create event group for connection tracking
    s_wifi_event_group = xEventGroupCreate();

    // 6) Set storage and mode
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    s_wifi_initialized = true;
    ESP_LOGI(TAG, "Wi‑Fi infrastructure initialized (no connection)");
    return ESP_OK;
}

esp_err_t network_init(const char* ssid, const char* password)
{
    // Initialize infrastructure first if not already done
    if (!s_wifi_initialized) {
        esp_err_t err = network_init_infrastructure();
        if (err != ESP_OK) {
            return err;
        }
    }

    // Event handlers are now registered in infrastructure init

    // Configure & connect to Wi‑Fi
    wifi_config_t wcfg = {};
    strlcpy((char*)wcfg.sta.ssid,     ssid,     sizeof(wcfg.sta.ssid));
    strlcpy((char*)wcfg.sta.password, password, sizeof(wcfg.sta.password));

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wcfg));

    // Stop and restart WiFi to trigger connection with new credentials
    ESP_ERROR_CHECK(esp_wifi_stop());
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Wi‑Fi initialization complete, connecting to '%s'", ssid);
    return ESP_OK;
}

void network_wait_for_time(void)
{
    // 1) Wait for Wi‑Fi to connect (sets WIFI_CONNECTED_BIT)
    xEventGroupWaitBits(s_wifi_event_group,
                        WIFI_CONNECTED_BIT,
                        pdFALSE,
                        pdTRUE,
                        portMAX_DELAY);

    // 2) SNTP is started in on_wifi_event() handler.
    //    Wait with exponential backoff for time to become reasonable.
    time_t now = 0;
    struct tm timeinfo = { 0 };
    int retry = 0;
    const int max_retries = 10;
    int retry_delay_ms = 1000;  // Start with 1 second
    const int max_delay_ms = 30000;  // Cap at 30 seconds

    while (timeinfo.tm_year < (2020 - 1900) && retry < max_retries) {
        ESP_LOGI(TAG, "Waiting for SNTP sync… attempt %d/%d (delay: %dms)",
                 retry+1, max_retries, retry_delay_ms);
        vTaskDelay(pdMS_TO_TICKS(retry_delay_ms));
        time(&now);
        localtime_r(&now, &timeinfo);
        retry++;

        // Exponential backoff: double the delay each time, up to max
        if (retry_delay_ms < max_delay_ms) {
            retry_delay_ms = (retry_delay_ms * 2 > max_delay_ms)
                              ? max_delay_ms
                              : retry_delay_ms * 2;
        }
    }

    if (retry == max_retries) {
        ESP_LOGW(TAG, "SNTP sync failed after %d attempts", max_retries);
        ESP_LOGW(TAG, "Current time: %04d-%02d-%02d %02d:%02d:%02d",
                 timeinfo.tm_year+1900, timeinfo.tm_mon+1, timeinfo.tm_mday,
                 timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);

        // Try to load last known time from NVS
        ESP_LOGI(TAG, "Attempting to load time from NVS fallback...");
        esp_err_t nvs_err = load_time_from_nvs();
        if (nvs_err != ESP_OK) {
            ESP_LOGE(TAG, "No NVS time fallback available, time will be incorrect!");
        }
    } else {
        ESP_LOGI(TAG, "SNTP synchronized successfully: %04d-%02d-%02d %02d:%02d:%02d",
                 timeinfo.tm_year+1900, timeinfo.tm_mon+1, timeinfo.tm_mday,
                 timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);

        // Save successful sync time to NVS for future fallback
        time(&now);  // Get updated time
        save_time_to_nvs(now);
    }
}

esp_err_t network_scan(wifi_ap_info_t* ap_list, uint16_t max_aps, uint16_t* found) {
    if (ap_list == NULL || found == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_wifi_initialized) {
        ESP_LOGE(TAG, "WiFi not initialized, call network_init() first");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Starting WiFi scan...");

    // Start scan
    wifi_scan_config_t scan_config = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = false,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time.active.min = 0,
        .scan_time.active.max = 0,
    };

    esp_err_t err = esp_wifi_scan_start(&scan_config, true);  // Block until done
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "WiFi scan failed: %s", esp_err_to_name(err));
        return err;
    }

    // Get scan results
    uint16_t ap_count = 0;
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_num(&ap_count));
    ESP_LOGI(TAG, "Found %d access points", ap_count);

    if (ap_count == 0) {
        *found = 0;
        return ESP_OK;
    }

    // Allocate temporary buffer for full AP records
    wifi_ap_record_t* ap_records = malloc(sizeof(wifi_ap_record_t) * ap_count);
    if (ap_records == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for scan results");
        return ESP_ERR_NO_MEM;
    }

    uint16_t actual_count = ap_count;
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&actual_count, ap_records));

    // Copy to simplified structure
    uint16_t copy_count = (actual_count < max_aps) ? actual_count : max_aps;
    for (uint16_t i = 0; i < copy_count; i++) {
        strlcpy(ap_list[i].ssid, (char*)ap_records[i].ssid, sizeof(ap_list[i].ssid));
        ap_list[i].rssi = ap_records[i].rssi;
        ap_list[i].authmode = ap_records[i].authmode;
    }

    free(ap_records);
    *found = copy_count;

    ESP_LOGI(TAG, "WiFi scan complete, returning %d APs", copy_count);
    return ESP_OK;
}

esp_err_t network_connect(const char* ssid, const char* password) {
    if (ssid == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_wifi_initialized) {
        ESP_LOGE(TAG, "WiFi not initialized, call network_init() first");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Connecting to '%s'...", ssid);

    // Disconnect if currently connected
    if (s_wifi_connected) {
        esp_wifi_disconnect();
        vTaskDelay(pdMS_TO_TICKS(500));  // Give time to disconnect
    }

    // Configure new credentials
    wifi_config_t wcfg = {};
    strlcpy((char*)wcfg.sta.ssid, ssid, sizeof(wcfg.sta.ssid));
    if (password != NULL) {
        strlcpy((char*)wcfg.sta.password, password, sizeof(wcfg.sta.password));
    }

    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &wcfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set WiFi config: %s", esp_err_to_name(err));
        return err;
    }

    // Connect
    err = esp_wifi_connect();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to connect: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Connection initiated");
    return ESP_OK;
}

bool network_is_connected(void) {
    return s_wifi_connected;
}

esp_err_t network_get_ssid(char* ssid) {
    if (ssid == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_wifi_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    wifi_config_t wcfg;
    esp_err_t err = esp_wifi_get_config(WIFI_IF_STA, &wcfg);
    if (err != ESP_OK) {
        return err;
    }

    strlcpy(ssid, (char*)wcfg.sta.ssid, 33);
    return ESP_OK;
}