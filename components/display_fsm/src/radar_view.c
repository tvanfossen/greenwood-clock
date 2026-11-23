/**
 * @file radar_view.c
 * @version 1
 * @brief Layer 1: RadarView — map background + radar overlay + home marker.
 *
 * Caller must hold LVGL lock for all functions, except radar_view_preload_map()
 * and radar_view_predecode(), which run off-lock.
 */

#include "display_widgets.h"
#include "image_decode.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_cache.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

static const char *TAG = "radar_view";

// Wrapper for lv_obj_set_style_opa — lv_anim_exec_xcb_t is (void*, int32_t)
// but lv_obj_set_style_opa takes 3 args (obj, value, selector).
// Casting directly leaves selector as garbage on RISC-V → crash.
/**
 * @brief Animation exec callback: set the home marker's opacity to @p v.
 * @version 1
 * @callback
 */
static void pulse_opa_cb(void *obj, int32_t v)
{
    lv_obj_set_style_opa((lv_obj_t *)obj, (lv_opa_t)v, 0);
}

#define SCREEN_W  1024
#define SCREEN_H  600
#define RADAR_BBOX_DEG 2.0f  // +/- degrees from center (matches nws_radar.c)
#define HOME_MARKER_SIZE 10
#define RADAR_OPA        LV_OPA_40   // overlay opacity over the basemap
#define PROGRESS_MAX     1000        // progress bar range (permille of the loop's time span)

// Cached map background — decoded once, reused across radar view lifecycle.
static uint8_t        *s_map_pixels;  // ARGB8888 pixel data in SPIRAM
static lv_image_dsc_t *s_map_dsc;     // image descriptor pointing to s_map_pixels

// Radar overlay — two-tier cache to avoid use-after-free.
// predecode() writes to s_pending_* (no LVGL lock).
// apply_radar() swaps s_pending_* → s_active_* under LVGL lock (safe for render thread).
// Expires after RADAR_MAX_AGE_S to avoid displaying stale data.
#define RADAR_MAX_AGE_S  1800  // 30 minutes

// Active: currently referenced by LVGL image — only freed under LVGL lock
static uint8_t        *s_active_radar_pixels;
static lv_image_dsc_t *s_active_radar_dsc;
static time_t          s_active_radar_ts;

// Pending: written by predecode, consumed by apply_radar
static uint8_t        *s_pending_radar_pixels;
static lv_image_dsc_t *s_pending_radar_dsc;
static time_t          s_pending_radar_ts;

struct radar_view_t {
    lv_obj_t       *container;
    lv_obj_t       *img_map;         // static map background from SD card
    lv_obj_t       *img_radar;       // transparent radar overlay
    lv_obj_t       *img_radar_b;     // second overlay for loop crossfades
    lv_obj_t       *img_front;       // overlay currently shown (img_radar or img_radar_b)
    lv_obj_t       *bar_progress;    // radar-loop position in its time span (hidden until it plays)
    lv_obj_t       *home_marker;     // crosshair/dot at home lat/lon
    lv_obj_t       *lbl_updated;     // "Last updated: HH:MM"
    float           center_lat;
    float           center_lon;
};

// Convert lat/lon to pixel position within the radar view.
// The radar image covers center +/- RADAR_BBOX_DEG in both axes.
/**
 * @brief Linearly map @p lat / @p lon to screen pixels for a 1024x600 view
 *        spanning center +/- 2 degrees (north at the top).
 * @version 1
 * @req REQ-RADAR-001
 */
static void latlon_to_pixel(float lat, float lon, float center_lat, float center_lon,
                            int *px_x, int *px_y)
{
    float x_frac = (lon - (center_lon - RADAR_BBOX_DEG)) / (2.0f * RADAR_BBOX_DEG);
    float y_frac = ((center_lat + RADAR_BBOX_DEG) - lat) / (2.0f * RADAR_BBOX_DEG);

    *px_x = (int)(x_frac * SCREEN_W);
    *px_y = (int)(y_frac * SCREEN_H);
}

