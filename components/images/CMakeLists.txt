# Images loaded from SD card at runtime to save firmware space
# Splash is loaded from /sdcard/splash.png via HTTP API upload
# No C array sources needed

idf_component_register(
    INCLUDE_DIRS .
    REQUIRES     lvgl
)