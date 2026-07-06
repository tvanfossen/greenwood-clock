// components/network/network.c

#include "network.h"
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>
#include "esp_log.h"
#include "esp_err.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_sntp.h"
#include "esp_wifi.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "nvs.h"

static const char *TAG = "network";

#define NVS_TIME_NAMESPACE  "time_store"
#define NVS_TIME_KEY        "last_sync"

static EventGroupHandle_t s_wifi_event_group;
static const int WIFI_CONNECTED_BIT = BIT0;
static const int SCAN_DONE_BIT      = BIT1;   // set by WIFI_EVENT_SCAN_DONE
static bool s_wifi_initialized = false;
static bool s_wifi_connected    = false;
// Set while network_scan() runs so the disconnect handler doesn't auto-reconnect
// and fight the scan (esp_wifi_scan_start fails ESP_ERR_WIFI_STATE mid-connect).
static volatile bool s_scanning = false;

// Reconnect backoff. The disconnect handler must NOT call esp_wifi_connect() in a
// tight loop — with wrong/absent creds that hammers the C6 and spams logs. Instead
// schedule the retry on a one-shot timer with exponential backoff (reset on success).
#define RECONNECT_BACKOFF_MIN_MS   1000
#define RECONNECT_BACKOFF_MAX_MS   30000
static esp_timer_handle_t s_reconnect_timer   = NULL;
static uint32_t           s_reconnect_backoff_ms = RECONNECT_BACKOFF_MIN_MS;

static const char* NTP_SERVERS[] = {
    "pool.ntp.org",
    "time.google.com",
    "time.cloudflare.com",
    "time.nist.gov"
};
#define NTP_SERVER_COUNT (sizeof(NTP_SERVERS) / sizeof(NTP_SERVERS[0]))

// =============================================================================
// Private: NVS time persistence
// =============================================================================

/**
 * @brief Save a Unix timestamp to NVS for use as a time fallback after reboot.
 * @param current_time  Timestamp to store.
 * @return ESP_OK on success.
 */
static esp_err_t save_time_to_nvs(time_t current_time) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_TIME_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to open NVS for time save: %d", (int)err);
        return err;
    }
    err = nvs_set_i64(handle, NVS_TIME_KEY, (int64_t)current_time);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
        if (err == ESP_OK) ESP_LOGI(TAG, "Saved time to NVS: %lld", (long long)current_time);
    }
    nvs_close(handle);
    return err;
}

/**
 * @brief Load the last saved timestamp from NVS and apply it as the system clock.
 * @return ESP_OK if a valid timestamp was found and applied.
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
    if (err != ESP_OK || saved_time <= 0) {
        ESP_LOGW(TAG, "No valid time found in NVS");
        return ESP_FAIL;
    }
    struct timeval tv = { .tv_sec = (time_t)saved_time, .tv_usec = 0 };
    settimeofday(&tv, NULL);
    struct tm timeinfo;
    localtime_r(&saved_time, &timeinfo);
    ESP_LOGI(TAG, "Loaded time from NVS: %04d-%02d-%02d %02d:%02d:%02d",
             timeinfo.tm_year+1900, timeinfo.tm_mon+1, timeinfo.tm_mday,
             timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    ESP_LOGW(TAG, "Using fallback time — may not be current!");
    return ESP_OK;
}

// =============================================================================
// Private: Wi-Fi event helpers
// =============================================================================

/**
 * @brief Configure and start the SNTP client with all pool servers.
 *
 * Fires on every IP_EVENT_STA_GOT_IP — including Wi-Fi reconnects. The lwip
 * SNTP client asserts in sntp_setoperatingmode() if the operating mode is set
 * while the client is already running, so re-init on reconnect would PANIC.
 * Guard against that: if SNTP is already enabled, just force a resync.
 */
static void network_start_sntp(void) {
    if (esp_sntp_enabled()) {
        ESP_LOGI(TAG, "SNTP already running — restarting sync (reconnect)");
        esp_sntp_restart();
        return;
    }
    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    for (int i = 0; i < (int)NTP_SERVER_COUNT; i++) {
        esp_sntp_setservername(i, NTP_SERVERS[i]);
        ESP_LOGI(TAG, "NTP server %d: %s", i, NTP_SERVERS[i]);
    }
    esp_sntp_init();
}

/**
 * @brief Handle IP_EVENT_STA_GOT_IP — update state and start SNTP.
 */
