# components/weather/CMakeLists.txt
idf_component_register(
    SRCS "weather.c"
    INCLUDE_DIRS "."
    REQUIRES esp_http_client cjson mbedtls
)