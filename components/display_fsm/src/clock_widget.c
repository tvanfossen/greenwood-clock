// components/display_fsm/src/clock_widget.c
//
// Layer 1: ClockWidget — reusable time/date display with full/minimized modes.
// Extracted from components/ui/ui.c clock label management.
//
// Caller must hold LVGL lock for all functions.

#include "display_widgets.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "clock_widget";

LV_FONT_DECLARE(nunito_48);
LV_FONT_DECLARE(nunito_128);
LV_FONT_DECLARE(nunito_256);

// ---------------------------------------------------------------------------
// Internal struct
// ---------------------------------------------------------------------------

struct clock_widget_t {
    lv_obj_t    *container;     // invisible container for grouping
    lv_obj_t    *lbl_time;      // HH:MM
    lv_obj_t    *lbl_ampm;      // AM/PM
    lv_obj_t    *lbl_date;      // Day, Month DD
    clock_mode_t mode;
    lv_color_t   color;
};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static void format_time(const struct tm *ti, char *buf_time, size_t time_sz,
                        char *buf_ampm, size_t ampm_sz,
                        char *buf_date, size_t date_sz)
{
    int h12 = ti->tm_hour % 12;
    if (!h12) h12 = 12;
    snprintf(buf_time, time_sz, "%02d:%02d", h12, ti->tm_min);
    snprintf(buf_ampm, ampm_sz, "%s", ti->tm_hour < 12 ? "AM" : "PM");
    strftime(buf_date, date_sz, "%A, %B, %d", ti);
}

static void apply_full_mode(clock_widget_t *w)
{
    // Container: full width, tall, centered
    lv_obj_set_size(w->container, 1008, 350);
    lv_obj_align(w->container, LV_ALIGN_TOP_MID, 0, 8);

    // Time: 256pt, centered in container
    lv_obj_set_style_text_font(w->lbl_time, &nunito_256, 0);
    lv_obj_align(w->lbl_time, LV_ALIGN_TOP_MID, 0, 24);

    // AM/PM: 48pt, right of time
    lv_obj_set_style_text_font(w->lbl_ampm, &nunito_48, 0);
    lv_obj_align_to(w->lbl_ampm, w->lbl_time, LV_ALIGN_OUT_RIGHT_MID, 160, 0);

    // Date: 48pt, bottom center
    lv_obj_set_style_text_font(w->lbl_date, &nunito_48, 0);
    lv_obj_align(w->lbl_date, LV_ALIGN_BOTTOM_MID, 0, -16);

    lv_obj_clear_flag(w->container, LV_OBJ_FLAG_HIDDEN);
}

