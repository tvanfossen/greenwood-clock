# components/network/CMakeLists.txt

idf_component_register(
    SRCS        "network.c"
    INCLUDE_DIRS "."
    REQUIRES
        esp_netif        # TCP/IP stack
        esp_event        # event loop
        lwip             # SNTP & IP_EVENT support
        esp_wifi         # Wi‑Fi API (stubbed by esp_wifi_remote)
        esp_wifi_remote  # esp_wifi_* over RPC via esp-hosted
        esp_hosted       # C6 host driver
        nvs_flash        # NVS for Wi‑Fi storage
        freertos         # EventGroup, vTaskDelay, etc.
)