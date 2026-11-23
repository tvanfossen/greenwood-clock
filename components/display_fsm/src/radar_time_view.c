/**
 * @file radar_time_view.c
 * @version 1
 * @brief Radar time indicator base: chooses what to show (status, single image, loop frame),
 *        formats it, holds and releases the minimized clock for looks that put the frame time
 *        on it; plus the chip look.
 */
#include "radar_time_view_priv.h"
#include "display_fsm.h"
#include "display_widgets.h"

#include <stdio.h>
#include <string.h>
#include "esp_log.h"

static const char *TAG = "radar_time";

/**
 * @brief Format @p ts as "h:mm AM" and its age as "now" / "40m ago" (one line in the dial).
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
 * @brief Create the indicator in @p parent (the radar view's container).
 * @version 1
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
    tv->ops = (style == RADAR_TIME_DIAL) ? &radar_time_dial_ops : &radar_time_chip_ops;
    tv->ops->build(tv, parent);
    ESP_LOGI(TAG, "created (%s)", style == RADAR_TIME_DIAL ? "dial" : "chip");
    return tv;
}

/**
 * @brief Delete the indicator (and release the clock if it held a frame time). NULL-safe.
 * @version 1
 * @req REQ-RADAR-003
 */
void radar_time_view_destroy(radar_time_view_t *tv)
{
    if (!tv) return;
    release_clock(tv);
    if (tv->root) lv_obj_delete(tv->root);
    lv_free(tv);
}

/**
 * @brief Show a status line instead of a time ("Radar loading...", "Radar data expired").
 * @version 1
 * @req REQ-RADAR-003
 */
void radar_time_view_message(radar_time_view_t *tv, const char *text)
{
    if (!tv) return;
    release_clock(tv);
    tv->ops->show(tv, NULL, text, NULL);
}

/**
 * @brief Show a single radar image (no loop) taken at @p ts.
 * @version 1
 * @req REQ-RADAR-003
 */
void radar_time_view_latest(radar_time_view_t *tv, time_t ts)
{
    if (!tv) return;
    struct tm ti;
    char when[16], age[24];
    format_when(ts, &ti, when, sizeof(when), age, sizeof(age));
    release_clock(tv);
    tv->ops->show(tv, "Last updated:", when, age);
    if (tv->ops->position) tv->ops->position(tv, RADAR_TIME_SPAN, 0);
}

/**
 * @brief Set the loop's frames: their positions (0..RADAR_TIME_SPAN, oldest first).
 * @version 1
 * @req REQ-RADAR-003
 */
void radar_time_view_frames(radar_time_view_t *tv, const int32_t *pos, int count)
{
    if (tv && tv->ops->ticks) tv->ops->ticks(tv, pos, LV_MIN(count, RADAR_TIME_MAX_FRAMES));
}

/**
 * @brief Move to the loop frame taken at @p ts, at position @p pos, over @p fade_ms (the
 *        overlay crossfade's duration). @p newest is the most recent frame: a look that holds
 *        the clock lets it show the live time there.
 * @version 1
 * @req REQ-RADAR-003
 */
void radar_time_view_frame(radar_time_view_t *tv, time_t ts, int32_t pos, uint32_t fade_ms,
                           bool newest)
{
    if (!tv) return;
    struct tm ti;
    char when[16], age[24];
    format_when(ts, &ti, when, sizeof(when), age, sizeof(age));
    if (tv->ops->holds_clock && !newest) {
        clock_widget_hold(display_fsm_get_clock(), &ti);
        tv->holding = true;
    } else {
        release_clock(tv);
    }
    tv->ops->show(tv, "Radar", when, age);
    if (tv->ops->position) tv->ops->position(tv, pos, fade_ms);
}

/* ---- Chip look: one text pill in the bottom-left corner ---- */

/**
 * @brief Chip: a dark translucent pill (legible over any map or precipitation colour).
 * @version 1
 * @dg_internal
 */
static void chip_build(radar_time_view_t *tv, lv_obj_t *parent)
{
    tv->root = lv_label_create(parent);
    tv->label = tv->root;
    lv_obj_set_style_text_font(tv->label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(tv->label, lv_color_hex(0xe0e0e0), 0);
    lv_obj_set_style_bg_color(tv->label, lv_color_hex(0x0a0a1e), 0);
    lv_obj_set_style_bg_opa(tv->label, LV_OPA_70, 0);
    lv_obj_set_style_pad_hor(tv->label, 8, 0);
    lv_obj_set_style_pad_ver(tv->label, 4, 0);
    lv_obj_set_style_radius(tv->label, 6, 0);
    lv_obj_align(tv->label, LV_ALIGN_BOTTOM_LEFT, 16, -22);
}

/**
 * @brief Chip: "<prefix> <when> (<age>)", or the bare message.
 * @version 1
 * @dg_internal
 */
static void chip_show(radar_time_view_t *tv, const char *prefix, const char *when,
                      const char *age)
{
    if (!prefix) lv_label_set_text(tv->label, when);
    else lv_label_set_text_fmt(tv->label, "%s %s (%s)", prefix, when, age);
}

const radar_time_ops_t radar_time_chip_ops = {
    .build = chip_build,
    .show = chip_show,
    .position = NULL,
    .ticks = NULL,
    .holds_clock = false,
};
