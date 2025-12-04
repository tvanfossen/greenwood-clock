# components/ota/CMakeLists.txt

idf_component_register(
    SRCS        "ota.c"
    INCLUDE_DIRS "."
    REQUIRES
        esp_http_client
        esp_https_ota
        app_update
        esp_partition
)
