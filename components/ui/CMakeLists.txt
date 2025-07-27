# components/ui/CMakeLists.txt

idf_component_register(
    SRCS        "ui.c"
    INCLUDE_DIRS "."
    REQUIRES
        lvgl
        esp32_p4_function_ev_board
        esp_lvgl_port
        time_sync
        images
        fonts
        lottie
        weather
        astronomy
        secrets
)