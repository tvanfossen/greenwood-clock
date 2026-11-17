/**
 * @file snowfall_view.c
 * @brief Snowfall effect: snow_physics model + round-flake drawing on the shared particle_view.
 */
#include "snowfall_view.h"
#include "particle_view.h"
#include "snow_physics.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"

static const char *TAG = "snowfall";

struct snowfall_view_t {
    particle_view_t *pv;
    snow_field_t     field;
};

/**
 * @brief Live flake count.
 * @version 1
 * @callback
 * @return Number of flakes.
 */
static int snow_count(void *m)
{
    return ((snow_field_t *)m)->count;
}

/**
 * @brief Advance the snow field.
 * @version 1
 * @callback
 */
static void snow_step(void *m, float dt_s)
{
    snow_field_step((snow_field_t *)m, dt_s);
}

/**
 * @brief Flake bounding box, padded one pixel for anti-aliasing.
 * @version 1
 * @callback
 */
static void snow_bounds(void *m, int i, lv_area_t *out)
{
    const snow_flake_t *fl = &((snow_field_t *)m)->flakes[i];
    int32_t r = (int32_t)(fl->radius_px + 1.5f);
    int32_t cx = (int32_t)fl->x_px, cy = (int32_t)fl->y_px;
    out->x1 = cx - r;
    out->y1 = cy - r;
    out->x2 = cx + r;
    out->y2 = cy + r;
}

/**
 * @brief Draw one flake as a white disc at its depth-faded opacity.
 * @version 1
 * @callback
 */
static void snow_draw(void *m, int i, lv_layer_t *layer)
{
    const snow_flake_t *fl = &((snow_field_t *)m)->flakes[i];
    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_color = lv_color_white();
    dsc.bg_opa = fl->opa;
    dsc.radius = LV_RADIUS_CIRCLE;
    lv_area_t a;
    snow_bounds(m, i, &a);
    lv_draw_rect(layer, &dsc, &a);
}

static const particle_view_ops_t SNOW_OPS = {
    .name = "snowfall", .count = snow_count, .step = snow_step, .bounds = snow_bounds, .draw = snow_draw,
};

/**
 * @brief Allocate the field in SPIRAM, seed it, and attach a particle view.
 * @version 2
 * @req REQ-SNOW-002
 * @return New view, or NULL on allocation failure.
 */
snowfall_view_t *snowfall_view_create(lv_obj_t *parent, int flake_count)
{
    snowfall_view_t *v = heap_caps_calloc(1, sizeof(*v), MALLOC_CAP_SPIRAM);
    if (!v) {
        ESP_LOGE(TAG, "alloc failed (%u B)", (unsigned)sizeof(*v));
        return NULL;
    }
    snow_params_t p;
    snow_params_default(&p, lv_obj_get_width(parent), lv_obj_get_height(parent));
    snow_field_init(&v->field, &p, flake_count, esp_random());
    v->pv = particle_view_create(parent, &SNOW_OPS, &v->field);
    if (!v->pv) {
        heap_caps_free(v);
        return NULL;
    }
    return v;
}

/**
 * @brief Run or freeze the snowfall.
 * @version 3
 * @req REQ-SNOW-002
 */
void snowfall_view_set_running(snowfall_view_t *v, bool running)
{
    if (v) particle_view_set_running(v->pv, running);
}

/**
 * @brief Destroy the view and free the field.
 * @version 2
 * @req REQ-SNOW-002
 */
void snowfall_view_destroy(snowfall_view_t *v)
{
    if (!v) return;
    particle_view_destroy(v->pv);
    heap_caps_free(v);
}
