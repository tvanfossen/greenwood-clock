/**
 * @file theme_editor_tracks.c
 * @version 1
 * @brief Theme editor sliders whose tracks show the channel's OKLCH gradient: each column is
 *        the core colour with that one channel swept, so the track shows what dragging does.
 */

#include "theme_editor_priv.h"
#include "ui_theme_bind.h"

#include <stdio.h>

#define TRACK_W 330
#define TRACK_H 22

static const char *const CHANNEL_NAMES[TE_CHANNELS] = { "L", "C", "h" };
static const int32_t     CHANNEL_MAX[TE_CHANNELS] = { TE_L_MAX, TE_C_MAX, TE_H_MAX };

/**
 * @brief @p core's @p ch channel in slider units.
 * @version 1
 * @utility
 * @return The slider value.
 */
static int32_t to_units(ui_oklch_t core, te_channel_t ch)
{
    float v = ch == TE_L ? core.l * 1000.0f : (ch == TE_C ? core.c * 1000.0f : core.h);
    return (int32_t)(v + 0.5f);
}

/**
 * @brief @p core with @p ch set to @p units slider units.
 * @version 1
 * @utility
 * @return The colour.
 */
static ui_oklch_t with_units(ui_oklch_t core, te_channel_t ch, int32_t units)
{
    if (ch == TE_L) core.l = (float)units / 1000.0f;
    else if (ch == TE_C) core.c = (float)units / 1000.0f;
    else core.h = (float)units;
    return core;
}

/**
 * @brief Free a track canvas's pixel buffer with the canvas.
 * @version 1
 * @callback
 */
static void canvas_delete_cb(lv_event_t *e)
{
    lv_draw_buf_t *buf = lv_canvas_get_draw_buf(lv_event_get_target(e));
    if (buf) lv_draw_buf_destroy(buf);
}

/**
 * @brief Build the track for @p channel at @p y in @p parent.
 * @version 1
 * @req REQ-UI-004
 */
void te_track_create(te_track_t *t, lv_obj_t *parent, te_channel_t channel, int32_t y,
                     lv_event_cb_t cb)
{
    t->channel = channel;
    t->label = lv_label_create(parent);
    lv_obj_set_pos(t->label, 0, y);
    lv_obj_set_width(t->label, 96);
    lv_obj_set_style_text_font(t->label, &lv_font_montserrat_20, 0);
    ui_bind_text(t->label, UI_ROLE_TEXT);

    t->canvas = lv_canvas_create(parent);
    lv_draw_buf_t *buf = lv_draw_buf_create(TRACK_W, TRACK_H, LV_COLOR_FORMAT_RGB565, 0);
    lv_canvas_set_draw_buf(t->canvas, buf);
    lv_obj_add_event_cb(t->canvas, canvas_delete_cb, LV_EVENT_DELETE, NULL);
    lv_obj_set_pos(t->canvas, 110, y + 2);
    lv_obj_set_style_radius(t->canvas, TRACK_H / 2, 0);
    lv_obj_set_style_clip_corner(t->canvas, true, 0);

    t->slider = lv_slider_create(parent);
    lv_obj_set_size(t->slider, TRACK_W, TRACK_H);
    lv_obj_set_pos(t->slider, 110, y + 2);
    lv_slider_set_range(t->slider, 0, CHANNEL_MAX[channel]);
    lv_obj_set_style_bg_opa(t->slider, LV_OPA_TRANSP, LV_PART_MAIN);        // the canvas is the track
    lv_obj_set_style_bg_opa(t->slider, LV_OPA_TRANSP, LV_PART_INDICATOR);
    lv_obj_add_event_cb(t->slider, cb, LV_EVENT_VALUE_CHANGED, t);
}

/**
 * @brief Repaint @p t's gradient around @p core, move its slider and relabel it.
 * @version 1
 * @req REQ-UI-004
 */
void te_track_show(te_track_t *t, ui_oklch_t core)
{
    lv_draw_buf_t *buf = lv_canvas_get_draw_buf(t->canvas);
    for (int32_t x = 0; x < TRACK_W; x++) {
        int32_t units = (int32_t)((int64_t)x * CHANNEL_MAX[t->channel] / (TRACK_W - 1));
        uint16_t px = lv_color_to_u16(ui_oklch_to_color(with_units(core, t->channel, units)));
        for (int32_t y = 0; y < TRACK_H; y++) {
            ((uint16_t *)(buf->data + (size_t)y * buf->header.stride))[x] = px;
        }
    }
    lv_obj_invalidate(t->canvas);
    int32_t v = to_units(core, t->channel);
    lv_slider_set_value(t->slider, v, LV_ANIM_OFF);
    char text[24];
    if (t->channel == TE_H) snprintf(text, sizeof(text), "%s %ld", CHANNEL_NAMES[t->channel], (long)v);
    else snprintf(text, sizeof(text), "%s %.3f", CHANNEL_NAMES[t->channel], (double)v / 1000.0);
    lv_label_set_text(t->label, text);
}

/**
 * @brief @p core with @p t's channel set from its slider.
 * @version 1
 * @req REQ-UI-004
 * @return The updated colour.
 */
ui_oklch_t te_track_apply(const te_track_t *t, ui_oklch_t core)
{
    return with_units(core, t->channel, lv_slider_get_value(t->slider));
}

