/**
 * @file sky_body.c
 * v2: reports what painting costs (LV_LOG_USER).
 * v3: the sky's events layer.
 * @version 3
 * @brief The sun and the moon as LVGL canvases (see sky_body.h).
 */
#include "sky_body.h"
#include "sky_paint.h"
#include "ui_theme.h"

#include <string.h>

#define SUN_FRAME_MS 100    // 10 frames a second
#define SUN_LOOP_MS  8000   // the rays turn one spacing, the glow breathes once

/** An animated sun: its tables, the frame it paints, its timer. */
typedef struct {
    sky_sun_t       sun;
    void           *tables;
    uint32_t       *frame;
    lv_draw_buf_t  *buf;
    lv_timer_t     *timer;
    lv_obj_t       *canvas;
    uint32_t        ms;
    uint32_t        frames, paint_ms;   /**< Since the last report (every 100 frames). */
} sun_anim_t;

/**
 * @brief A theme colour as the painter's.
 * @version 1
 * @utility
 * @return It.
 */
static sky_rgb_t rgb(lv_color_t c)
{
    return (sky_rgb_t){ c.red, c.green, c.blue };
}

/**
 * @brief Copy @p px (contiguous) into @p buf (whatever its row stride).
 * @version 1
 * @utility
 */
static void blit(lv_draw_buf_t *buf, const uint32_t *px, int32_t d)
{
    for (int32_t y = 0; y < d; y++) {
        memcpy(buf->data + (size_t)y * buf->header.stride, px + (size_t)y * (size_t)d,
               (size_t)d * sizeof(uint32_t));
    }
}

/**
 * @brief A canvas over a fresh ARGB8888 buffer of @p d px.
 * @version 1
 * @utility
 * @return The canvas, or NULL; its buffer in @p out.
 */
static lv_obj_t *canvas_for(lv_obj_t *parent, int32_t d, lv_draw_buf_t **out)
{
    *out = lv_draw_buf_create(d, d, LV_COLOR_FORMAT_ARGB8888, 0);
    if (!*out) return NULL;
    lv_obj_t *cv = lv_canvas_create(parent);
    lv_canvas_set_draw_buf(cv, *out);
    lv_obj_remove_flag(cv, LV_OBJ_FLAG_CLICKABLE);
    return cv;
}

/**
 * @brief The canvas is going: free its buffer.
 * @version 1
 * @callback
 */
static void free_buf_cb(lv_event_t *e)
{
    lv_draw_buf_destroy((lv_draw_buf_t *)lv_event_get_user_data(e));
}

/**
 * @brief One sun frame.
 * v2: its paint time reported every 100 frames.
 * @version 2
 * @callback
 */
static void sun_tick_cb(lv_timer_t *t)
{
    sun_anim_t *a = (sun_anim_t *)lv_timer_get_user_data(t);
    uint32_t t0 = lv_tick_get();
    a->ms = (a->ms + SUN_FRAME_MS) % SUN_LOOP_MS;
    sky_sun_frame(&a->sun, (float)a->ms / SUN_LOOP_MS, a->frame);
    blit(a->buf, a->frame, a->sun.d);
    lv_obj_invalidate(a->canvas);
    a->paint_ms += lv_tick_elaps(t0);
    if (++a->frames == 100) {   // what the animation costs, on the device's own clock
        LV_LOG_USER("sky_body: sun %dpx: 100 frames painted in %u ms", a->sun.d,
                    (unsigned)a->paint_ms);
        a->frames = a->paint_ms = 0;
    }
}

/**
 * @brief Free @p a and what it holds (any of it may be missing).
 * @version 1
 * @utility
 */
static void sun_free(sun_anim_t *a)
{
    if (!a) return;
    lv_free(a->tables);
    lv_free(a->frame);
    lv_free(a);
}

/**
 * @brief The sun's canvas is going: stop its timer, free its memory.
 * @version 1
 * @callback
 */
static void sun_delete_cb(lv_event_t *e)
{
    sun_anim_t *a = (sun_anim_t *)lv_event_get_user_data(e);
    lv_timer_delete(a->timer);
    lv_draw_buf_destroy(a->buf);
    sun_free(a);
}

/**
 * @brief A sun's memory for @p d px: its tables, its frame, its canvas under @p parent.
 * @version 1
 * @utility
 * @return It, or NULL (nothing left allocated).
 */
static sun_anim_t *sun_alloc(lv_obj_t *parent, int32_t d)
{
    sun_anim_t *a = lv_malloc_zeroed(sizeof(*a));
    bool ok = a != NULL;
    if (ok) a->tables = lv_malloc(sky_sun_bytes(d));
    if (ok) a->frame = lv_malloc((size_t)d * (size_t)d * sizeof(uint32_t));
    ok = ok && a->tables && a->frame;
    if (ok) a->canvas = canvas_for(parent, d, &a->buf);
    if (!ok || !a->canvas) {
        sun_free(a);
        a = NULL;
    }
    return a;
}

/**
 * @brief An animated sun.
 * @version 1
 * @req REQ-ASTRO-001
 * @return It, or NULL.
 */
