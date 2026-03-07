idf_component_register(
    SRCS "http_api.c"
    INCLUDE_DIRS "."
    REQUIRES
        esp_http_server
        sdcard
        esp_timer
        debug_log
        udp_log
        ui
        ota
        secrets
        freertos
        display_fsm
        nws
        settings
        cjson
)
