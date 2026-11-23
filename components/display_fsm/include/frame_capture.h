/**
 * @file frame_capture.h
 * @version 1
 * @brief Debug frame recorder: copies the next N rendered frames (half resolution, RGB565)
 *        into SPIRAM with their timestamps, so a transition can be reviewed frame by frame
 *        off-device. Armed from the HTTP task, recorded on the LVGL task at RENDER_READY
 *        (direct mode: the active buffer then holds the whole finished frame).
 */
#ifndef FRAME_CAPTURE_H
#define FRAME_CAPTURE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

#define FRAME_CAPTURE_MAX 24   /**< Frames per recording (each 512x300 RGB565 = 300 KB). */

/** A finished recording, valid until frame_capture_release(). */
typedef struct {
    uint16_t       count, w, h;      /**< Frames recorded and their size. */
    const int64_t *t_us;             /**< Render-ready time of each frame (esp_timer). */
    const uint8_t *pixels;           /**< count * w * h RGB565 frames, back to back. */
} frame_capture_result_t;

/**
 * @brief Hook the recorder to the default display. LVGL lock held; once, after LVGL init.
 * @version 1
 * @req REQ-API-003
 */
void frame_capture_attach(void);

/**
 * @brief Allocate and arm a recording of @p frames frames. With @p on_transition it starts
 *        at the next display state change, otherwise at the next rendered frame. Any task.
 * @version 1
 * @req REQ-API-003
 * @return ESP_OK; ESP_ERR_INVALID_STATE if not attached or already armed/recording;
 *         ESP_ERR_INVALID_ARG for a bad count; ESP_ERR_NO_MEM if SPIRAM is short.
 */
esp_err_t frame_capture_arm(int frames, bool on_transition);

/**
 * @brief Start an armed on-transition recording (called on each display state entry).
 * @version 1
 * @req REQ-API-003
 */
void frame_capture_on_transition(void);

/**
 * @brief Get the finished recording, ending a recording that has run its time limit. LVGL
 *        lock held (it excludes the RENDER_READY writer).
 * @version 1
 * @req REQ-API-003
 * @return true if one is complete (then @p out is filled), false while idle or recording.
 */
bool frame_capture_result(frame_capture_result_t *out);

/**
 * @brief The draw buffer on screen now (LVGL lock held): between refreshes buf_act is the
 *        one the next refresh draws into, which lacks the last frame's changes.
 * @version 1
 * @req REQ-API-003
 * @return The shown buffer, or NULL without a display.
 */
lv_draw_buf_t *frame_capture_shown_buffer(void);

/**
 * @brief Free the recording and return to idle. Any task.
 * @version 1
 * @req REQ-API-003
 */
void frame_capture_release(void);

#ifdef __cplusplus
}
#endif

#endif /* FRAME_CAPTURE_H */
