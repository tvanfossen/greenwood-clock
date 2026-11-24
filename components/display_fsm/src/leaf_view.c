/**
 * @file leaf_view.c
 * @version 1
 * @brief Falling-leaves effect: each leaf is a rotated kite (two triangles) in an autumn colour.
 */
#include "leaf_view.h"
#include "leaf_field.h"
#include "particle_view.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include <math.h>

static const char *TAG = "leaves";

/** Kite half-width as a fraction of half-length, and where the widest point sits along the chord. */
#define LEAF_WIDTH_RATIO  0.42f
#define LEAF_WIDEST_AT   -0.15f

static const uint32_t LEAF_PALETTE[LEAF_PALETTE_SIZE] = {
    0xB83A1E,  /* maple red */
    0xD9661F,  /* orange */
    0xE0A526,  /* gold */
    0x8C5A2B,  /* brown */
    0xA4471B,  /* rust */
};

struct leaf_view_t {
    particle_view_t *pv;
    leaf_field_t     field;
};

/** Kite outline in screen space: tip, side A, tail, side B. */
typedef struct {
    lv_point_precise_t p[4];
} leaf_kite_t;

/**
 * @brief Rotated kite vertices for leaf @p i.
 * @version 1
 * @utility
 */
static void leaf_kite(const leaf_field_t *f, int i, leaf_kite_t *k)
{
    float cx, cy, ang;
    leaf_screen_pose(f, i, &cx, &cy, &ang);
    float hl = f->leaves[i].half_len_px, hw = hl * LEAF_WIDTH_RATIO;
    float c = cosf(ang), s = sinf(ang);
    /* chord-frame points: (along, across) */
    const float pts[4][2] = {{hl, 0.0f}, {LEAF_WIDEST_AT * hl, hw}, {-hl, 0.0f}, {LEAF_WIDEST_AT * hl, -hw}};
    for (int j = 0; j < 4; j++) {
        k->p[j].x = (lv_value_precise_t)(cx + pts[j][0] * c - pts[j][1] * s);
        k->p[j].y = (lv_value_precise_t)(cy + pts[j][0] * s + pts[j][1] * c);
    }
}

/**
 * @brief Live leaf count.
 * @version 1
 * @callback
 * @return Number of leaves.
 */
static int leaves_count(void *m)
{
    return ((leaf_field_t *)m)->count;
}

/**
 * @brief Advance the leaf field.
 * @version 1
 * @callback
 */
static void leaves_step(void *m, float dt_s)
{
    leaf_field_step((leaf_field_t *)m, dt_s);
}

/**
 * @brief Bounding box of the rotated kite, padded for anti-aliasing.
 * @version 1
 * @callback
 */
static void leaves_bounds(void *m, int i, lv_area_t *out)
{
    leaf_kite_t k;
    leaf_kite((leaf_field_t *)m, i, &k);
    float x1 = k.p[0].x, x2 = k.p[0].x, y1 = k.p[0].y, y2 = k.p[0].y;
    for (int j = 1; j < 4; j++) {
        x1 = fminf(x1, k.p[j].x);
        x2 = fmaxf(x2, k.p[j].x);
        y1 = fminf(y1, k.p[j].y);
        y2 = fmaxf(y2, k.p[j].y);
    }
    out->x1 = (int32_t)floorf(x1) - 2;
    out->y1 = (int32_t)floorf(y1) - 2;
    out->x2 = (int32_t)ceilf(x2) + 2;
    out->y2 = (int32_t)ceilf(y2) + 2;
}

/**
 * @brief Draw one leaf as two triangles (tip-sideA-tail, tip-tail-sideB).
 * @version 1
 * @callback
 */
static void leaves_draw(void *m, int i, lv_layer_t *layer)
{
    const leaf_field_t *f = (const leaf_field_t *)m;
    leaf_kite_t k;
    leaf_kite(f, i, &k);
    lv_draw_triangle_dsc_t dsc;
    lv_draw_triangle_dsc_init(&dsc);
    dsc.color = lv_color_hex(LEAF_PALETTE[f->leaves[i].color]);
    dsc.opa = f->leaves[i].opa;
    dsc.p[0] = k.p[0];
    dsc.p[1] = k.p[1];
    dsc.p[2] = k.p[2];
    lv_draw_triangle(layer, &dsc);
    dsc.p[1] = k.p[3];
    lv_draw_triangle(layer, &dsc);
}

static const particle_view_ops_t LEAF_OPS = {
    .name = "leaves", .count = leaves_count, .step = leaves_step, .bounds = leaves_bounds, .draw = leaves_draw,
};

/**
 * @brief Allocate the field in SPIRAM, seed it, attach a particle view.
 * @version 1
 * @req REQ-ANIM-003
 * @return New view, or NULL on allocation failure.
 */
leaf_view_t *leaf_view_create(lv_obj_t *parent, int leaf_count)
{
    leaf_view_t *v = heap_caps_calloc(1, sizeof(*v), MALLOC_CAP_SPIRAM);
    if (!v) {
        ESP_LOGE(TAG, "alloc failed (%u B)", (unsigned)sizeof(*v));
        return NULL;
    }
    leaf_params_t p;
    leaf_params_default(&p, lv_obj_get_width(parent), lv_obj_get_height(parent));
    leaf_field_init(&v->field, &p, leaf_count, esp_random());
    v->pv = particle_view_create(parent, &LEAF_OPS, &v->field);
    if (!v->pv) {
        heap_caps_free(v);
        return NULL;
    }
    return v;
}

/**
 * @brief Run or freeze the leaves.
 * @version 1
 * @req REQ-ANIM-003
 */
void leaf_view_set_running(leaf_view_t *v, bool running)
{
    if (v) particle_view_set_running(v->pv, running);
}

/**
 * @brief Destroy the view and free the field.
 * @version 1
 * @req REQ-ANIM-003
 */
void leaf_view_destroy(leaf_view_t *v)
{
    if (!v) return;
    particle_view_destroy(v->pv);
    heap_caps_free(v);
}