static void network_handle_got_ip(void) {
    ESP_LOGI(TAG, "Got IP, starting SNTP");
    s_wifi_connected = true;
    xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    network_start_sntp();
}

/** @brief One-shot timer callback: attempt a (backed-off) reconnect. */
static void reconnect_timer_cb(void* arg) {
    (void)arg;
    if (s_scanning) return;   // a scan started meanwhile — it will resume the connect
    esp_wifi_connect();
}

/**
 * @brief Schedule a reconnect after the current backoff, then grow the backoff.
 *
 * Never calls esp_wifi_connect() synchronously from the event task in a loop —
 * that is what caused the too-frequent retries with wrong credentials.
 */
static void schedule_reconnect(void) {
    if (!s_reconnect_timer) { esp_wifi_connect(); return; }   // fallback: timer not ready
    esp_timer_stop(s_reconnect_timer);                        // no-op if not running
    esp_timer_start_once(s_reconnect_timer, (uint64_t)s_reconnect_backoff_ms * 1000);
    ESP_LOGI(TAG, "Wi-Fi reconnect in %lu ms", (unsigned long)s_reconnect_backoff_ms);
    uint32_t next = s_reconnect_backoff_ms * 2;
    s_reconnect_backoff_ms = (next > RECONNECT_BACKOFF_MAX_MS) ? RECONNECT_BACKOFF_MAX_MS : next;
}

/**
 * @brief Unified event handler for WIFI_EVENT and IP_EVENT_STA_GOT_IP.
 *
 * - WIFI_EVENT_STA_START → connect immediately (first attempt)
 * - WIFI_EVENT_STA_DISCONNECTED → reconnect with exponential backoff
 * - IP_EVENT_STA_GOT_IP → reset backoff, update state + start SNTP
 */
static void on_wifi_event(void* arg, esp_event_base_t ev_base,
                          int32_t ev_id, void* ev_data)
{
    if (ev_base == WIFI_EVENT) {
        if (ev_id == WIFI_EVENT_SCAN_DONE) {
            const wifi_event_sta_scan_done_t* d = (const wifi_event_sta_scan_done_t*)ev_data;
            if (d)
                ESP_LOGI(TAG, "SCAN_DONE: status=%u number=%u id=%u",
                         (unsigned)d->status, (unsigned)d->number, (unsigned)d->scan_id);
            xEventGroupSetBits(s_wifi_event_group, SCAN_DONE_BIT);
        } else if (ev_id == WIFI_EVENT_STA_START) {
            esp_wifi_connect();                       // initial attempt, immediate
        } else if (ev_id == WIFI_EVENT_STA_DISCONNECTED) {
            ESP_LOGI(TAG, "Wi-Fi disconnected%s", s_scanning ? " (scan in progress)" : "");
            // Suppress auto-reconnect during a scan — network_scan() aborts the
            // connect on purpose and resumes it when done. Otherwise back off.
            if (!s_scanning) schedule_reconnect();
        }
    } else if (ev_base == IP_EVENT && ev_id == IP_EVENT_STA_GOT_IP) {
        s_reconnect_backoff_ms = RECONNECT_BACKOFF_MIN_MS;   // connected — reset backoff
        network_handle_got_ip();
    }
}

/** @brief Track disconnect in the event-group and clear the connected flag. */
static void on_wifi_disconnect(void* arg, esp_event_base_t ev_base,
                                int32_t ev_id, void* ev_data)
{
    if (ev_base == WIFI_EVENT && ev_id == WIFI_EVENT_STA_DISCONNECTED) {
        s_wifi_connected = false;
        xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

// =============================================================================
// Private: network_init_infrastructure helpers
// =============================================================================

/**
 * @brief Initialise the TCP/IP stack and default event loop.
 */
static void network_init_tcp_stack(void) {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
}

/**
 * @brief Initialise the Wi-Fi driver with default configuration.
 */
static void network_init_wifi_driver(void) {
    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_cfg));
}

/**
 * @brief Register all event handler instances required for STA operation.
 */
static void network_register_event_handlers(void) {
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &on_wifi_event, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &on_wifi_event, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &on_wifi_disconnect, NULL, NULL));
}

/**
 * @brief Create the event group, configure storage/mode, and start Wi-Fi.
 */
static void network_configure_and_start(void) {
    s_wifi_event_group = xEventGroupCreate();
    const esp_timer_create_args_t rt = {
        .callback = reconnect_timer_cb,
        .name     = "wifi_reconnect",
    };
    ESP_ERROR_CHECK(esp_timer_create(&rt, &s_reconnect_timer));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
}

