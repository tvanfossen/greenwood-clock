// components/ui/screen_manager.h

#ifndef SCREEN_MANAGER_H
#define SCREEN_MANAGER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"

typedef enum {
    SCREEN_CLOCK,
    SCREEN_SETTINGS_MENU,
    SCREEN_WIFI_SETTINGS,
    SCREEN_BRIGHTNESS_SETTINGS,
    SCREEN_BACKGROUND_SELECTOR,
    SCREEN_OTA_SETTINGS,
    SCREEN_ANIMATION_PREVIEW,
    SCREEN_ABOUT,
    SCREEN_MAX
} screen_id_t;

/**
 * @brief Initialize the screen manager
 */
void screen_manager_init(void);

/**
 * @brief Set the clock screen reference
 * @param scr Clock screen object
 */
void screen_manager_set_clock_screen(lv_obj_t* scr);

/**
 * @brief Navigate to a screen (push onto stack)
 * @param screen Screen ID to navigate to
 */
void screen_manager_push(screen_id_t screen);

/**
 * @brief Go back to previous screen (pop from stack)
 */
void screen_manager_pop(void);

/**
 * @brief Return to home screen (clock)
 */
void screen_manager_home(void);

/**
 * @brief Get current screen ID
 * @return Current screen ID
 */
screen_id_t screen_manager_get_current(void);

#ifdef __cplusplus
}
#endif

#endif // SCREEN_MANAGER_H
