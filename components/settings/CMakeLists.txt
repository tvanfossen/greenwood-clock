# components/settings/CMakeLists.txt

idf_component_register(
    SRCS        "settings.c" "clock_anim_pref.c"
    INCLUDE_DIRS "."
    REQUIRES
        nvs_flash
)
