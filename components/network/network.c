// components/network/network.c

#include "network.h"
#include <string.h>                // for strlcpy()
#include "esp_log.h"
#include "esp_err.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_sntp.h"              // new SNTP APIs
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

static const char *TAG = "network";
static EventGroupHandle_t s_wifi_event_group;
static const int WIFI_CONNECTED_BIT = BIT0;

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
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);

        // Start SNTP (using the new APIs)
        esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
        esp_sntp_setservername(0, "pool.ntp.org");
        esp_sntp_init();
    }
}

esp_err_t network_init(const char* ssid, const char* password)
{
    // 1) Init TCP/IP & default event loop
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // 2) Create default STA interface
    esp_netif_create_default_wifi_sta();

    // 3) Init Wi‑Fi
    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_cfg));

    // 4) Register our event handler for both Wi‑Fi and IP events
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &on_wifi_event, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &on_wifi_event, NULL, NULL));

    // 5) Create an event group to signal when we’re connected
    s_wifi_event_group = xEventGroupCreate();

    // 6) Configure & start Wi‑Fi
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

    wifi_config_t wcfg = {};
    strlcpy((char*)wcfg.sta.ssid,     ssid,     sizeof(wcfg.sta.ssid));
    strlcpy((char*)wcfg.sta.password, password, sizeof(wcfg.sta.password));

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wcfg));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "Wi‑Fi initialization complete");
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

    // 2) SNTP is started in your on_wifi_event() handler.
    //    Now wait up to ~20 seconds for the time to become “reasonable.”
    time_t now = 0;
    struct tm timeinfo = { 0 };
    int retry = 0;
    const int max_retries = 10;
    const int retry_delay_ms = 2000;

    while (timeinfo.tm_year < (2020 - 1900) && retry < max_retries) {
        ESP_LOGI(TAG, "Waiting for SNTP sync… retry %d/%d", retry+1, max_retries);
        vTaskDelay(pdMS_TO_TICKS(retry_delay_ms));
        time(&now);
        localtime_r(&now, &timeinfo);
        retry++;
    }

    if (retry == max_retries) {
        ESP_LOGW(TAG, "SNTP still not synced—proceeding with time %04d-%02d-%02d %02d:%02d:%02d",
                 timeinfo.tm_year+1900, timeinfo.tm_mon+1, timeinfo.tm_mday,
                 timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    } else {
        ESP_LOGI(TAG, "SNTP synchronized: %04d-%02d-%02d %02d:%02d:%02d",
                 timeinfo.tm_year+1900, timeinfo.tm_mon+1, timeinfo.tm_mday,
                 timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    }
}