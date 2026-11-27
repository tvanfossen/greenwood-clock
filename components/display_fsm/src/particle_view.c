/**
 * @file particle_view.c
 * v3: one rect per particle unless the join is huge; display render/refresh perf stats.
 * v4: display stats come from the shared refresh probe.
 * v5: each redraw area draws only the particles that can touch it (a band index of this
 *     frame's boxes) instead of testing all of them: same particles, same order.
 * @version 5
 * @brief Shared particle-field renderer: one custom-draw object, per-particle dirty rects.
 */
#include "particle_view.h"
#include "src/misc/lv_area_private.h"  /* lv_area_intersect / lv_area_join (private API) */
#include "refresh_probe.h"
#include "particle_index.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "particles";

_Static_assert(PARTICLE_VIEW_MAX <= PARTICLE_INDEX_MAX, "the band index must hold every particle");

/** Simulation/redraw period (30 Hz target; LVGL load decides what is achieved). */
#define PARTICLE_PERIOD_MS 33
/** Clamp the step so a stalled LVGL task cannot teleport particles. */
#define PARTICLE_MAX_DT_S  0.12f
/** Perf log period. */
#define PARTICLE_STATS_MS  10000

struct particle_view_t {
    const particle_view_ops_t *ops;
    void        *model;
    lv_obj_t    *obj;
    lv_timer_t  *timer;
    uint32_t     last_tick;
    lv_area_t    before[PARTICLE_VIEW_MAX];  /* per-frame scratch; SPIRAM with the view */
    lv_area_t    now[PARTICLE_VIEW_MAX];     /* where each particle is drawn this frame */
    particle_index_t index;                  /* which of them each redraw area can touch */
    uint32_t     stats_start;
    uint32_t     stats_ticks;
    int64_t      stats_cost_us;
    int64_t      stats_cost_max_us;
    refresh_probe_t probe;   /* display-side cost per refresh while running */
};

/**
 * @brief Number of particles, clamped to the scratch capacity.
 * @version 1
 * @utility
 * @return Particle count.
 */
static int pv_count(const particle_view_t *v)
{
    int n = v->ops->count(v->model);
    return n > PARTICLE_VIEW_MAX ? PARTICLE_VIEW_MAX : n;
}

/**
 * @brief Zero the perf window.
 * v2: also zeroes the display render/refresh counters.
 * v4: resets (and arms or disarms) the shared refresh probe.
 * @version 4
 * @utility
 */
static void pv_stats_reset(particle_view_t *v, uint32_t now, bool armed)
{
    v->stats_start = now;
    v->stats_ticks = 0;
    v->stats_cost_us = 0;
    v->stats_cost_max_us = 0;
    refresh_probe_reset(&v->probe, armed);
}

/**
 * @brief Accumulate one tick's cost; every PARTICLE_STATS_MS log achieved Hz and cost.
 * v2: logs display render/refresh time and dirty areas per refresh.
 * v4: reads them from the refresh probe; also logs the longest refresh gap.
 * @version 4
 * @utility
 */
static void pv_stats(particle_view_t *v, uint32_t now, int64_t cost_us)
{
    v->stats_ticks++;
    v->stats_cost_us += cost_us;
    if (cost_us > v->stats_cost_max_us) v->stats_cost_max_us = cost_us;
    uint32_t span = lv_tick_diff(now, v->stats_start);
    if (span < PARTICLE_STATS_MS) return;
    const refresh_probe_t *p = &v->probe;
    uint32_t refrs = p->refrs ? p->refrs : 1;
    ESP_LOGI(TAG, "%s perf: %.1f Hz (target %d), tick cost avg %lld us max %lld us; "
             "display: %lu refreshes, render avg %lld us, refresh avg %lld us max %lld us, "
             "max gap %lld us, %lu dirty areas/refresh",
             v->ops->name, (double)v->stats_ticks * 1000.0 / span, 1000 / PARTICLE_PERIOD_MS,
             v->stats_cost_us / v->stats_ticks, v->stats_cost_max_us,
             (unsigned long)p->refrs, p->render_us / refrs, p->refr_us / refrs, p->refr_max_us,
             p->gap_max_us, (unsigned long)(p->areas / refrs));
    pv_stats_reset(v, now, true);
}

/**
 * @brief DRAW_MAIN: draw only particles intersecting the current clip area.
 * v2: only the particles the band index says can touch the area (same set and order as testing all).
 * @version 2
 * @callback
 */
static void pv_draw_cb(lv_event_t *e)
{
    particle_view_t *v = (particle_view_t *)lv_event_get_user_data(e);
    lv_layer_t *layer = lv_event_get_layer(e);
    uint8_t hit[PARTICLE_VIEW_MAX];
    int k = particle_index_query(&v->index, v->now, &layer->_clip_area, hit);
    for (int j = 0; j < k; j++) v->ops->draw(v->model, hit[j], layer);
}

/**
 * @brief Record where every particle is drawn this frame and index them by band.
 * @version 1
 * @utility
 */
static void pv_snapshot(particle_view_t *v)
{
    int n = pv_count(v);
    for (int i = 0; i < n; i++) v->ops->bounds(v->model, i, &v->now[i]);
    particle_index_build(&v->index, v->now, n);
}

