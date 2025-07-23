# Glob all generated C sources under c_array/
file(GLOB LOTTIE_SRCS "c_array/*.c")

idf_component_register(
    SRCS         ${LOTTIE_SRCS}
    INCLUDE_DIRS .
    REQUIRES     lvgl
)