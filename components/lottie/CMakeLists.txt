# Lottie animations are now loaded from SPIFFS at runtime
# No C array sources needed - animations load via lv_lottie_set_src_file()

idf_component_register(
    INCLUDE_DIRS .
    REQUIRES     lvgl
)