// =============================================================================
// Public: infrastructure init
// =============================================================================

esp_err_t network_init_infrastructure(void) {
    if (s_wifi_initialized) {
        ESP_LOGW(TAG, "WiFi already initialized");
        return ESP_OK;
    }
    network_init_tcp_stack();
    network_init_wifi_driver();
    network_register_event_handlers();
    network_configure_and_start();
    s_wifi_initialized = true;
    ESP_LOGI(TAG, "Wi-Fi infrastructure initialized (no connection)");
    return ESP_OK;
}

// =============================================================================
// Private: network_init helpers
// =============================================================================

/**
 * @brief Write SSID/password into a wifi_config_t and apply it to the STA interface.
 * @param ssid      Network SSID.
 * @param password  Network password; may be NULL for open networks.
 */
static void network_set_credentials(const char* ssid, const char* password) {
    wifi_config_t wcfg = {};
    strlcpy((char*)wcfg.sta.ssid,     ssid,     sizeof(wcfg.sta.ssid));
    strlcpy((char*)wcfg.sta.password, password, sizeof(wcfg.sta.password));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wcfg));
}

// =============================================================================
// Public: network_init
// =============================================================================

esp_err_t network_init(const char* ssid, const char* password) {
    if (!s_wifi_initialized) {
        esp_err_t err = network_init_infrastructure();
        if (err != ESP_OK) return err;
    }
    network_set_credentials(ssid, password);
    ESP_ERROR_CHECK(esp_wifi_stop());
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "Wi-Fi init complete, connecting to '%s'", ssid);
    return ESP_OK;
}

// =============================================================================
// Private: network_wait_for_time helpers
// =============================================================================

/**
 * @brief Poll the system clock until it is after 2020, with exponential back-off.
 *
 * SNTP is started by the IP-event handler when the device gets an IP address.
 * This function waits for the SNTP sync to propagate into the RTC.
 *
 * @param max_retries  Maximum poll attempts.
 * @return true if the clock synchronised within max_retries attempts.
 */
static bool sntp_poll_until_synced(int max_retries) {
    int delay_ms = 1000;
    for (int i = 0; i < max_retries; i++) {
        ESP_LOGI(TAG, "SNTP attempt %d/%d (delay %d ms)", i + 1, max_retries, delay_ms);
        vTaskDelay(pdMS_TO_TICKS(delay_ms));
        time_t now = 0;
        time(&now);
        struct tm timeinfo = {0};
        localtime_r(&now, &timeinfo);
        if (timeinfo.tm_year >= (2020 - 1900)) return true;
        delay_ms = (delay_ms * 2 > 30000) ? 30000 : delay_ms * 2;
    }
    return false;
}

// =============================================================================
// Public: network_wait_for_time
// =============================================================================

// Bound the boot-time wait for Wi-Fi so a clock with NO network (AP down, moved,
// bad credentials) still boots to the display instead of hanging here forever.
// If Wi-Fi connects later, the IP-event handler starts SNTP and the RTC corrects
// itself in the background.
#define WIFI_CONNECT_BOOT_WAIT_MS   20000

void network_wait_for_time(void) {
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT,
                        pdFALSE, pdTRUE, pdMS_TO_TICKS(WIFI_CONNECT_BOOT_WAIT_MS));
    if (!(bits & WIFI_CONNECTED_BIT)) {
        ESP_LOGW(TAG, "Wi-Fi not connected within %d ms — booting without SNTP; "
                 "RTC will sync in the background if Wi-Fi comes up",
                 WIFI_CONNECT_BOOT_WAIT_MS);
        load_time_from_nvs();   // best-effort last-known time; incorrect is better than hung
        return;
    }
    bool synced = sntp_poll_until_synced(10);
    if (synced) {
        time_t now = 0;
        time(&now);
        save_time_to_nvs(now);
        ESP_LOGI(TAG, "SNTP synchronised successfully");
    } else {
        ESP_LOGW(TAG, "SNTP sync failed, attempting NVS fallback...");
        esp_err_t err = load_time_from_nvs();
        if (err != ESP_OK) ESP_LOGE(TAG, "No NVS time fallback — time will be incorrect!");
    }
}

// =============================================================================
// Private: network_scan helpers
// =============================================================================

