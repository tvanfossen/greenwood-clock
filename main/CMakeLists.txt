# main/CMakeLists.txt

idf_component_register(
    SRCS        "main.cpp"
    INCLUDE_DIRS "."
    REQUIRES
        esp32_p4_function_ev_board  # board support (LCD, DSI…)
        esp_hosted                  # C6 host driver
        esp_wifi_remote             # esp_wifi over RPC
        esp_lcd_ek79007             # panel driver

        spiffs                      # for bsp_spiffs_init_default()

        network                     # your network module
        time_sync                   # your SNTP/TZ module
        ui                          # your LVGL splash & clock UI
        weather
        settings                    # persistent settings via NVS
        ota                         # OTA firmware updates
        sdcard                      # SD card support
        http_api                    # HTTP server for file management
        debug_log                   # Debug log capture to SD card
)
spiffs_create_partition_image(storage ../spiffs FLASH_IN_PROJECT)