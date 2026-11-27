/**
 * @file particle_index.c
 * @version 1
 * @brief The particle band index (see particle_index.h). Pure C.
 */
#include "particle_index.h"

#include <string.h>

/**
 * @brief The band of row @p y, clamped to the index.
 * @version 1
 * @utility
 * @return 0..PARTICLE_INDEX_BANDS-1.
 */
static int band_of(int32_t y)
{
    int b = y < 0 ? 0 : (int)(y / PARTICLE_INDEX_BAND);
    return b < PARTICLE_INDEX_BANDS ? b : PARTICLE_INDEX_BANDS - 1;
}

/**
 * @brief Index the boxes.
 * @version 1
 * @req REQ-ANIM-004
 */
void particle_index_build(particle_index_t *x, const lv_area_t *boxes, int n)
{
    uint16_t count[PARTICLE_INDEX_BANDS] = { 0 };
    int total = 0;
    n = n < PARTICLE_INDEX_MAX ? n : PARTICLE_INDEX_MAX;
    for (int i = 0; i < n; i++) {
        for (int b = band_of(boxes[i].y1); b <= band_of(boxes[i].y2); b++, total++) count[b]++;
    }
    x->count = n;
    x->n = total <= PARTICLE_INDEX_SLOTS ? n : -1;
    x->start[0] = 0;
    for (int b = 0; b < PARTICLE_INDEX_BANDS; b++) x->start[b + 1] = (uint16_t)(x->start[b] + (x->n < 0 ? 0 : count[b]));
    uint16_t fill[PARTICLE_INDEX_BANDS];
    memcpy(fill, x->start, sizeof(fill));
    for (int i = 0; i < x->n; i++) {   // ascending i: each band lists its particles in order
        for (int b = band_of(boxes[i].y1); b <= band_of(boxes[i].y2); b++) x->idx[fill[b]++] = (uint8_t)i;
    }
}

/**
 * @brief Whether box @p a intersects @p b (edges inclusive).
 * @version 1
 * @utility
 * @return Whether.
 */
static bool hits(const lv_area_t *a, const lv_area_t *b)
{
    return a->x1 <= b->x2 && b->x1 <= a->x2 && a->y1 <= b->y2 && b->y1 <= a->y2;
}

/**
 * @brief Insert @p v into the ascending list @p out of @p k.
 * @version 1
 * @utility
 */
static void insert_sorted(uint8_t *out, int k, uint8_t v)
{
    int j = k;
    while (j > 0 && out[j - 1] > v) {
        out[j] = out[j - 1];
        j--;
    }
    out[j] = v;
}

/**
 * @brief The particles touching @p clip, in index order.
 * @version 1
 * @req REQ-ANIM-004
 * @return How many.
 */
int particle_index_query(particle_index_t *x, const lv_area_t *boxes, const lv_area_t *clip, uint8_t *out)
{
    int k = 0;
    if (x->n < 0) {   // too many memberships to index: a full scan
        for (int i = 0; i < x->count; i++) {
            if (hits(&boxes[i], clip)) out[k++] = (uint8_t)i;
        }
        return k;
    }
    if (++x->stamp == 0) {   // wrapped: forget every visit
        memset(x->seen, 0, sizeof(x->seen));
        x->stamp = 1;
    }
    for (int b = band_of(clip->y1); b <= band_of(clip->y2); b++) {
        for (int j = x->start[b]; j < x->start[b + 1]; j++) {
            uint8_t i = x->idx[j];
            if (x->seen[i] == x->stamp || !hits(&boxes[i], clip)) continue;
            x->seen[i] = x->stamp;
            insert_sorted(out, k++, i);
        }
    }
    return k;
}
