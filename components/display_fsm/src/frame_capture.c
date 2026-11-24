/**
 * @file frame_capture.c
 * @version 1
 * @brief Debug frame recorder (see frame_capture.h). State moves IDLE -> ARMED -> RECORDING ->
 *        DONE -> IDLE; each step has a single writer task, and the state is an atomic so
 *        the buffer is published before the step that hands it over.
 */
#include "frame_capture.h"
#include "src/display/lv_display_private.h"   /* buf_1/buf_2/buf_act: the frame just shown */

#include <stdatomic.h>
#include <string.h>
#include "esp_cache.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "lvgl.h"

static const char *TAG = "frame_cap";

/** Keep this much SPIRAM free beyond the recording (radar loop, decodes). */
#define FRAME_CAPTURE_HEADROOM (4 * 1024 * 1024)

/** A recording also ends this long after it starts (a still screen renders no frames). */
#define FRAME_CAPTURE_MAX_US (2500 * 1000)
/** An armed recording whose state change never came is abandoned after this long. */
#define FRAME_CAPTURE_ARM_US (8000 * 1000)

typedef enum { CAP_IDLE, CAP_ARMED, CAP_RECORDING, CAP_DONE } cap_state_t;

static _Atomic int   s_state = CAP_IDLE;
static bool          s_attached;
static uint16_t      s_want, s_count, s_w, s_h;
static uint8_t      *s_pixels;
static int64_t       s_t_us[FRAME_CAPTURE_MAX];
static int64_t       s_start_us;   /* recording start: arm (now) or the state change */

/**
 * @brief The buffer holding the frame that was just rendered. Direct mode, double buffered:
 *        LVGL swaps buf_act on the last flush, so at RENDER_READY the finished frame is the
 *        other buffer.
 * @version 1
 * @utility
 * @return The finished frame's draw buffer, or NULL.
 */
static lv_draw_buf_t *finished_frame(const lv_display_t *disp)
{
    if (!disp->buf_2) return disp->buf_act;
    return disp->buf_act == disp->buf_1 ? disp->buf_2 : disp->buf_1;
}

/**
 * @brief The draw buffer on screen now (LVGL lock held): between refreshes buf_act is the
 *        one the next refresh draws into, which lacks the last frame's changes.
 * @version 1
 * @req REQ-API-003
 * @return The shown buffer, or NULL without a display.
 */
lv_draw_buf_t *frame_capture_shown_buffer(void)
{
    lv_display_t *disp = lv_display_get_default();
    return disp ? finished_frame(disp) : NULL;
}

/**
 * @brief Copy one RGB565 frame at half resolution (every other pixel of every other row).
 * @version 1
 * @utility
 */
static void copy_half(const lv_draw_buf_t *fb, uint16_t *dst)
{
    for (uint16_t y = 0; y < s_h; y++) {
        const uint16_t *row = (const uint16_t *)(fb->data + (size_t)(y * 2) * fb->header.stride);
        for (uint16_t x = 0; x < s_w; x++) *dst++ = row[x * 2];
    }
}

/**
 * @brief RENDER_READY hook (LVGL task): record the finished frame while recording.
 * @version 1
 * @callback
 */
static void render_ready_cb(lv_event_t *e)
{
    if (atomic_load(&s_state) != CAP_RECORDING) return;
    lv_display_t *disp = (lv_display_t *)lv_event_get_target(e);
    lv_draw_buf_t *fb = finished_frame(disp);
    if (!fb || fb->header.cf != LV_COLOR_FORMAT_RGB565) {
        ESP_LOGW(TAG, "unsupported framebuffer; recording stopped at %u", s_count);
        atomic_store(&s_state, CAP_DONE);
        return;
    }
    s_t_us[s_count] = esp_timer_get_time();
    // The flush wrote the CPU's lines back, but the PPA writes memory directly: drop any
    // cached lines so the copy reads what the panel shows, not what this CPU last saw.
    esp_err_t err = esp_cache_msync(fb->data, fb->data_size,
                                    ESP_CACHE_MSYNC_FLAG_DIR_M2C | ESP_CACHE_MSYNC_FLAG_TYPE_DATA);
    if (err != ESP_OK) ESP_LOGW(TAG, "cache invalidate failed: %s", esp_err_to_name(err));
    copy_half(fb, (uint16_t *)(s_pixels + (size_t)s_count * s_w * s_h * 2));
    if (++s_count >= s_want) {
        atomic_store(&s_state, CAP_DONE);
        ESP_LOGI(TAG, "recorded %u frames over %lld ms", s_count,
                 (s_t_us[s_count - 1] - s_t_us[0]) / 1000);
    }
}