// Parse + validate the 8-byte map header. Sets mw/mh/file_stride on success.
/**
 * @brief Read the map binary's [u16 w][u16 h][u32 stride] header and validate it
 *        (non-zero, at most 2048x1200, file large enough for w*h*4 pixels).
 * @version 1
 * @req REQ-RADAR-001
 * @return true if the header was read and is valid.
 */
static bool read_map_header(FILE *f, long fsize, uint16_t *mw, uint16_t *mh,
                            uint32_t *file_stride)
{
    if (fsize < 8 ||
        fread(mw, sizeof(*mw), 1, f) != 1 ||
        fread(mh, sizeof(*mh), 1, f) != 1 ||
        fread(file_stride, sizeof(*file_stride), 1, f) != 1) {
        ESP_LOGE(TAG, "Map binary header read failed");
        return false;
    }
    long expected = 8 + (long)((uint32_t)*mw * *mh * 4);
    if (*mw == 0 || *mh == 0 || *mw > 2048 || *mh > 1200 || fsize < expected) {
        ESP_LOGE(TAG, "Map binary invalid: dims=%ux%u file=%ld expected>=%ld",
                 *mw, *mh, fsize, expected);
        return false;
    }
    return true;
}

// Read pixel rows into the stride-aligned buffer, skipping any file padding
// when the file stride exceeds the tight row width.
/**
 * @brief Read @p mh rows of @p mw x 4 bytes into @p mpix at @p mstride, seeking
 *        past file padding when @p file_stride is wider.
 * @version 1
 * @req REQ-RADAR-001
 * @return true if every row was read; false on a short read.
 */
static bool read_map_rows(FILE *f, uint8_t *mpix, uint16_t mw, uint16_t mh,
                          uint32_t mstride, uint32_t file_stride)
{
    size_t row_bytes = (size_t)mw * 4;
    for (unsigned y = 0; y < mh; y++) {
        uint8_t *dst = mpix + y * mstride;
        if (fread(dst, 1, row_bytes, f) != row_bytes) {
            ESP_LOGE(TAG, "Map read failed at row %u", y);
            return false;
        }
        if (file_stride > row_bytes) {
            fseek(f, (long)(file_stride - row_bytes), SEEK_CUR);
        }
    }
    return true;
}

// Cache-sync the filled pixel buffer and publish it as s_map_dsc/s_map_pixels.
// Takes ownership of mpix (frees it if the descriptor alloc fails).
//
// No vertical flip: the radar map + overlay composite via LVGL's software draw
// path (upright). No image path flips; stb decode is pinned top-down.
// Verified with a colour-coded orientation test card.
/**
 * @brief Cache-sync the filled map buffer and publish it as an XRGB8888 image
 *        descriptor in s_map_dsc / s_map_pixels (frees @p mpix if the descriptor
 *        allocation fails).
 * @version 1
 * @req REQ-RADAR-001
 */
static void publish_map_dsc(uint8_t *mpix, uint16_t mw, uint16_t mh,
                            uint32_t mstride, uint32_t mbuf_sz)
{
    esp_cache_msync(mpix, mbuf_sz,
                    ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_TYPE_DATA);

    lv_image_dsc_t *mdsc = lv_malloc(sizeof(lv_image_dsc_t));
    if (!mdsc) {
        heap_caps_free(mpix);
        ESP_LOGE(TAG, "Map descriptor alloc failed");
        return;
    }
    memset(mdsc, 0, sizeof(*mdsc));
    mdsc->header.magic  = LV_IMAGE_HEADER_MAGIC;
    // Opaque base map: the raw binary's alpha byte is 0, so it must be treated as
    // XRGB8888 (alpha ignored). As ARGB8888 the PPA alpha-blend path would honor
    // the 0 alpha and render it fully transparent (map vanishes).
    mdsc->header.cf     = LV_COLOR_FORMAT_XRGB8888;
    mdsc->header.w      = mw;
    mdsc->header.h      = mh;
    mdsc->header.stride = mstride;
    mdsc->data_size     = mbuf_sz;
    mdsc->data          = mpix;

    s_map_pixels = mpix;
    s_map_dsc    = mdsc;
    ESP_LOGI(TAG, "Map loaded: %ux%u stride=%lu data_size=%lu (raw binary, no decode)",
             mw, mh, (unsigned long)mstride, (unsigned long)mbuf_sz);
}

