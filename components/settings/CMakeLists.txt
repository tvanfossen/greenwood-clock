# components/settings/CMakeLists.txt

idf_component_register(
    SRCS        "settings.c" "clock_anim_pref.c" "bg_schedule_model.c" "bg_schedule.c"
    INCLUDE_DIRS "."
    REQUIRES
        nvs_flash
)
