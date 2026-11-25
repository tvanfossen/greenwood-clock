/**
 * @file upload_view.h
 * @version 1
 * @brief Upload screen (prototype): pair a phone by QR code and list what it sent. Waiting:
 *        the QR code (the upload address with a one-time pairing code), how to use it and when
 *        the code expires. Connected: the session's time left and the files received, a clock
 *        animation with a Place button (its position is set on the clock). Draws only what it
 *        is given. Caller holds the LVGL lock.
 */
#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/** One received file. */
typedef struct {
    const char *name;    /**< File name as sent ("IMG_2041.jpg"). */
    const char *kind;    /**< "Background", "Album photo" or "Clock animation". */
    const char *detail;  /**< "1024 x 600", "in the slideshow", ... */
} upload_item_t;

/**
 * @brief The upload screen in a settings frame on @p parent. Waiting for a phone (@p items
 *        NULL): a QR code of @p url, its expiry in @p secs_left. Connected: the session's
 *        @p secs_left and the @p n files received.
 * @version 1
 * @req REQ-UI-005
 * @return The frame.
 */
lv_obj_t *upload_view_create(lv_obj_t *parent, const char *url, int secs_left,
                             const upload_item_t *items, int n, lv_event_cb_t back_cb);

#ifdef __cplusplus
}
#endif