/**
 * @brief Validate common preconditions for scan and connect operations.
 *
 * @param ptr  Required non-NULL pointer (e.g. output buffer or SSID string).
 * @return ESP_OK if all preconditions pass.
 */
static esp_err_t network_check_ready(const void* ptr) {
    if (!ptr) return ESP_ERR_INVALID_ARG;
    if (!s_wifi_initialized) {
        ESP_LOGE(TAG, "WiFi not initialized, call network_init() first");
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}

/**
 * @brief Allocate a buffer and retrieve raw scan records from the driver.
 *
 * Caller must free the returned pointer.
 *
 * @param ap_count   Number of APs reported by the driver.
 * @param actual_out Set to the number of records actually returned.
 * @return Heap buffer on success, NULL on OOM.
 */
static wifi_ap_record_t* network_fetch_scan_records(uint16_t ap_count, uint16_t* actual_out)
{
    wifi_ap_record_t* records = malloc(sizeof(wifi_ap_record_t) * ap_count);
    if (!records) {
        ESP_LOGE(TAG, "OOM for scan result buffer (%u APs)", ap_count);
        return NULL;
    }
    *actual_out = ap_count;
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(actual_out, records));
    return records;
}

/**
 * @brief Copy up to n raw AP records into the caller's simplified list.
 *
 * @param dst  Destination array.
 * @param src  Source raw records.
 * @param n    Number of entries to copy.
 */
static void network_copy_ap_records(wifi_ap_info_t* dst, const wifi_ap_record_t* src,
                                     uint16_t n)
{
    for (uint16_t i = 0; i < n; i++) {
        strlcpy(dst[i].ssid, (char*)src[i].ssid, sizeof(dst[i].ssid));
        dst[i].rssi     = src[i].rssi;
        dst[i].authmode = src[i].authmode;
    }
}

/**
 * @brief Read scan results from the driver into the caller's simplified list.
 *
 * @param ap_list   Destination buffer.
 * @param max_aps   Maximum entries to copy.
 * @param found     Set to the number of entries copied.
 * @return ESP_OK, ESP_ERR_NO_MEM on OOM, or ESP_OK with *found=0 if empty.
 */
static esp_err_t network_scan_results(wifi_ap_info_t* ap_list, uint16_t max_aps,
                                       uint16_t* found)
{
    uint16_t ap_count = 0;
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_num(&ap_count));
    *found = 0;
    if (ap_count == 0) return ESP_OK;
    uint16_t actual = 0;
    wifi_ap_record_t* records = network_fetch_scan_records(ap_count, &actual);
    if (!records) return ESP_ERR_NO_MEM;
    uint16_t n = (actual < max_aps) ? actual : max_aps;
    network_copy_ap_records(ap_list, records, n);
    free(records);
    *found = n;
    ESP_LOGI(TAG, "Scan: %d APs found, %d returned", (int)ap_count, (int)n);
    return ESP_OK;
}

// =============================================================================
// Public: network_scan
// =============================================================================

// Run a NON-blocking scan and wait for WIFI_EVENT_SCAN_DONE. On esp_hosted the
// blocking form (esp_wifi_scan_start(cfg, true)) often returns before results are
// ready, so esp_wifi_scan_get_ap_num() reads 0 — this waits for the event instead.
static esp_err_t network_run_scan(const wifi_scan_config_t* cfg) {
    xEventGroupClearBits(s_wifi_event_group, SCAN_DONE_BIT);
    esp_err_t err = esp_wifi_scan_start(cfg, false);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "WiFi scan start failed: %d", (int)err);
        return err;
    }
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group, SCAN_DONE_BIT,
                                           pdTRUE, pdFALSE, pdMS_TO_TICKS(10000));
    if (!(bits & SCAN_DONE_BIT)) {
        ESP_LOGW(TAG, "WiFi scan timed out waiting for SCAN_DONE");
        esp_wifi_scan_stop();
        return ESP_ERR_TIMEOUT;
    }
    uint16_t n = 0;
    esp_wifi_scan_get_ap_num(&n);
    ESP_LOGI(TAG, "post-scan esp_wifi_scan_get_ap_num=%u", n);
    return ESP_OK;
}

// Give the scan exclusive use of the radio: suppress our reconnect, abort the
// in-progress connect, and clear the STA config so nothing keeps chasing the stored
// AP during the scan. Saves the prior config for scan_resume_connect() to restore.
static void scan_preempt_connect(wifi_config_t* saved, bool* had_cfg) {
    s_scanning = true;
    *had_cfg = (esp_wifi_get_config(WIFI_IF_STA, saved) == ESP_OK);
    esp_wifi_disconnect();
    wifi_config_t empty = {0};
    esp_wifi_set_config(WIFI_IF_STA, &empty);
    vTaskDelay(pdMS_TO_TICKS(200));   // let the connect fully stand down
}

