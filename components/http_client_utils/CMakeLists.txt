# components/http_client_utils/CMakeLists.txt
idf_component_register(
    SRCS        "http_client_utils.c"
    INCLUDE_DIRS "."
    REQUIRES    esp_http_client
)
