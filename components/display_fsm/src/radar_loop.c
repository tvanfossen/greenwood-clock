/**
 * @file radar_loop.c
 * @version 1
 * @brief Radar loop: decode the SD radar history off-lock, play it oldest to newest on the
 *        radar screen, holding the newest frame before repeating.
 */
#include "radar_loop.h"
#include "image_decode.h"
#include "nws.h"
#include "src/misc/cache/instance/lv_image_cache.h"  /* lv_image_cache_drop */

#include <string.h>
#include "esp_heap_caps.h"
#include "esp_log.h"

static const char *TAG = "radar_loop";

#define LOOP_FRAME_MS      600          /* time per frame */
#define LOOP_FADE_MS       450          /* crossfade into each frame (within LOOP_FRAME_MS) */
#define LOOP_HOLD_TICKS    4            /* extra ticks on the newest frame (2 s) */
#define LOOP_MIN_FREE_B    (8u << 20)   /* SPIRAM needed to start a decode (full frame + stb temp) */

typedef struct {
    lv_image_dsc_t *dsc;
    time_t          ts;
} loop_frame_t;

static loop_frame_t  s_frames[NWS_RADAR_HISTORY_MAX];
static time_t        s_list[NWS_RADAR_HISTORY_MAX];
static int           s_listed = -1;     /* frames listed on SD (-1: not listed yet) */
static int           s_next;            /* next listed frame to decode */
static int           s_decoded;         /* frames decoded (written off-lock) */
static int           s_ready;           /* frames published to playback (under lock) */
static bool          s_armed;
static lv_timer_t   *s_timer;
static radar_view_t *s_rv;
static int           s_idx;
static int           s_hold;

/**
 * @brief Start a loop session (no I/O): forget any previous frames' bookkeeping.
 * @version 1
 * @req REQ-RADAR-003
 */
void radar_loop_arm(void)
{
    radar_loop_stop();
    s_armed = true;
}

/**
 * @brief Decode history frame @p i into s_frames at half resolution, if memory allows.
 * v2: frames are kept at half resolution (0.6 MB instead of 2.4 MB) and shown at 2x: four
 *     full frames took SPIRAM down to ~6 MB and a fifth decode failed (outofmem) on device.
 * @version 2
 * @dg_internal
 * @return true if the frame was decoded.
 */
static bool decode_frame(int i)
{
    size_t free_b = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    if (free_b < LOOP_MIN_FREE_B) {
        ESP_LOGW(TAG, "skip frame %d: SPIRAM free %u B < %u B", i, (unsigned)free_b,
                 (unsigned)LOOP_MIN_FREE_B);
        return false;
    }
    char path[48];
    nws_radar_history_path(s_list[i], path, sizeof(path));
    lv_image_dsc_t *full = image_decode_png_file(path);
    lv_image_dsc_t *dsc = full ? image_decode_halve_argb(full) : NULL;
    image_decode_free(full);
    if (!dsc) {
        ESP_LOGW(TAG, "decode %s failed", path);
        return false;
    }
    s_frames[s_decoded].dsc = dsc;
    s_frames[s_decoded].ts  = s_list[i];
    ESP_LOGI(TAG, "frame %d/%d decoded (%s); SPIRAM free %u -> %u B", s_decoded + 1, s_listed,
             path, (unsigned)free_b, (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    return true;
}

/**
 * @brief Off the LVGL lock: list the history on first call, then decode the next frame.
 * @version 1
 * @req REQ-RADAR-003
 * @return true if more frames remain to decode.
 */
bool radar_loop_load_next(void)
{
    if (!s_armed) return false;
    if (s_listed < 0) {
        s_listed = nws_radar_history_list(s_list, NWS_RADAR_HISTORY_MAX);
        s_next = 0;
        ESP_LOGI(TAG, "%d history frame(s) on SD", s_listed);
    }
    if (s_next < s_listed) {
        if (decode_frame(s_next)) s_decoded++;
        s_next++;
    }
    return s_next < s_listed;
}

/**
 * @brief Playback timer (LVGL task): crossfade to the next published frame, holding the newest.
 * v2: crossfades between frames and moves the progress bar (was a hard frame swap).
 * v3: tells the view which frame is the newest.
 * v4: no progress (the bar is gone).
 * @version 4
 * @callback
 */
static void loop_timer_cb(lv_timer_t *t)
{
    LV_UNUSED(t);
    if (!s_rv || s_ready < 2) return;
    if (s_idx == s_ready - 1 && s_hold < LOOP_HOLD_TICKS) {
        s_hold++;
        return;
    }
    s_hold = 0;
    s_idx = (s_idx + 1) % s_ready;
    radar_view_crossfade_frame(s_rv, s_frames[s_idx].dsc, s_frames[s_idx].ts, LOOP_FADE_MS,
                               s_idx == s_ready - 1);
}

/**
 * @brief Under the lock: publish the decoded frames and start playback on @p rv once at
 *        least two frames are ready.
 * v2: passes the frames' positions to the view.
 * v3: no longer (the dial that drew them is gone).
 * @version 3
 * @req REQ-RADAR-003
 */
void radar_loop_attach(radar_view_t *rv)
{
    if (!s_armed || !rv) return;
    s_rv = rv;
    s_ready = s_decoded;
    if (s_ready >= 2 && !s_timer) {
        s_idx = s_ready - 1;   /* start on the newest (what the screen already shows) */
        s_hold = 0;
        s_timer = lv_timer_create(loop_timer_cb, LOOP_FRAME_MS, NULL);
        ESP_LOGI(TAG, "playback started (%d frames)", s_ready);
    }
}

/**
 * @brief Under the lock: stop playback and free every frame. Call after the view that
 *        showed the frames is destroyed.
 * @version 1
 * @req REQ-RADAR-003
 */
void radar_loop_stop(void)
{
    if (s_timer) {
        lv_timer_delete(s_timer);
        s_timer = NULL;
    }
    for (int i = 0; i < s_decoded; i++) {
        lv_image_cache_drop(s_frames[i].dsc);
        image_decode_free(s_frames[i].dsc);
    }
    if (s_decoded) ESP_LOGI(TAG, "stopped, %d frame(s) freed", s_decoded);
    memset(s_frames, 0, sizeof(s_frames));
    s_decoded = 0;
    s_next = 0;
    s_ready = 0;
    s_listed = -1;
    s_armed = false;
    s_rv = NULL;
}