/** @brief Restore the STA config saved by scan_preempt_connect() and reconnect. */
static void scan_resume_connect(const wifi_config_t* saved, bool had_cfg) {
    if (had_cfg) esp_wifi_set_config(WIFI_IF_STA, saved);
    s_scanning = false;
    esp_wifi_connect();
}

esp_err_t network_scan(wifi_ap_info_t* ap_list, uint16_t max_aps, uint16_t* found) {
    esp_err_t err = network_check_ready(ap_list);
    if (err != ESP_OK || !found) return err ? err : ESP_ERR_INVALID_ARG;
    ESP_LOGI(TAG, "Starting WiFi scan...");
    const wifi_scan_config_t scan_config = {
        .ssid        = NULL,
        .bssid       = NULL,
        .channel     = 0,
        .show_hidden = false,
        .scan_type   = WIFI_SCAN_TYPE_ACTIVE,
    };
    // esp_wifi_scan_start() returns ESP_ERR_WIFI_STATE (12294) while the STA is
    // mid-connect. When we're not connected the STA is in the auto-reconnect loop,
    // so abort it (with reconnect suppressed) and give it a moment to settle before
    // scanning, then resume. A connected STA can scan directly.
    // Make the scan take precedence over the (retrying) connect. Suppressing our
    // reconnect handler isn't enough: while an SSID is configured the connect
    // machinery keeps chasing that AP and starves/overwrites the scan. So when we're
    // not already connected, snapshot the STA config, clear it (no target → nothing
    // reconnects and the radio is free), scan, read results, then restore + resume.
    // A live connection is left alone (scanning while connected already works).
    bool suppressed = !s_wifi_connected;
    wifi_config_t saved = {0};
    bool had_cfg = false;
    if (suppressed) scan_preempt_connect(&saved, &had_cfg);

    err = network_run_scan(&scan_config);
    if (err == ESP_OK)
        err = network_scan_results(ap_list, max_aps, found);   // read before restoring

    if (suppressed) scan_resume_connect(&saved, had_cfg);
    return err;
}

// =============================================================================
// Private: network_connect helpers
// =============================================================================

/**
 * @brief Apply SSID/password credentials and initiate a Wi-Fi connection.
 *
 * @param ssid      Network SSID.
 * @param password  Network password; NULL for open networks.
 * @return ESP_OK on successful initiation.
 */
static esp_err_t network_set_and_connect(const char* ssid, const char* password) {
    wifi_config_t wcfg = {};
    strlcpy((char*)wcfg.sta.ssid, ssid, sizeof(wcfg.sta.ssid));
    if (password) strlcpy((char*)wcfg.sta.password, password, sizeof(wcfg.sta.password));
    esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, &wcfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set WiFi config: %d", (int)err);
        return err;
    }
    err = esp_wifi_connect();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to connect: %d", (int)err);
        return err;
    }
    ESP_LOGI(TAG, "Connection to '%s' initiated", ssid);
    return ESP_OK;
}

// =============================================================================
// Public: network_connect
// =============================================================================

esp_err_t network_connect(const char* ssid, const char* password) {
    esp_err_t err = network_check_ready(ssid);
    if (err != ESP_OK) return err;
    ESP_LOGI(TAG, "Connecting to '%s'...", ssid);
    if (s_reconnect_timer) esp_timer_stop(s_reconnect_timer);   // cancel any pending retry
    s_reconnect_backoff_ms = RECONNECT_BACKOFF_MIN_MS;          // user action → try promptly
    if (s_wifi_connected) {
        esp_wifi_disconnect();
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    return network_set_and_connect(ssid, password);
}

// =============================================================================
// Public: status / info
// =============================================================================

bool network_is_connected(void) {
    return s_wifi_connected;
}

esp_err_t network_get_ssid(char* ssid) {
    esp_err_t err = network_check_ready(ssid);
    if (err != ESP_OK) return err;
    wifi_config_t wcfg;
    err = esp_wifi_get_config(WIFI_IF_STA, &wcfg);
    if (err == ESP_OK) strlcpy(ssid, (char*)wcfg.sta.ssid, 33);
    return err;
}
