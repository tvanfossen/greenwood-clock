/**
 * @file particle_view.c
 * @brief Shared particle-field renderer: one custom-draw object, per-particle dirty rects.
 */
#include "particle_view.h"
#include "src/misc/lv_area_private.h"  /* lv_area_intersect / lv_area_join (v9.3 private API) */

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "particles";

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
    uint32_t     stats_start;
    uint32_t     stats_ticks;
    int64_t      stats_cost_us;
    int64_t      stats_cost_max_us;
};

/**
 * @brief Number of particles, clamped to the scratch capacity.
 * @version 1
 * @dg_internal
 * @return Particle count.
 */
static int pv_count(const particle_view_t *v)
{
    int n = v->ops->count(v->model);
    return n > PARTICLE_VIEW_MAX ? PARTICLE_VIEW_MAX : n;
}

/**
 * @brief Zero the perf window.
 * @version 1
 * @dg_internal
 */
static void pv_stats_reset(particle_view_t *v, uint32_t now)
{
    v->stats_start = now;
    v->stats_ticks = 0;
    v->stats_cost_us = 0;
    v->stats_cost_max_us = 0;
}

/**
 * @brief Accumulate one tick's cost; every PARTICLE_STATS_MS log achieved Hz and cost.
 * @version 1
 * @dg_internal
 */
static void pv_stats(particle_view_t *v, uint32_t now, int64_t cost_us)
{
    v->stats_ticks++;
    v->stats_cost_us += cost_us;
    if (cost_us > v->stats_cost_max_us) v->stats_cost_max_us = cost_us;
    uint32_t span = lv_tick_diff(now, v->stats_start);
    if (span < PARTICLE_STATS_MS) return;
    ESP_LOGI(TAG, "%s perf: %.1f Hz (target %d), tick cost avg %lld us max %lld us", v->ops->name,
             (double)v->stats_ticks * 1000.0 / span, 1000 / PARTICLE_PERIOD_MS,
             v->stats_cost_us / v->stats_ticks, v->stats_cost_max_us);
    pv_stats_reset(v, now);
}

/**
 * @brief DRAW_MAIN: draw only particles intersecting the current clip area.
 * @version 1
 * @callback
 */
static void pv_draw_cb(lv_event_t *e)
{
    particle_view_t *v = (particle_view_t *)lv_event_get_user_data(e);
    lv_layer_t *layer = lv_event_get_layer(e);
    int n = pv_count(v);
    for (int i = 0; i < n; i++) {
        lv_area_t a, clipped;
        v->ops->bounds(v->model, i, &a);
        if (lv_area_intersect(&clipped, &a, &layer->_clip_area)) v->ops->draw(v->model, i, layer);
    }
}

/**
 * @brief Frame timer: step the model, then invalidate each particle's old∪new rectangle.
 * @version 1
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
    n = pv_count(v);
    for (int i = 0; i < n; i++) {
        lv_area_t after, both;
        v->ops->bounds(v->model, i, &after);
        lv_area_join(&both, &v->before[i], &after);
        lv_obj_invalidate_area(v->obj, &both);
    }
    pv_stats(v, now, esp_timer_get_time() - t0);
}

/**
 * @brief Allocate in SPIRAM; create the click-through object and a paused timer.
 * @version 1
 * @req REQ-ANIM-003
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
    v->last_tick = lv_tick_get();
    ESP_LOGI(TAG, "%s: view created (%d particles, %u B)", ops->name, pv_count(v), (unsigned)sizeof(*v));
    return v;
}

/**
 * @brief Run or freeze; resuming resets dt and the perf window so nothing jumps.
 * @version 1
 * @req REQ-ANIM-003
 */
void particle_view_set_running(particle_view_t *v, bool running)
{
    if (!v) return;
    if (running) {
        v->last_tick = lv_tick_get();
        pv_stats_reset(v, v->last_tick);
        lv_obj_clear_flag(v->obj, LV_OBJ_FLAG_HIDDEN);
        lv_timer_resume(v->timer);
    } else {
        lv_timer_pause(v->timer);
        lv_obj_add_flag(v->obj, LV_OBJ_FLAG_HIDDEN);
    }
    ESP_LOGI(TAG, "%s: %s", v->ops->name, running ? "running" : "paused");
}

/**
 * @brief Tear down timer, object and storage (not the model).
 * @version 1
 * @req REQ-ANIM-003
 */
void particle_view_destroy(particle_view_t *v)
{
    if (!v) return;
    lv_timer_delete(v->timer);
    lv_obj_delete(v->obj);
    ESP_LOGI(TAG, "%s: destroyed", v->ops->name);
    heap_caps_free(v);
}
