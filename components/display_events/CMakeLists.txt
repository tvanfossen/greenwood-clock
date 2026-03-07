# components/display_events/CMakeLists.txt
#
# Header-only component: display event types shared between display_fsm, nws,
# http_api, and any other component that sends display events.
# Extracted to break circular dependency between nws and display_fsm.

idf_component_register(
    INCLUDE_DIRS
        "include"
    REQUIRES
        lvgl
)
