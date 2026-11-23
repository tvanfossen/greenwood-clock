/**
 * @file settings_ui.h
 * @version 1
 * @brief Settings screens' shared internals: the sub-screen ids and navigation stack, the
 *        common sub-screen frame and the text colour presets. Used by state_settings.cpp and
 *        the screens split out of it (settings_schedule.cpp). LVGL lock held throughout.
 */
#ifndef SETTINGS_UI_H
#define SETTINGS_UI_H

#include <stdint.h>
#include "lvgl.h"

enum settings_sub_screen_t {
    SUB_MENU = 0,
    SUB_WIFI,
    SUB_BRIGHTNESS,
    SUB_BACKGROUND,
    SUB_TEXT_COLOR,
    SUB_ANIMATION,
    SUB_CLOCK_ANIM,
    SUB_ABOUT,
    SUB_SCHEDULE,
    SUB_SCHED_PROFILES,
    SUB_SCHED_PROFILE,
    SUB_MAX
};

struct color_preset_t {
    uint32_t    color;
    const char *name;
};

extern const color_preset_t COLOR_PRESETS[];
extern const int            NUM_COLOR_PRESETS;

/**
 * @brief Hide the current sub-screen, create the requested one on demand, show it and push it.
 * @version 1
 * @req REQ-UI-001
 */
void show_sub(settings_sub_screen_t sub);

/**
 * @brief Destroy the top sub-screen and re-show the previous one (at the menu: leave settings).
 * @version 1
 * @req REQ-UI-001
 */
void pop_sub(void);

/**
 * @brief The common sub-screen frame: a dark rounded panel with a title and a Back button.
 * @version 1
 * @req REQ-UI-001
 * @return The new container.
 */
lv_obj_t *make_sub_container(lv_obj_t *parent, const char *title);

/** Schedule editor screens (settings_schedule.cpp). */
lv_obj_t *create_sub_schedule(lv_obj_t *parent);
lv_obj_t *create_sub_sched_profiles(lv_obj_t *parent);
lv_obj_t *create_sub_sched_profile(lv_obj_t *parent);

#endif /* SETTINGS_UI_H */