lv_obj_t *sky_body_sun_create(lv_obj_t *parent, int32_t d)
{
    sun_anim_t *a = sun_alloc(parent, d);
    if (!a) return NULL;
    sky_sun_init(&a->sun, a->tables, d, rgb(ui_theme_color(UI_ROLE_SUN)),
                 rgb(ui_theme_color(UI_ROLE_MOON)));
    a->timer = lv_timer_create(sun_tick_cb, SUN_FRAME_MS, a);
    lv_obj_add_event_cb(a->canvas, sun_delete_cb, LV_EVENT_DELETE, a);
    sun_tick_cb(a->timer);   // the first frame now
    return a->canvas;
}

/**
 * @brief The moon.
 * v2: a slow paint (over 10 ms) is reported.
 * @version 2
 * @req REQ-ASTRO-001
 * @return It, or NULL.
 */
lv_obj_t *sky_body_moon_create(lv_obj_t *parent, int32_t d, float age, lv_opa_t dark_opa)
{
    uint32_t *px = lv_malloc((size_t)d * (size_t)d * sizeof(uint32_t));
    lv_draw_buf_t *buf = NULL;
    lv_obj_t *cv = px ? canvas_for(parent, d, &buf) : NULL;
    if (cv) {
        uint32_t t0 = lv_tick_get();
        sky_paint_moon(px, d, age, rgb(ui_theme_color(UI_ROLE_MOON)),
                       rgb(ui_theme_color(UI_ROLE_SURFACE_INSET)),   // follows the card: light or dark
                       dark_opa);
        blit(buf, px, d);
        lv_obj_add_event_cb(cv, free_buf_cb, LV_EVENT_DELETE, buf);
        uint32_t took = lv_tick_elaps(t0);
        if (took > 10) LV_LOG_USER("sky_body: moon %dpx painted in %u ms", (int)d, (unsigned)took);
    }
    lv_free(px);
    return cv;
}

#define EVENTS_FRAME_MS 66   // 15 frames a second: meteors are quick

/** The sky's events: what they are, the frame, the buffer, the timer. */
typedef struct {
    sky_events_t   ev;
    uint32_t      *frame;
    lv_draw_buf_t *buf;
    lv_timer_t    *timer;
    lv_obj_t      *canvas;
    uint32_t       ms;
    uint32_t       frames, paint_ms;
} events_anim_t;

/**
 * @brief One events frame.
 * @version 1
 * @callback
 */
static void events_tick_cb(lv_timer_t *t)
{
    events_anim_t *a = (events_anim_t *)lv_timer_get_user_data(t);
    uint32_t t0 = lv_tick_get();
    a->ms += EVENTS_FRAME_MS;
    sky_events_frame(&a->ev, a->ms, a->frame);
    for (int32_t y = 0; y < a->ev.h; y++) {
        memcpy(a->buf->data + (size_t)y * a->buf->header.stride, a->frame + (size_t)y * (size_t)a->ev.w,
               (size_t)a->ev.w * sizeof(uint32_t));
    }
    lv_obj_invalidate(a->canvas);
    a->paint_ms += lv_tick_elaps(t0);
    if (++a->frames == 150) {
        LV_LOG_USER("sky_body: events %dx%d: 150 frames painted in %u ms", a->ev.w, a->ev.h,
                    (unsigned)a->paint_ms);
        a->frames = a->paint_ms = 0;
    }
}

/**
 * @brief The events layer is going: stop its timer, free its memory.
 * @version 1
 * @callback
 */
static void events_delete_cb(lv_event_t *e)
{
    events_anim_t *a = (events_anim_t *)lv_event_get_user_data(e);
    lv_timer_delete(a->timer);
    lv_draw_buf_destroy(a->buf);
    lv_free(a->frame);
    lv_free(a);
}

/**
 * @brief The sky's events layer.
 * @version 1
 * @req REQ-ASTRO-001
 * @return It, or NULL.
 */
lv_obj_t *sky_body_events_create(lv_obj_t *parent, int32_t w, int32_t h, unsigned night,
                                 float aurora, int zhr)
{
    events_anim_t *a = (aurora > 0.0f || zhr > 0) ? lv_malloc_zeroed(sizeof(*a)) : NULL;
    if (a) a->frame = lv_malloc((size_t)w * (size_t)h * sizeof(uint32_t));
    if (a && a->frame) {
        a->buf = lv_draw_buf_create(w, h, LV_COLOR_FORMAT_ARGB8888, 0);
    }
    if (!a || !a->frame || !a->buf) {
        if (a) lv_free(a->frame);
        lv_free(a);
        return NULL;
    }
    a->canvas = lv_canvas_create(parent);
    lv_canvas_set_draw_buf(a->canvas, a->buf);
    lv_obj_remove_flag(a->canvas, LV_OBJ_FLAG_CLICKABLE);
    sky_events_init(&a->ev, w, h, night, aurora, zhr, rgb(ui_theme_color(UI_ROLE_AURORA)),
                    rgb(ui_theme_color(UI_ROLE_AURORA_ALT)), rgb(ui_theme_color(UI_ROLE_METEOR)));
    a->timer = lv_timer_create(events_tick_cb, EVENTS_FRAME_MS, a);
    lv_obj_add_event_cb(a->canvas, events_delete_cb, LV_EVENT_DELETE, a);
    events_tick_cb(a->timer);
    return a->canvas;
}
