/**
 * @file settings_lists.h
 * @version 1
 * @brief What the clock offers to choose from, read from the device: the pictures in
 *        /sdcard/backgrounds, the theme files in /sdcard/themes and the clock animations.
 *        Shared by the clock's settings backend and the phone panel's API. SD I/O: call off
 *        the LVGL lock.
 */
#pragma once

#include "settings_backend.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Fill @p out (at most @p max) with the device's choices for @p which.
 * @version 1
 * @req REQ-UI-001
 * @return How many.
 */
int settings_lists_fill(settings_list_t which, settings_choice_t *out, int max);

#ifdef __cplusplus
}
#endif
