idf_component_register(
    SRCS "log_async.c"
    INCLUDE_DIRS "include"
    REQUIRES log freertos esp_ringbuf
    PRIV_REQUIRES heap
)