/**
 * @brief Hook the recorder to the default display. LVGL lock held; once, after LVGL init.
 * @version 1
 * @req REQ-API-003
 */
void frame_capture_attach(void)
{
    lv_display_t *disp = lv_display_get_default();
    if (s_attached || !disp) return;
    s_w = (uint16_t)(lv_display_get_horizontal_resolution(disp) / 2);
    s_h = (uint16_t)(lv_display_get_vertical_resolution(disp) / 2);
    lv_display_add_event_cb(disp, render_ready_cb, LV_EVENT_RENDER_READY, NULL);
    s_attached = true;
}

/**
 * @brief Allocate and arm a recording of @p frames frames. With @p on_transition it starts
 *        at the next display state change, otherwise at the next rendered frame. Any task.
 * @version 1
 * @req REQ-API-003
 * @return ESP_OK; ESP_ERR_INVALID_STATE if not attached or already armed/recording;
 *         ESP_ERR_INVALID_ARG for a bad count; ESP_ERR_NO_MEM if SPIRAM is short.
 */
esp_err_t frame_capture_arm(int frames, bool on_transition)
{
    if (frames < 1 || frames > FRAME_CAPTURE_MAX) return ESP_ERR_INVALID_ARG;
    if (!s_attached || atomic_load(&s_state) != CAP_IDLE) return ESP_ERR_INVALID_STATE;
    size_t bytes = (size_t)frames * s_w * s_h * 2;
    size_t free_b = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    uint8_t *buf = (free_b > bytes + FRAME_CAPTURE_HEADROOM)
                       ? heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) : NULL;
    ESP_LOGI(TAG, "arm %d frames (%u KB, SPIRAM free %u KB, %s): %s", frames,
             (unsigned)(bytes / 1024), (unsigned)(free_b / 1024),
             on_transition ? "on transition" : "now", buf ? "ok" : "no memory");
    if (!buf) return ESP_ERR_NO_MEM;
    s_pixels = buf;
    s_want = (uint16_t)frames;
    s_count = 0;
    s_start_us = esp_timer_get_time();
    atomic_store(&s_state, on_transition ? CAP_ARMED : CAP_RECORDING);
    return ESP_OK;
}

/**
 * @brief Start an armed on-transition recording (called on each display state entry).
 * @version 1
 * @req REQ-API-003
 */
void frame_capture_on_transition(void)
{
    int armed = CAP_ARMED;
    s_start_us = esp_timer_get_time();
    if (atomic_compare_exchange_strong(&s_state, &armed, CAP_RECORDING)) {
        ESP_LOGI(TAG, "transition: recording %u frames", s_want);
    }
}

/**
 * @brief Get the finished recording, ending a recording that has run its time limit. LVGL
 *        lock held (it excludes the RENDER_READY writer).
 * @version 1
 * @req REQ-API-003
 * @return true if one is complete (then @p out is filled), false while idle or recording.
 */
bool frame_capture_result(frame_capture_result_t *out)
{
    int st = atomic_load(&s_state);
    int64_t age = esp_timer_get_time() - s_start_us;
    if ((st == CAP_RECORDING && age > FRAME_CAPTURE_MAX_US) ||
        (st == CAP_ARMED && age > FRAME_CAPTURE_ARM_US)) {
        ESP_LOGI(TAG, "time limit: %s ends with %u frames",
                 st == CAP_ARMED ? "armed wait" : "recording", s_count);
        atomic_store(&s_state, CAP_DONE);
    }
    bool done = atomic_load(&s_state) == CAP_DONE;
    if (done) {
        out->count = s_count;
        out->w = s_w;
        out->h = s_h;
        out->t_us = s_t_us;
        out->pixels = s_pixels;
    }
    return done;
}

/**
 * @brief Free the recording and return to idle. Any task.
 * @version 1
 * @req REQ-API-003
 */
void frame_capture_release(void)
{
    int st = atomic_load(&s_state);
    if (st == CAP_RECORDING) return;   // the LVGL task is writing; release once done
    heap_caps_free(s_pixels);
    s_pixels = NULL;
    s_count = 0;
    atomic_store(&s_state, CAP_IDLE);
}