static void apply_minimized_mode(clock_widget_t *w)
{
    // Container: smaller, top-right corner
    lv_obj_set_size(w->container, 300, 180);
    lv_obj_align(w->container, LV_ALIGN_TOP_RIGHT, -16, 8);

    // Time: 128pt
    lv_obj_set_style_text_font(w->lbl_time, &nunito_128, 0);
    lv_obj_align(w->lbl_time, LV_ALIGN_TOP_MID, 0, 0);

    // AM/PM: 48pt, right of time
    lv_obj_set_style_text_font(w->lbl_ampm, &nunito_48, 0);
    lv_obj_align_to(w->lbl_ampm, w->lbl_time, LV_ALIGN_OUT_RIGHT_MID, 8, 0);

    // Date: 48pt, below time but smaller
    lv_obj_set_style_text_font(w->lbl_date, &lv_font_montserrat_24, 0);
    lv_obj_align(w->lbl_date, LV_ALIGN_BOTTOM_MID, 0, -4);

    lv_obj_clear_flag(w->container, LV_OBJ_FLAG_HIDDEN);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

clock_widget_t *clock_widget_create(lv_obj_t *parent)
{
    clock_widget_t *w = lv_malloc(sizeof(clock_widget_t));
    if (!w) {
        ESP_LOGE(TAG, "Failed to allocate clock_widget_t");
        return NULL;
    }
    memset(w, 0, sizeof(*w));
    w->color = lv_color_white();
    w->mode  = CLOCK_MODE_FULL;

    // Transparent container — groups labels for easy repositioning
    w->container = lv_obj_create(parent);
    lv_obj_set_style_bg_opa(w->container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(w->container, 0, 0);
    lv_obj_set_style_pad_all(w->container, 8, 0);
    lv_obj_clear_flag(w->container, LV_OBJ_FLAG_SCROLLABLE);

    // Time label
    w->lbl_time = lv_label_create(w->container);
    lv_obj_set_style_text_color(w->lbl_time, w->color, 0);
    lv_obj_set_style_text_align(w->lbl_time, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(w->lbl_time, "12:00");

    // AM/PM label
    w->lbl_ampm = lv_label_create(w->container);
    lv_obj_set_style_text_color(w->lbl_ampm, w->color, 0);
    lv_label_set_text(w->lbl_ampm, "PM");

    // Date label
    w->lbl_date = lv_label_create(w->container);
    lv_obj_set_style_text_color(w->lbl_date, w->color, 0);
    lv_obj_set_style_text_align(w->lbl_date, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(w->lbl_date, "");

    apply_full_mode(w);
    ESP_LOGI(TAG, "ClockWidget created");
    return w;
}

void clock_widget_destroy(clock_widget_t *w)
{
    if (!w) return;
    if (w->container) {
        lv_obj_del(w->container);
    }
    lv_free(w);
    ESP_LOGI(TAG, "ClockWidget destroyed");
}

void clock_widget_update(clock_widget_t *w, const struct tm *ti)
{
    if (!w || !ti) return;

    char buf_time[6], buf_ampm[3], buf_date[30];
    format_time(ti, buf_time, sizeof(buf_time),
                buf_ampm, sizeof(buf_ampm),
                buf_date, sizeof(buf_date));

    lv_label_set_text(w->lbl_time, buf_time);
    lv_label_set_text(w->lbl_ampm, buf_ampm);
    lv_label_set_text(w->lbl_date, buf_date);
}

static void anim_x_cb(void *obj, int32_t v) { lv_obj_set_x((lv_obj_t *)obj, v); }
static void anim_y_cb(void *obj, int32_t v) { lv_obj_set_y((lv_obj_t *)obj, v); }
static void anim_w_cb(void *obj, int32_t v) { lv_obj_set_width((lv_obj_t *)obj, v); }
static void anim_h_cb(void *obj, int32_t v) { lv_obj_set_height((lv_obj_t *)obj, v); }

static void animate_container(lv_obj_t *cont,
                               int32_t x0, int32_t y0, int32_t w0, int32_t h0,
                               int32_t x1, int32_t y1, int32_t w1, int32_t h1)
{
    lv_anim_t a;
    uint32_t dur = 250;

    lv_anim_init(&a);
    lv_anim_set_var(&a, cont);
    lv_anim_set_duration(&a, dur);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);

    lv_anim_set_values(&a, x0, x1);
    lv_anim_set_exec_cb(&a, anim_x_cb);
    lv_anim_start(&a);

    lv_anim_set_values(&a, y0, y1);
    lv_anim_set_exec_cb(&a, anim_y_cb);
    lv_anim_start(&a);

    lv_anim_set_values(&a, w0, w1);
    lv_anim_set_exec_cb(&a, anim_w_cb);
    lv_anim_start(&a);

    lv_anim_set_values(&a, h0, h1);
    lv_anim_set_exec_cb(&a, anim_h_cb);
    lv_anim_start(&a);
}

// Full mode: container 1008×350 at top-center (x ≈ 8, y = 8)
// Minimized: container 300×180 at top-right (x ≈ 708, y = 8)
#define FULL_X  8
#define FULL_Y  8
#define FULL_W  1008
#define FULL_H  350
#define MIN_X   708
#define MIN_Y   8
#define MIN_W   300
#define MIN_H   180

void clock_widget_set_mode(clock_widget_t *w, clock_mode_t mode)
{
    if (!w || w->mode == mode) return;

    // Apply font/layout for target mode immediately (fonts can't be animated)
    if (mode == CLOCK_MODE_FULL) {
        apply_full_mode(w);
        // Animate container from minimized position to full position
        animate_container(w->container, MIN_X, MIN_Y, MIN_W, MIN_H,
                                         FULL_X, FULL_Y, FULL_W, FULL_H);
    } else {
        apply_minimized_mode(w);
        // Animate container from full position to minimized position
        animate_container(w->container, FULL_X, FULL_Y, FULL_W, FULL_H,
                                         MIN_X, MIN_Y, MIN_W, MIN_H);
    }

    w->mode = mode;
    ESP_LOGI(TAG, "ClockWidget mode → %s (animated)", mode == CLOCK_MODE_FULL ? "FULL" : "MINIMIZED");
}

void clock_widget_set_color(clock_widget_t *w, lv_color_t color)
{
    if (!w) return;
    w->color = color;
    lv_obj_set_style_text_color(w->lbl_time, color, 0);
    lv_obj_set_style_text_color(w->lbl_ampm, color, 0);
    lv_obj_set_style_text_color(w->lbl_date, color, 0);
}

clock_mode_t clock_widget_get_mode(const clock_widget_t *w)
{
    return w ? w->mode : CLOCK_MODE_FULL;
}
