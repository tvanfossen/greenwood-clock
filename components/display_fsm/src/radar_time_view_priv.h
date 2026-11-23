/**
 * @file radar_time_view_priv.h
 * @version 1
 * @brief Radar time indicator internals: the view all looks share and the operations each look
 *        supplies. The base (radar_time_view.c) decides what to show and formats it; a look
 *        only draws it.
 */
#ifndef RADAR_TIME_VIEW_PRIV_H
#define RADAR_TIME_VIEW_PRIV_H

#include "radar_time_view.h"

#define RADAR_TIME_MAX_FRAMES 8

/** What a look draws. Text arrives formatted: @p prefix ("Radar", "Last updated:" or NULL for
 *  a status message), @p when ("6:01 PM", or the message) and @p age ("40m ago" or NULL). */
typedef struct {
    void (*build)(radar_time_view_t *tv, lv_obj_t *parent);
    void (*show)(radar_time_view_t *tv, const char *prefix, const char *when, const char *age);
    void (*position)(radar_time_view_t *tv, int32_t pos, uint32_t ms);   /* may be NULL */
    void (*ticks)(radar_time_view_t *tv, const int32_t *pos, int count); /* may be NULL */
    bool holds_clock;   /* the minimized clock shows the frame's time */
} radar_time_ops_t;

struct radar_time_view_t {
    const radar_time_ops_t *ops;
    lv_obj_t *root;     /* every object of the look lives under it */
    lv_obj_t *label;
    lv_obj_t *arc;
    lv_obj_t *dots[RADAR_TIME_MAX_FRAMES];
    int       ndots;
    bool      holding;  /* the clock shows a held frame time */
};

extern const radar_time_ops_t radar_time_chip_ops;
extern const radar_time_ops_t radar_time_dial_ops;

#endif /* RADAR_TIME_VIEW_PRIV_H */
