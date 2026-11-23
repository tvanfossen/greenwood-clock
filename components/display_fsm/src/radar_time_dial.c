/**
 * @file radar_time_dial.c
 * @version 1
 * @brief Radar time indicator, dial look: a ring left of the minimized clock. The ring fills
 *        from the oldest loop frame to the newest, a notch marks each frame, the knob (the
 *        hand) sweeps with the overlay crossfade and the frame's age sits in the middle. The
 *        clock itself shows the frame's time (radar_time_view.c holds it).
 */
#include "radar_time_view_priv.h"
#include "display_widgets.h"

#include <string.h>

#define DIAL_D        112                 /* ring diameter */
#define DIAL_W        6                   /* ring stroke */
#define DIAL_GAP      16                  /* space to the clock's left edge */
#define DIAL_TIME_CY  74                  /* minimized time's vertical centre below its area top */
#define DIAL_START    135                 /* ring start angle (deg, clockwise from 3 o'clock) */
#define DIAL_SWEEP    270
#define NOTCH_D       4
#define LONG_TEXT     10                  /* longer texts (status messages) use the small font */

/**
 * @brief lv_anim exec callback: set the ring's value.
 * @version 1
 * @callback
 */
static void dial_value_cb(void *arc, int32_t v)
{
    lv_arc_set_value((lv_obj_t *)arc, v);
}

/**
 * @brief Dial: the ring (track, filled part, knob) and the centre label, left of the clock.
 * @version 1
 * @dg_internal
 */
static void dial_build(radar_time_view_t *tv, lv_obj_t *parent)
{
    lv_area_t clock;
    clock_widget_min_area(&clock);
    tv->root = lv_obj_create(parent);
    lv_obj_remove_style_all(tv->root);
    lv_obj_set_size(tv->root, DIAL_D, DIAL_D);
    lv_obj_set_pos(tv->root, clock.x1 - DIAL_GAP - DIAL_D, clock.y1 + DIAL_TIME_CY - DIAL_D / 2);
    lv_obj_remove_flag(tv->root, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *arc = lv_arc_create(tv->root);
    tv->arc = arc;
    lv_obj_set_size(arc, DIAL_D, DIAL_D);
    lv_obj_center(arc);
    lv_arc_set_rotation(arc, DIAL_START);
    lv_arc_set_bg_angles(arc, 0, DIAL_SWEEP);
    lv_arc_set_range(arc, 0, RADAR_TIME_SPAN);
    lv_arc_set_value(arc, RADAR_TIME_SPAN);
    lv_obj_remove_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, DIAL_W, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_arc_opa(arc, LV_OPA_30, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, DIAL_W, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, lv_color_white(), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(arc, lv_color_white(), LV_PART_KNOB);
    lv_obj_set_style_pad_all(arc, 3, LV_PART_KNOB);
    lv_obj_set_style_shadow_width(arc, 0, LV_PART_KNOB);

    tv->label = lv_label_create(tv->root);
    lv_obj_set_width(tv->label, DIAL_D - 2 * DIAL_W - 8);
    lv_obj_set_style_text_align(tv->label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(tv->label, lv_color_white(), 0);
    lv_label_set_long_mode(tv->label, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_center(tv->label);
}

/**
 * @brief Dial: the age in the middle (the clock shows the time), or the status message.
 * @version 1
 * @dg_internal
 */
static void dial_show(radar_time_view_t *tv, const char *prefix, const char *when,
                      const char *age)
{
    const char *text = prefix ? age : when;
    bool long_text = strlen(text) > LONG_TEXT;
    lv_obj_set_style_text_font(tv->label, long_text ? &lv_font_montserrat_14
                                                    : &lv_font_montserrat_20, 0);
    lv_label_set_text(tv->label, text);
    lv_obj_center(tv->label);
}

/**
 * @brief Dial: sweep the ring's fill and knob to @p pos over @p ms (eased like the crossfade).
 * @version 1
 * @dg_internal
 */
static void dial_position(radar_time_view_t *tv, int32_t pos, uint32_t ms)
{
    lv_anim_delete(tv->arc, dial_value_cb);
    if (ms == 0) {
        lv_arc_set_value(tv->arc, pos);
        return;
    }
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, tv->arc);
    lv_anim_set_values(&a, lv_arc_get_value(tv->arc), pos);
    lv_anim_set_duration(&a, ms);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_set_exec_cb(&a, dial_value_cb);
    lv_anim_start(&a);
}

/**
 * @brief Dial: one dark notch on the ring per frame.
 * @version 1
 * @dg_internal
 */
static void dial_ticks(radar_time_view_t *tv, const int32_t *pos, int count)
{
    for (int i = 0; i < tv->ndots; i++) lv_obj_delete(tv->dots[i]);
    tv->ndots = 0;
    const int32_t r = DIAL_D / 2 - DIAL_W / 2;   // ring centre line
    for (int i = 0; i < count; i++) {
        int32_t deg = DIAL_START + DIAL_SWEEP * pos[i] / RADAR_TIME_SPAN;
        int32_t x = DIAL_D / 2 + r * lv_trigo_cos(deg) / LV_TRIGO_SIN_MAX - NOTCH_D / 2;
        int32_t y = DIAL_D / 2 + r * lv_trigo_sin(deg) / LV_TRIGO_SIN_MAX - NOTCH_D / 2;
        lv_obj_t *dot = lv_obj_create(tv->root);
        lv_obj_remove_style_all(dot);
        lv_obj_set_size(dot, NOTCH_D, NOTCH_D);
        lv_obj_set_pos(dot, x, y);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(dot, lv_color_hex(0x0a0a1e), 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_80, 0);
        tv->dots[tv->ndots++] = dot;
    }
    lv_obj_move_foreground(tv->label);
}

const radar_time_ops_t radar_time_dial_ops = {
    .build = dial_build,
    .show = dial_show,
    .position = dial_position,
    .ticks = dial_ticks,
    .holds_clock = true,
};
