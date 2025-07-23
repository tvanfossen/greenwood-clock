# components/time_sync/CMakeLists.txt

idf_component_register(
    SRCS        "time_sync.c"
    INCLUDE_DIRS "."
    # No extra IDF dependencies needed
)