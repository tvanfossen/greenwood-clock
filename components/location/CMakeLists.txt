# components/location/CMakeLists.txt — the clock's place and timezone (REQ-LOC-001)

idf_component_register(
    SRCS
        "src/location.c"
        "src/location_census.c"
        "src/tz.c"
        "src/tz_table.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        settings
        cjson
    PRIV_REQUIRES
        time_sync
        http_client_utils
        esp_http_client
        mbedtls
        nvs_flash
)
