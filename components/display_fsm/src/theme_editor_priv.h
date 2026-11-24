/**
 * @file theme_editor_priv.h
 * @version 1
 * @brief Theme editor internals shared by its parts: OKLCH slider tracks
 *        (theme_editor_tracks.c) and the design preview (theme_editor_preview.c).
 */
#pragma once

#include "lvgl.h"
#include "ui_theme.h"

#ifdef __cplusplus
extern "C" {
#endif

/** One OKLCH channel of a core colour. */
typedef enum { TE_L = 0, TE_C, TE_H, TE_CHANNELS } te_channel_t;

/** Slider units per channel (L and C in thousandths, h in degrees). */
#define TE_L_MAX 1000
#define TE_C_MAX 370
#define TE_H_MAX 359

/** A slider over a canvas painted with the channel's OKLCH gradient. */
typedef struct {
    lv_obj_t     *canvas;
    lv_obj_t     *slider;
    lv_obj_t     *label;
    te_channel_t  channel;
} te_track_t;

/**
 * @brief Build the track for @p channel at @p y in @p parent; @p cb gets the slider's
 *        LV_EVENT_VALUE_CHANGED.
 * @version 1
 * @req REQ-UI-004
 */
void te_track_create(te_track_t *t, lv_obj_t *parent, te_channel_t channel, int32_t y,
                     lv_event_cb_t cb);

/**
 * @brief Repaint @p t's gradient around @p core (the other two channels held), move its
 *        slider to @p core's value and relabel it.
 * @version 1
 * @req REQ-UI-004
 */
void te_track_show(te_track_t *t, ui_oklch_t core);

/**
 * @brief @p core with @p t's channel set from its slider.
 * @version 1
 * @req REQ-UI-004
 * @return The updated colour.
 */
ui_oklch_t te_track_apply(const te_track_t *t, ui_oklch_t core);

/**
 * @brief Build the design preview in @p pane: clock text, a card, a strip, the four alert
 *        severities and stock controls, every colour bound (so it follows each edit).
 * @version 1
 * @req REQ-UI-004
 */
void te_preview_create(lv_obj_t *pane);

#ifdef __cplusplus
}
#endif
