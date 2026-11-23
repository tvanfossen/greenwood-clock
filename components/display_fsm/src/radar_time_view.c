/**
 * @file radar_time_view.c
 * @version 2
 * @brief Radar time indicator: a text chip in the bottom-left corner, and for the clock look
 *        the minimized clock held at each loop frame's time.
 */
#include "radar_time_view.h"
#include "display_fsm.h"
#include "display_widgets.h"

#include <stdio.h>
#include "esp_log.h"

static const char *TAG = "radar_time";

struct radar_time_view_t {
    lv_obj_t *chip;
    bool      on_clock;   /* loop frame times go to the clock, not the chip */
    bool      holding;    /* the clock shows a held frame time */
};

/**
 * @brief Format @p ts as "h:mm AM" and its age as "now" / "40m ago".
 * @version 1
 * @dg_internal
 */
static void format_when(time_t ts, struct tm *ti, char *when, size_t when_sz, char *age,
                        size_t age_sz)
{
    time_t now;
    time(&now);
    localtime_r(&ts, ti);
    snprintf(when, when_sz, "%d:%02d %s", ti->tm_hour % 12 == 0 ? 12 : ti->tm_hour % 12,
             ti->tm_min, ti->tm_hour >= 12 ? "PM" : "AM");
    long min = (long)(now - ts) / 60;   // minutes only: the history spans at most 70
    if (min < 2) snprintf(age, age_sz, "now");
    else snprintf(age, age_sz, "%ldm ago", min);
}

/**
 * @brief Show the live time on the clock again if this view held it.
 * @version 1
 * @dg_internal
 */
static void release_clock(radar_time_view_t *tv)
{
    if (!tv->holding) return;
    clock_widget_release(display_fsm_get_clock());
    tv->holding = false;
}

/**
 * @brief Put @p text on the chip and show it.
 * @version 1
 * @dg_internal
 */
static void chip_text(radar_time_view_t *tv, const char *text)
{
    lv_label_set_text(tv->chip, text);
    lv_obj_remove_flag(tv->chip, LV_OBJ_FLAG_HIDDEN);
}

/**
 * @brief Create the indicator in @p parent (the radar view's container).
 * v2: one chip for both looks; the clock look also holds the clock (the dial is gone).
 * @version 2
 * @req REQ-RADAR-003
 * @return The view, or NULL if allocation failed.
 */
radar_time_view_t *radar_time_view_create(lv_obj_t *parent, radar_time_style_t style)
{
    radar_time_view_t *tv = lv_malloc_zeroed(sizeof(*tv));
    if (!tv) {
        ESP_LOGE(TAG, "alloc failed");
        return NULL;
    }
    tv->on_clock = (style == RADAR_TIME_CLOCK);
    tv->chip = lv_label_create(parent);   // dark translucent pill: legible over any map
    lv_obj_set_style_text_font(tv->chip, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(tv->chip, lv_color_hex(0xe0e0e0), 0);
    lv_obj_set_style_bg_color(tv->chip, lv_color_hex(0x0a0a1e), 0);
    lv_obj_set_style_bg_opa(tv->chip, LV_OPA_70, 0);
    lv_obj_set_style_pad_hor(tv->chip, 8, 0);
    lv_obj_set_style_pad_ver(tv->chip, 4, 0);
    lv_obj_set_style_radius(tv->chip, 6, 0);
    lv_obj_align(tv->chip, LV_ALIGN_BOTTOM_LEFT, 16, -22);
    ESP_LOGI(TAG, "created (%s)", tv->on_clock ? "clock" : "chip");
    return tv;
}

/**
 * @brief Delete the indicator (and release the clock if it held a frame time). NULL-safe.
 * v2: deletes the chip (no look root).
 * @version 2
 * @req REQ-RADAR-003
 */
void radar_time_view_destroy(radar_time_view_t *tv)
{
    if (!tv) return;
    release_clock(tv);
    lv_obj_delete(tv->chip);
    lv_free(tv);
}

/**
 * @brief Show a status line instead of a time ("Radar loading...", "Radar data expired").
 * v2: on the chip directly (no look ops).
 * @version 2
 * @req REQ-RADAR-003
 */
void radar_time_view_message(radar_time_view_t *tv, const char *text)
{
    if (!tv) return;
    release_clock(tv);
    chip_text(tv, text);
}

/**
 * @brief Show a single radar image (no loop) taken at @p ts.
 * v2: on the chip directly (no look ops).
 * @version 2
 * @req REQ-RADAR-003
 */
void radar_time_view_latest(radar_time_view_t *tv, time_t ts)
{
    if (!tv) return;
    struct tm ti;
    char when[16], age[24], text[64];
    format_when(ts, &ti, when, sizeof(when), age, sizeof(age));
    release_clock(tv);
    snprintf(text, sizeof(text), "Last updated: %s (%s)", when, age);
    chip_text(tv, text);
}

/**
 * @brief Show the loop frame taken at @p ts. The clock look holds the clock at that time
 *        (the live time on the @p newest frame) and hides the chip; the chip look writes it.
 * v2: no position (the progress bar shows it); the clock look hides the chip.
 * @version 2
 * @req REQ-RADAR-003
 */
void radar_time_view_frame(radar_time_view_t *tv, time_t ts, bool newest)
{
    if (!tv) return;
    struct tm ti;
    char when[16], age[24], text[64];
    format_when(ts, &ti, when, sizeof(when), age, sizeof(age));
    if (!tv->on_clock) {
        snprintf(text, sizeof(text), "Radar %s (%s)", when, age);
        chip_text(tv, text);
        return;
    }
    lv_obj_add_flag(tv->chip, LV_OBJ_FLAG_HIDDEN);
    if (newest) {
        release_clock(tv);
    } else {
        clock_widget_hold(display_fsm_get_clock(), &ti);
        tv->holding = true;
    }
}
