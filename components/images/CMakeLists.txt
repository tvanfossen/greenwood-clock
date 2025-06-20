# Glob all generated C sources under c_array/
file(GLOB IMAGE_SRCS "c_array/*.c")

idf_component_register(
    SRCS         ${IMAGE_SRCS}
    INCLUDE_DIRS .
    REQUIRES     lvgl
)