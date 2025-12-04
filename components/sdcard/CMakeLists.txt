idf_component_register(
    SRCS "sdcard.c"
    INCLUDE_DIRS "."
    REQUIRES fatfs sdmmc vfs esp32_p4_function_ev_board
)
