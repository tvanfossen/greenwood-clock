// components/network/network.h
#include "esp_err.h"
#include "esp_wifi.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** WiFi scan result structure */
typedef struct {
    char ssid[33];
    int8_t rssi;
    wifi_auth_mode_t authmode;
} wifi_ap_info_t;

/** Initialize Wi‑Fi infrastructure only (for scanning without connecting) */
esp_err_t network_init_infrastructure(void);

/** Initialize Wi‑Fi STA + SNTP. */
esp_err_t network_init(const char* ssid, const char* pass);

/** Block until SNTP has set the RTC. */
void network_wait_for_time(void);

/**
 * @brief Scan for WiFi networks
 * @param ap_list Array to store scan results
 * @param max_aps Maximum number of APs to return
 * @param found Number of APs found (output)
 * @return ESP_OK on success
 */
esp_err_t network_scan(wifi_ap_info_t* ap_list, uint16_t max_aps, uint16_t* found);

/**
 * @brief Connect to a WiFi network
 * @param ssid SSID to connect to
 * @param password Password (can be NULL for open networks)
 * @return ESP_OK on success
 */
esp_err_t network_connect(const char* ssid, const char* password);

/**
 * @brief Check if WiFi is connected
 * @return true if connected, false otherwise
 */
bool network_is_connected(void);

/**
 * @brief Get current SSID
 * @param ssid Buffer to store SSID (must be at least 33 bytes)
 * @return ESP_OK on success
 */
esp_err_t network_get_ssid(char* ssid);

#ifdef __cplusplus
}
#endif
