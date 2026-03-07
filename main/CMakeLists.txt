# main/CMakeLists.txt

idf_component_register(
    SRCS        "main.cpp"
    INCLUDE_DIRS "."
    REQUIRES
        driver                      # gpio_set_direction / gpio_set_level for early RST assert
        esp32_p4_function_ev_board  # board support (LCD, DSI…)
        esp_hosted                  # C6 host driver
        esp_wifi_remote             # esp_wifi over RPC
        esp_lcd_ek79007             # panel driver

        spiffs                      # for bsp_spiffs_init_default()
        lvgl_mem_esp                # LVGL SPIRAM memory allocator

        network                     # your network module
        time_sync                   # your SNTP/TZ module
        ui                          # your LVGL splash & clock UI
        settings                    # persistent settings via NVS
        ota                         # OTA firmware updates
        sdcard                      # SD card support
        http_api                    # HTTP server for file management
        debug_log                   # Debug log capture to SD card
        display_fsm                 # Display state machine
        nws                         # NWS weather service
        mdns                        # mDNS service advertisement
)
spiffs_create_partition_image(storage ../spiffs FLASH_IN_PROJECT)

# Intercept esp_panic_handler globally so we can log crash info to SD
# before the real handler prints the backtrace to serial.
idf_build_set_property(LINK_OPTIONS "-Wl,--wrap=esp_panic_handler" APPEND)