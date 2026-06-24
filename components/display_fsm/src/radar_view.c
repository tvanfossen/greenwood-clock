// components/display_fsm/src/radar_view.c
//
// Layer 1: RadarView — map background + radar overlay + home marker.
// Caller must hold LVGL lock for all functions.

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
static void pulse_opa_cb(void *obj, int32_t v)
{
    lv_obj_set_style_opa((lv_obj_t *)obj, (lv_opa_t)v, 0);
}

#define SCREEN_W  1024
#define SCREEN_H  600
#define RADAR_BBOX_DEG 2.0f  // +/- degrees from center (matches nws_radar.c)
#define HOME_MARKER_SIZE 10

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
    lv_obj_t       *home_marker;     // crosshair/dot at home lat/lon
    lv_obj_t       *lbl_updated;     // "Last updated: HH:MM"
    float           center_lat;
    float           center_lon;
};

// Convert lat/lon to pixel position within the radar view.
// The radar image covers center +/- RADAR_BBOX_DEG in both axes.
static void latlon_to_pixel(float lat, float lon, float center_lat, float center_lon,
                            int *px_x, int *px_y)
{
    float x_frac = (lon - (center_lon - RADAR_BBOX_DEG)) / (2.0f * RADAR_BBOX_DEG);
    float y_frac = ((center_lat + RADAR_BBOX_DEG) - lat) / (2.0f * RADAR_BBOX_DEG);

    *px_x = (int)(x_frac * SCREEN_W);
    *px_y = (int)(y_frac * SCREEN_H);
}

// Parse + validate the 8-byte map header. Sets mw/mh/file_stride on success.
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
// path (upright). Only the full-screen background uses the flipping PPA path.
// Verified with a colour-coded orientation test card.
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
    mdsc->header.cf     = LV_COLOR_FORMAT_ARGB8888;
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
static void load_map_dsc(void)
{
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
    lv_obj_set_style_bg_color(rv->container, lv_color_hex(0x0a0a1e), 0);
    lv_obj_set_style_bg_opa(rv->container, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(rv->container, 0, 0);
    lv_obj_set_style_pad_all(rv->container, 0, 0);
    lv_obj_set_style_radius(rv->container, 0, 0);
    lv_obj_clear_flag(rv->container, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    // Map background — pre-rendered raw BGRA binary (bypasses lodepng), loaded
    // once into the module-level cache.
    rv->img_map = NULL;
    if (!s_map_dsc) {
        load_map_dsc();
    } else {
        ESP_LOGI(TAG, "Map using cached data");
    }
    if (s_map_dsc) {
        rv->img_map = lv_image_create(rv->container);
        lv_image_set_src(rv->img_map, s_map_dsc);
        lv_obj_align(rv->img_map, LV_ALIGN_TOP_LEFT, 0, 0);
    }

    // Radar overlay image (initially empty — set via radar_view_apply_radar)
    rv->img_radar = lv_image_create(rv->container);
    lv_obj_align(rv->img_radar, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_image_opa(rv->img_radar, LV_OPA_40, 0);

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
    lv_obj_set_style_text_color(rv->lbl_updated, lv_color_hex(0x808080), 0);
    lv_obj_align(rv->lbl_updated, LV_ALIGN_BOTTOM_LEFT, 16, -12);
    lv_label_set_text(rv->lbl_updated, "Radar loading...");

    ESP_LOGI(TAG, "RadarView created (center=%.3f,%.3f)", lat, lon);
    return rv;
}

void radar_view_destroy(radar_view_t *rv)
{
    if (!rv) return;
    if (rv->home_marker) lv_anim_delete(rv->home_marker, NULL);
    if (rv->container) {
        lv_anim_delete(rv->container, NULL);
        lv_obj_delete(rv->container);
    }
    // Map and radar data are module-level caches — not freed here
    lv_free(rv);
    ESP_LOGI(TAG, "RadarView destroyed");
}

lv_obj_t *radar_view_container(const radar_view_t *rv)
{
    return rv ? rv->container : NULL;
}

void radar_view_predecode(const uint8_t *png_data, size_t len)
{
    if (!png_data || len == 0) return;

    // Decode into a persistent SPIRAM ARGB8888 descriptor (shared helper).
    lv_image_dsc_t *dsc = image_decode_png_buf(png_data, len);
    if (!dsc) {
        ESP_LOGE(TAG, "Radar predecode failed (len=%zu)", len);
        return;
    }

    // stb-decoded images render vertically flipped on this device (same as the
    // clock background); the raw-fread map does NOT. So flip the overlay to put
    // north at the top, aligning it with the (unflipped, north-up) basemap.
    // Verified against a fresh basemap+NWS radar composite for the configured
    // bbox: heavy precip belongs lower/south, not upper/north.
    image_decode_flip_vertical(dsc);

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

void radar_view_apply_radar(radar_view_t *rv)
{
    if (!rv) return;

    // Swap pending → active under LVGL lock (safe: render thread won't read freed memory)
    if (s_pending_radar_dsc) {
        // Detach old active from LVGL before freeing
        if (s_active_radar_dsc) {
            lv_image_set_src(rv->img_radar, NULL);
        }
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
        lv_image_set_src(rv->img_radar, NULL);
        heap_caps_free(s_active_radar_pixels); s_active_radar_pixels = NULL;
        heap_caps_free(s_active_radar_dsc);    s_active_radar_dsc = NULL;
        lv_label_set_text(rv->lbl_updated, "Radar data expired");
        return;
    }

    lv_image_set_src(rv->img_radar, s_active_radar_dsc);

    // Update timestamp label with age
    struct tm ti;
    localtime_r(&s_active_radar_ts, &ti);
    char buf[48];
    snprintf(buf, sizeof(buf), "Last updated: %d:%02d %s (%ldm ago)",
             ti.tm_hour % 12 == 0 ? 12 : ti.tm_hour % 12,
             ti.tm_min,
             ti.tm_hour >= 12 ? "PM" : "AM",
             age / 60);
    lv_label_set_text(rv->lbl_updated, buf);

    ESP_LOGI(TAG, "Radar applied: %ux%u (age=%ld s)",
             s_active_radar_dsc->header.w, s_active_radar_dsc->header.h, age);
}
