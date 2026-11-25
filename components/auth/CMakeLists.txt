idf_component_register(
    SRCS "auth.c" "auth_core.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES secrets esp_timer esp_hw_support
)
