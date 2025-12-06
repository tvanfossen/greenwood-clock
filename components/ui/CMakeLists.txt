# components/ui/CMakeLists.txt

idf_component_register(
    SRCS        "ui.c" "screen_manager.c"
    INCLUDE_DIRS "."
    REQUIRES
        lvgl
        lvgl_mem_esp
        esp32_p4_function_ev_board
        esp_lvgl_port
        time_sync
        images
        fonts
        lottie
        weather
        astronomy
        secrets
        esp_netif
        network
        settings
        ota
        sdcard
)