// components/display_fsm/src/radar_view.c
//
// Layer 1: RadarView — map background + radar overlay + home marker.
// Caller must hold LVGL lock for all functions.

#include "display_widgets.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include <string.h>
#include <stdio.h>
#include <math.h>

static const char *TAG = "radar_view";

#define SCREEN_W  1024
#define SCREEN_H  600
#define RADAR_BBOX_DEG 2.0f  // +/- degrees from center (matches nws_radar.c)
#define HOME_MARKER_SIZE 10

struct radar_view_t {
    lv_obj_t *container;
    lv_obj_t *img_map;       // static map background from SD card
    lv_obj_t *img_radar;     // transparent radar overlay PNG
    lv_obj_t *home_marker;   // crosshair/dot at home lat/lon
    lv_obj_t *lbl_updated;   // "Last updated: HH:MM"
    float     center_lat;
    float     center_lon;
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

    // Map background image from SD card (optional — dark background if missing)
    rv->img_map = lv_image_create(rv->container);
    lv_image_set_src(rv->img_map, "A:/maps/local.png");
    lv_obj_align(rv->img_map, LV_ALIGN_TOP_LEFT, 0, 0);

    // Radar overlay image (initially empty — set via radar_view_set_radar)
    rv->img_radar = lv_image_create(rv->container);
    lv_obj_align(rv->img_radar, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_image_opa(rv->img_radar, LV_OPA_80, 0);

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
    lv_anim_set_exec_cb(&a, (lv_anim_exec_xcb_t)lv_obj_set_style_opa);
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
    if (rv->home_marker) lv_anim_del(rv->home_marker, NULL);
    if (rv->container) lv_obj_del(rv->container);
    lv_free(rv);
    ESP_LOGI(TAG, "RadarView destroyed");
}

lv_obj_t *radar_view_container(const radar_view_t *rv)
{
    return rv ? rv->container : NULL;
}

void radar_view_set_radar(radar_view_t *rv, const uint8_t *png_data, size_t len)
{
    if (!rv || !png_data || len == 0) return;

    // Create an LVGL image descriptor for the PNG data
    // LVGL's PNG decoder handles raw PNG buffers via lv_image_set_src with a data pointer
    lv_image_dsc_t *dsc = lv_malloc(sizeof(lv_image_dsc_t));
    if (!dsc) {
        ESP_LOGE(TAG, "Failed to allocate image descriptor");
        return;
    }
    memset(dsc, 0, sizeof(*dsc));
    dsc->header.magic = LV_IMAGE_HEADER_MAGIC;
    dsc->header.cf    = LV_COLOR_FORMAT_RAW;
    dsc->header.w     = SCREEN_W;
    dsc->header.h     = SCREEN_H;
    dsc->data_size    = len;
    dsc->data         = png_data;

    lv_image_set_src(rv->img_radar, dsc);

    // Update timestamp label
    time_t now;
    time(&now);
    struct tm ti;
    localtime_r(&now, &ti);
    char buf[48];
    snprintf(buf, sizeof(buf), "Last updated: %d:%02d %s",
             ti.tm_hour % 12 == 0 ? 12 : ti.tm_hour % 12,
             ti.tm_min,
             ti.tm_hour >= 12 ? "PM" : "AM");
    lv_label_set_text(rv->lbl_updated, buf);

    ESP_LOGI(TAG, "Radar overlay updated (%zu bytes)", len);
}
