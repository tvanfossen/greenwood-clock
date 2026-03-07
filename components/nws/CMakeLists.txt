# components/nws/CMakeLists.txt

idf_component_register(
    SRCS
        "src/nws_location.c"
        "src/nws_conditions.c"
        "src/nws_forecast.c"
        "src/nws_alerts.c"
        "src/nws_radar.c"
        "src/nws_condition_map.c"
        "src/nws_task.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        freertos
        settings
        esp_http_client
        http_client_utils
        cjson
        display_events
    PRIV_REQUIRES
        esp-tls
        mbedtls
)
