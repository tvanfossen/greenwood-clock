/**
 * @file phone_view.h
 * @version 1
 * @brief The Phone screen (Settings > Phone): a QR code holding the panel's address and a
 *        one-time pairing code, how to use it, when the code expires, how many phones are
 *        connected, and a button that disconnects them. Draws only what it is given; the
 *        caller refreshes the countdown with phone_view_update. Caller holds the LVGL lock.
 */
#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief The Phone screen in a settings frame on @p parent: a QR code of @p url (the part
 *        after '#' is the pairing code and is not printed), Back calling @p back_cb,
 *        Disconnect calling @p disconnect_cb.
 * @version 1
 * @req REQ-UI-005
 * @return The frame.
 */
lv_obj_t *phone_view_create(lv_obj_t *parent, const char *url, lv_event_cb_t back_cb,
                            lv_event_cb_t disconnect_cb);

/**
 * @brief Show the code's time left (@p secs_left; 0 or less: "used or expired") and the
 *        number of connected phones.
 * @version 1
 * @req REQ-UI-005
 */
void phone_view_update(int secs_left, int phones);

#ifdef __cplusplus
}
#endif
