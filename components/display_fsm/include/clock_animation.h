/**
 * @file clock_animation.h
 * @version 1
 * @brief The ambient animation on the full clock screen, selectable in Settings.
 *
 * One animation is "active" (from clock_anim_pref). show()/hide() are driven
 * by the display FSM (ClockFull entry / exit); select() swaps the active
 * animation. Inactive animations stay cached (hidden + paused), so switching
 * back costs no reload. All functions are safe with or without the LVGL lock
 * held (esp_lvgl_port's lock is recursive).
 */
#ifndef CLOCK_ANIMATION_H
#define CLOCK_ANIMATION_H

#include "lvgl.h"
#include "clock_anim_pref.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Bind to the clock screen, load the stored choice and show it.
 * @version 1
 * @req REQ-ANIM-001
 */
void clock_animation_init(lv_obj_t *scr);

/**
 * @brief Make @p anim the active animation; the previous one is hidden, and if it was on
 *        screen the new one is shown in its place.
 * @version 1
 * @req REQ-ANIM-001
 */
void clock_animation_select(clock_anim_t anim);

/**
 * @brief Show (creating on first use) the active animation.
 * @version 1
 * @req REQ-ANIM-001
 */
void clock_animation_show(void);

/**
 * @brief Hide and pause the active animation.
 * @version 1
 * @req REQ-ANIM-001
 */
void clock_animation_hide(void);

#ifdef __cplusplus
}
#endif

#endif /* CLOCK_ANIMATION_H */
