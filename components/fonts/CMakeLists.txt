# Only compile fonts that are actually used in the code
# ui.c uses: nunito_48, nunito_256
# clock_widget.c uses: nunito_48, nunito_128, nunito_256
set(FONT_SRCS
    "c_array/nunito_48.c"
    "c_array/nunito_128.c"
    "c_array/nunito_256.c"
)

idf_component_register(
    SRCS         ${FONT_SRCS}
    INCLUDE_DIRS .
    REQUIRES     lvgl
)