# Glob all generated C sources under c_array/
file(GLOB FONT_SRCS "c_array/*.c")

idf_component_register(
    SRCS         ${FONT_SRCS}
    INCLUDE_DIRS .
    REQUIRES     lvgl
)