/**
 * @brief Invalidate a particle's old and new boxes as ONE joined rect unless that rect is far
 *        larger than the two boxes (a respawn bottom -> top or a horizontal wrap), which are
 *        invalidated separately so the span between them is not redrawn.
 * v2: join unless the join exceeds 4x the boxes' combined area. v1 split whenever the boxes
 *     did not overlap; small fast flakes move further than their own size each tick, so most
 *     flakes produced two rects (~280 for 140 flakes), overflowing LV_INV_BUF_SIZE (256) and
 *     forcing a full-screen redraw every frame (laggy snowfall on device).
 * @version 3
 * @req REQ-ANIM-004
 */
static void pv_invalidate_move(lv_obj_t *obj, const lv_area_t *before, const lv_area_t *after)
{
    lv_area_t both;
    lv_area_join(&both, before, after);
    uint32_t parts = lv_area_get_size(before) + lv_area_get_size(after);
    if (lv_area_get_size(&both) <= 4 * parts) {
        lv_obj_invalidate_area(obj, &both);
    } else {
        lv_obj_invalidate_area(obj, before);
        lv_obj_invalidate_area(obj, after);
    }
}

/**
 * @brief Frame timer: step the model, then invalidate each particle's old and new rectangles.
 * v2: joins old/new only when they overlap (pv_invalidate_move); a respawn or wrap used to
 *     dirty a screen-spanning rectangle.
 * v4: keeps this frame's boxes and their band index (pv_snapshot).
 * @version 4
 * @callback
 */
static void pv_timer_cb(lv_timer_t *t)
{
    int64_t t0 = esp_timer_get_time();
    particle_view_t *v = (particle_view_t *)lv_timer_get_user_data(t);
    uint32_t now = lv_tick_get();
    float dt = (float)lv_tick_diff(now, v->last_tick) / 1000.0f;
    v->last_tick = now;
    if (dt > PARTICLE_MAX_DT_S) dt = PARTICLE_MAX_DT_S;

    int n = pv_count(v);
    for (int i = 0; i < n; i++) v->ops->bounds(v->model, i, &v->before[i]);
    v->ops->step(v->model, dt);
    pv_snapshot(v);   // where they are drawn now, indexed for the redraw
    n = pv_count(v);
    for (int i = 0; i < n; i++) pv_invalidate_move(v->obj, &v->before[i], &v->now[i]);
    pv_stats(v, now, esp_timer_get_time() - t0);
}

/**
 * @brief Allocate in SPIRAM; create the click-through object and a paused timer.
 * v2: hooks display refresh/render events for perf stats.
 * v4: through the shared refresh probe.
 * @version 4
 * @req REQ-ANIM-004
 * @return New view, or NULL on allocation failure.
 */
particle_view_t *particle_view_create(lv_obj_t *parent, const particle_view_ops_t *ops, void *model)
{
    particle_view_t *v = heap_caps_calloc(1, sizeof(*v), MALLOC_CAP_SPIRAM);
    if (!v) {
        ESP_LOGE(TAG, "%s: alloc failed (%u B)", ops->name, (unsigned)sizeof(*v));
        return NULL;
    }
    v->ops = ops;
    v->model = model;
    v->obj = lv_obj_create(parent);
    lv_obj_remove_style_all(v->obj);
    lv_obj_set_size(v->obj, lv_pct(100), lv_pct(100));
    lv_obj_clear_flag(v->obj, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(v->obj, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(v->obj, pv_draw_cb, LV_EVENT_DRAW_MAIN, v);
    v->timer = lv_timer_create(pv_timer_cb, PARTICLE_PERIOD_MS, v);
    lv_timer_pause(v->timer);
    refresh_probe_attach(&v->probe);
    v->last_tick = lv_tick_get();
    ESP_LOGI(TAG, "%s: view created (%d particles, %u B)", ops->name, pv_count(v), (unsigned)sizeof(*v));
    return v;
}

/**
 * @brief Run or freeze; resuming resets dt and the perf window so nothing jumps.
 * v2: arms the refresh probe only while running.
 * v3: indexes the particles before the first frame.
 * @version 3
 * @req REQ-ANIM-004
 */
void particle_view_set_running(particle_view_t *v, bool running)
{
    if (!v) return;
    if (running) {
        v->last_tick = lv_tick_get();
        pv_snapshot(v);   // drawn before the first tick too
        pv_stats_reset(v, v->last_tick, true);
        lv_obj_clear_flag(v->obj, LV_OBJ_FLAG_HIDDEN);
        lv_timer_resume(v->timer);
    } else {
        lv_timer_pause(v->timer);
        refresh_probe_reset(&v->probe, false);
        lv_obj_add_flag(v->obj, LV_OBJ_FLAG_HIDDEN);
    }
    ESP_LOGI(TAG, "%s: %s", v->ops->name, running ? "running" : "paused");
}

/**
 * @brief Tear down timer, object and storage (not the model).
 * v2: unhooks the display perf events.
 * v4: through the shared refresh probe.
 * @version 4
 * @req REQ-ANIM-004
 */
void particle_view_destroy(particle_view_t *v)
{
    if (!v) return;
    refresh_probe_detach(&v->probe);
    lv_timer_delete(v->timer);
    lv_obj_delete(v->obj);
    ESP_LOGI(TAG, "%s: destroyed", v->ops->name);
    heap_caps_free(v);
}