// Load /sdcard/maps/local.bin into s_map_dsc (once). Raw BGRA, bypasses lodepng.
// Format: [u16 width][u16 height][u32 stride][BGRA pixel data].
// Does SD I/O + a multi-MB SPIRAM fill — callers must NOT hold the LVGL lock.
/**
 * @brief Load /sdcard/maps/local.bin into a 64-byte-aligned SPIRAM buffer and
 *        publish it as the basemap, once; no-op if already cached, absent or invalid.
 * @version 1
 * @req REQ-RADAR-001
 */
static void load_map_dsc(void)
{
    if (s_map_dsc) return;  // already cached

    FILE *f = fopen("/sdcard/maps/local.bin", "rb");
    if (!f) {
        ESP_LOGI(TAG, "No map background (/sdcard/maps/local.bin not found)");
        return;
    }
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);
    ESP_LOGI(TAG, "Map binary: %ld bytes", fsize);

    uint16_t mw = 0, mh = 0;
    uint32_t file_stride = 0;
    if (!read_map_header(f, fsize, &mw, &mh, &file_stride)) {
        fclose(f);
        return;
    }

    uint32_t mstride = lv_draw_buf_width_to_stride(mw, LV_COLOR_FORMAT_ARGB8888);
    uint32_t mbuf_sz = mstride * mh;
    uint8_t *mpix = (uint8_t *)heap_caps_aligned_alloc(64, mbuf_sz,
                                                       MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!mpix) {
        ESP_LOGE(TAG, "Map SPIRAM alloc failed (%lu B)", (unsigned long)mbuf_sz);
        fclose(f);
        return;
    }
    memset(mpix, 0, mbuf_sz);

    bool ok = read_map_rows(f, mpix, mw, mh, mstride, file_stride);
    fclose(f);
    if (!ok) {
        heap_caps_free(mpix);
        return;
    }
    publish_map_dsc(mpix, mw, mh, mstride, mbuf_sz);
}

/**
 * @brief Access the cached radar basemap descriptor.
 * @version 1
 * @req REQ-RADAR-001
 * @return Upright basemap image descriptor, or NULL if no map is loaded.
 */
const lv_image_dsc_t *radar_view_map_dsc(void)
{
    return s_map_dsc;   // upright (raw binary, not flipped); NULL if no map loaded
}

/**
 * @brief Preload the basemap off the LVGL lock (idempotent), so radar view
 *        creation never reads the SD card under the lock.
 * @version 1
 * @req REQ-RADAR-001
 */
void radar_view_preload_map(void)
{
    // Off-lock startup preload so the first radar transition doesn't pay the
    // ~2.4 MB SD read under the LVGL lock. Idempotent (load_map_dsc no-ops if cached).
    load_map_dsc();
}

/**
 * @brief Build the full-screen radar view: preloaded basemap (skipped if not yet
 *        loaded), 40 %-opacity radar overlay, pulsing home marker at the centre
 *        and a "Last updated" chip.
 * v2: a second overlay for loop crossfades and a (hidden) loop progress bar.
 * v3: no container fill when the basemap covers the screen.
 * @version 3
 * @req REQ-RADAR-001
 * @return The new view, or NULL if allocation failed.
 */
