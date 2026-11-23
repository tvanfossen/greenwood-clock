idf_component_register(
    SRCS "http_api.c" "http_bg_schedule.c" "http_frame_capture.c"
    INCLUDE_DIRS "."
    REQUIRES
        esp_http_server
        sdcard
        esp_timer
        debug_log
        udp_log
        ota
        secrets
        freertos
        display_fsm
        nws
        settings
        cjson
        espcoredump
        esp_partition
        lvgl
        esp_lvgl_port
)