radar_view_t *radar_view_create(lv_obj_t *parent, float lat, float lon)
{
    radar_view_t *rv = lv_malloc(sizeof(radar_view_t));
    if (!rv) {
        ESP_LOGE(TAG, "Failed to allocate radar_view_t");
        return NULL;
    }
    memset(rv, 0, sizeof(*rv));
    rv->center_lat = lat;
    rv->center_lon = lon;

    // Full-screen container
    rv->container = lv_obj_create(parent);
    lv_obj_set_size(rv->container, SCREEN_W, SCREEN_H);
    lv_obj_align(rv->container, LV_ALIGN_TOP_LEFT, 0, 0);
    // Dark backing only without a basemap: the map covers the screen, and while the view
    // fades in a translucent full-screen fill is software-blended (~75 ms a frame, measured).
    lv_obj_set_style_bg_color(rv->container, lv_color_hex(0x0a0a1e), 0);
    lv_obj_set_style_bg_opa(rv->container, s_map_dsc ? LV_OPA_TRANSP : LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(rv->container, 0, 0);
    lv_obj_set_style_pad_all(rv->container, 0, 0);
    lv_obj_set_style_radius(rv->container, 0, 0);
    lv_obj_clear_flag(rv->container, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    // Map background — pre-rendered raw BGRA binary. NEVER read it here:
    // radar_view_create() runs UNDER the LVGL lock (RadarOverlay::entry), so the
    // 2.4MB fread would stall the render thread → display watchdog reset. The map is
    // preloaded OFF-lock (radar_view_preload_map). If that hasn't succeeded yet,
    // render radar WITHOUT the basemap (fail fast) instead of blocking on SD.
    rv->img_map = NULL;
    if (s_map_dsc) {
        rv->img_map = lv_image_create(rv->container);
        lv_image_set_src(rv->img_map, s_map_dsc);
        lv_obj_align(rv->img_map, LV_ALIGN_TOP_LEFT, 0, 0);
    } else {
        ESP_LOGW(TAG, "radar: basemap not preloaded — rendering without it (no on-lock read)");
    }

    // Radar overlay image (initially empty — set via radar_view_apply_radar)
    rv->img_radar = lv_image_create(rv->container);
    lv_obj_align(rv->img_radar, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_image_opa(rv->img_radar, RADAR_OPA, 0);
    // Second overlay: the radar loop crossfades between the two (fully transparent until used).
    rv->img_radar_b = lv_image_create(rv->container);
    lv_obj_align(rv->img_radar_b, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_image_opa(rv->img_radar_b, LV_OPA_TRANSP, 0);
    rv->img_front = rv->img_radar;

    // Home marker — pulsing red dot at lat/lon position
    rv->home_marker = lv_obj_create(rv->container);
    lv_obj_set_size(rv->home_marker, HOME_MARKER_SIZE, HOME_MARKER_SIZE);
    lv_obj_set_style_bg_color(rv->home_marker, lv_color_hex(0xFF3333), 0);
    lv_obj_set_style_bg_opa(rv->home_marker, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(rv->home_marker, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_color(rv->home_marker, lv_color_white(), 0);
    lv_obj_set_style_border_width(rv->home_marker, 2, 0);
    lv_obj_clear_flag(rv->home_marker, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    // Position home marker
    int hx, hy;
    latlon_to_pixel(lat, lon, lat, lon, &hx, &hy);
    lv_obj_set_pos(rv->home_marker, hx - HOME_MARKER_SIZE / 2, hy - HOME_MARKER_SIZE / 2);

    // Pulse animation on home marker (opacity)
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, rv->home_marker);
    lv_anim_set_values(&a, LV_OPA_60, LV_OPA_COVER);
    lv_anim_set_time(&a, 1000);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_playback_time(&a, 1000);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_set_exec_cb(&a, pulse_opa_cb);
    lv_anim_start(&a);

    // "Last updated" label
    rv->lbl_updated = lv_label_create(rv->container);
    lv_obj_set_style_text_font(rv->lbl_updated, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(rv->lbl_updated, lv_color_hex(0xe0e0e0), 0);
    // Dark semi-opaque backing chip: the map (and bright precip cells) are an
    // image, so no single text colour is guaranteed — the chip is. Padding +
    // radius make it a readable pill.
    lv_obj_set_style_bg_color(rv->lbl_updated, lv_color_hex(0x0a0a1e), 0);
    lv_obj_set_style_bg_opa(rv->lbl_updated, LV_OPA_70, 0);
    lv_obj_set_style_pad_hor(rv->lbl_updated, 8, 0);
    lv_obj_set_style_pad_ver(rv->lbl_updated, 4, 0);
    lv_obj_set_style_radius(rv->lbl_updated, 6, 0);
    lv_obj_align(rv->lbl_updated, LV_ALIGN_BOTTOM_LEFT, 16, -22);
    lv_label_set_text(rv->lbl_updated, "Radar loading...");

    // Radar-loop progress: a thin bar along the bottom edge, sliding with the frame times.
    rv->bar_progress = lv_bar_create(rv->container);
    lv_obj_set_size(rv->bar_progress, SCREEN_W - 32, 6);
    lv_obj_align(rv->bar_progress, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_bar_set_range(rv->bar_progress, 0, PROGRESS_MAX);
    lv_obj_set_style_bg_color(rv->bar_progress, lv_color_hex(0x0a0a1e), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(rv->bar_progress, LV_OPA_60, LV_PART_MAIN);
    lv_obj_set_style_bg_color(rv->bar_progress, lv_color_hex(0xe0e0e0), LV_PART_INDICATOR);
    lv_obj_add_flag(rv->bar_progress, LV_OBJ_FLAG_HIDDEN);

    ESP_LOGI(TAG, "RadarView created (center=%.3f,%.3f)", lat, lon);
    return rv;
}

/**
 * @brief Stop the view's animations, delete its LVGL objects and free it; the
 *        module-level map and radar caches are kept. NULL-safe.
 * v2: also stops a running loop crossfade.
 * @version 2
 * @req REQ-RADAR-001
 */
void radar_view_destroy(radar_view_t *rv)
{
    if (!rv) return;
    if (rv->home_marker) lv_anim_delete(rv->home_marker, NULL);
    lv_anim_delete(rv, NULL);   // loop crossfade (its var is the view)
    if (rv->container) {
        lv_anim_delete(rv->container, NULL);
        lv_obj_delete(rv->container);
    }
    // Map and radar data are module-level caches — not freed here
    lv_free(rv);
    ESP_LOGI(TAG, "RadarView destroyed");
}

/**
 * @brief Access the view's root container.
 * @version 1
 * @req REQ-RADAR-001
 * @return The container object, or NULL if @p rv is NULL.
 */
lv_obj_t *radar_view_container(const radar_view_t *rv)
{
    return rv ? rv->container : NULL;
}

/**
 * @brief Decode a radar PNG to top-down (north-up) ARGB8888 off the LVGL lock and
 *        stage it as the pending overlay, replacing any unconsumed pending one.
 * v2: no flip; decode is pinned top-down (the old flip undid stb's accidental bottom-up decode).
 * @version 2
 * @req REQ-RADAR-001
 */
void radar_view_predecode(const uint8_t *png_data, size_t len)
{
    if (!png_data || len == 0) return;

    // Decode into a persistent SPIRAM ARGB8888 descriptor (shared helper).
    lv_image_dsc_t *dsc = image_decode_png_buf(png_data, len);
    if (!dsc) {
        ESP_LOGE(TAG, "Radar predecode failed (len=%zu)", len);
        return;
    }

    // Decode is top-down (north up), matching the raw north-up basemap.

    // Stage new radar (free previous pending if unconsumed). Pixel buffer and
    // descriptor are tracked separately to match the active/pending swap in
    // radar_view_apply_radar(); both were allocated with heap_caps and free the
    // same way.
    if (s_pending_radar_pixels) heap_caps_free(s_pending_radar_pixels);
    if (s_pending_radar_dsc)    heap_caps_free(s_pending_radar_dsc);
    s_pending_radar_pixels = (uint8_t *)dsc->data;
    s_pending_radar_dsc    = dsc;
    time(&s_pending_radar_ts);

    ESP_LOGI(TAG, "Radar staged: %lux%lu (%lu bytes)",
             (unsigned long)dsc->header.w, (unsigned long)dsc->header.h,
             (unsigned long)dsc->data_size);
}

/**
 * @brief Set the chip to "<prefix> h:mm AM/PM (Nm ago)" for frame time @p ts.
 * @version 1
 * @dg_internal
 */
static void set_time_label(radar_view_t *rv, const char *prefix, time_t ts)
{
    time_t now;
    time(&now);
    struct tm ti;
    localtime_r(&ts, &ti);
    char buf[64];
    snprintf(buf, sizeof(buf), "%s %d:%02d %s (%ldm ago)", prefix,
             ti.tm_hour % 12 == 0 ? 12 : ti.tm_hour % 12, ti.tm_min,
             ti.tm_hour >= 12 ? "PM" : "AM", (long)(now - ts) / 60);
    lv_label_set_text(rv->lbl_updated, buf);
}

/**
 * @brief The overlay not currently shown (the crossfade target).
 * @version 1
 * @dg_internal
 * @return The back overlay image.
 */
static lv_obj_t *overlay_back(const radar_view_t *rv)
{
    return (rv->img_front == rv->img_radar) ? rv->img_radar_b : rv->img_radar;
}

/**
 * @brief Show @p dsc on overlay @p img, scaled to fill the screen width (half-resolution
 *        loop frames draw at 2x, an exact PPA scale; full-resolution images at 1x).
 * @version 1
 * @dg_internal
 */
static void overlay_set(lv_obj_t *img, const lv_image_dsc_t *dsc)
{
    lv_image_set_src(img, dsc);
    lv_image_set_pivot(img, 0, 0);
    lv_image_set_scale(img, (dsc && dsc->header.w) ? 256 * SCREEN_W / dsc->header.w : 256);
}

/**
 * @brief Detach @p dsc from whichever overlay still shows it (before it is freed).
 * @version 1
 * @dg_internal
 */
static void overlay_detach(radar_view_t *rv, const lv_image_dsc_t *dsc)
{
    if (lv_image_get_src(rv->img_radar) == dsc)   lv_image_set_src(rv->img_radar, NULL);
    if (lv_image_get_src(rv->img_radar_b) == dsc) lv_image_set_src(rv->img_radar_b, NULL);
}

/**
 * @brief Crossfade step: the back overlay rises to RADAR_OPA as the front one falls to 0.
 * @version 1
 * @callback
 */
static void crossfade_exec_cb(void *var, int32_t t)   // t: 0 .. 256
{
    radar_view_t *rv = (radar_view_t *)var;
    lv_obj_set_style_image_opa(overlay_back(rv), (lv_opa_t)(RADAR_OPA * t / 256), 0);
    lv_obj_set_style_image_opa(rv->img_front, (lv_opa_t)(RADAR_OPA * (256 - t) / 256), 0);
}

/**
 * @brief Crossfade finished: the back overlay becomes the front one.
 * @version 1
 * @callback
 */
static void crossfade_done_cb(lv_anim_t *a)
{
    radar_view_t *rv = (radar_view_t *)lv_anim_get_user_data(a);
    rv->img_front = overlay_back(rv);
}

/**
 * @brief Under the LVGL lock: crossfade the overlay to one radar-loop frame over @p fade_ms,
 *        show its time on the chip, and slide the progress bar to @p progress (0..1000 of
 *        the loop's time span) over the same time. A crossfade still running is finished
 *        first, so the frames never mix three-way.
 * v2: scales the frame to the screen width (loop frames are half resolution).
 * @version 2
 * @req REQ-RADAR-003
 */
void radar_view_crossfade_frame(radar_view_t *rv, const lv_image_dsc_t *dsc, time_t ts,
                                uint32_t fade_ms, int32_t progress)
{
    if (!rv || !dsc) return;
    lv_anim_t *running = lv_anim_get(rv, crossfade_exec_cb);
    if (running) {
        crossfade_exec_cb(rv, 256);
        lv_anim_delete(rv, crossfade_exec_cb);   // no completed_cb on delete: swap here
        rv->img_front = overlay_back(rv);
    }
    overlay_set(overlay_back(rv), dsc);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, rv);
    lv_anim_set_values(&a, 0, 256);
    lv_anim_set_duration(&a, fade_ms);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_set_exec_cb(&a, crossfade_exec_cb);
    lv_anim_set_user_data(&a, rv);
    lv_anim_set_completed_cb(&a, crossfade_done_cb);
    lv_anim_start(&a);

    set_time_label(rv, "Radar", ts);
    lv_obj_remove_flag(rv->bar_progress, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_anim_duration(rv->bar_progress, fade_ms, LV_PART_MAIN);
    lv_bar_set_value(rv->bar_progress, LV_CLAMP(0, progress, PROGRESS_MAX), LV_ANIM_ON);
}

/**
 * @brief Under the LVGL lock, promote the pending overlay to active (freeing the
 *        old one), then show it with a "Last updated h:mm AM/PM (Nm ago)" label,
 *        or discard it and show "Radar data expired" if older than 30 min.
 * v2: label via set_time_label (shared with the radar loop).
 * v3: sets the front overlay (the loop crossfades between two).
 * v4: via overlay_set (resets the scale a half-resolution loop frame may have left).
 * @version 4
 * @req REQ-RADAR-001
 */
void radar_view_apply_radar(radar_view_t *rv)
{
    if (!rv) return;

    // Swap pending → active under LVGL lock (safe: render thread won't read freed memory)
    if (s_pending_radar_dsc) {
        // Detach old active from LVGL before freeing (either overlay may hold it)
        if (s_active_radar_dsc) overlay_detach(rv, s_active_radar_dsc);
        if (s_active_radar_pixels) heap_caps_free(s_active_radar_pixels);
        if (s_active_radar_dsc)    heap_caps_free(s_active_radar_dsc);

        s_active_radar_pixels = s_pending_radar_pixels;
        s_active_radar_dsc    = s_pending_radar_dsc;
        s_active_radar_ts     = s_pending_radar_ts;
        s_pending_radar_pixels = NULL;
        s_pending_radar_dsc    = NULL;
    }

    if (!s_active_radar_dsc) return;

    // Expire stale radar data
    time_t now;
    time(&now);
    long age = (long)(now - s_active_radar_ts);
    if (age > RADAR_MAX_AGE_S) {
        ESP_LOGW(TAG, "Radar expired: age=%ld s (max %d s) — discarding", age, RADAR_MAX_AGE_S);
        overlay_detach(rv, s_active_radar_dsc);
        heap_caps_free(s_active_radar_pixels); s_active_radar_pixels = NULL;
        heap_caps_free(s_active_radar_dsc);    s_active_radar_dsc = NULL;
        lv_label_set_text(rv->lbl_updated, "Radar data expired");
        return;
    }

    overlay_set(rv->img_front, s_active_radar_dsc);

    set_time_label(rv, "Last updated:", s_active_radar_ts);

    ESP_LOGI(TAG, "Radar applied: %ux%u (age=%ld s)",
             s_active_radar_dsc->header.w, s_active_radar_dsc->header.h, age);
}
