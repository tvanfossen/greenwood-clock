/**
 * @file library.c
 * @version 1
 * @brief The Library's rules (see library.h). Pure C.
 */
#include "library.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

/**
 * @brief The field of @p r that names @p kind's items (NULL for the album: no profile does).
 * @version 1
 * @utility
 * @return The field, and its size in @p len.
 */
static char *field(sched_rule_t *r, lib_kind_t kind, size_t *len)
{
    char *f = NULL;
    *len = 0;
    if (kind == LIB_BACKGROUNDS) { f = r->image; *len = sizeof(r->image); }
    if (kind == LIB_THEMES) { f = r->theme; *len = sizeof(r->theme); }
    if (kind == LIB_ANIMS) { f = r->anim; *len = sizeof(r->anim); }
    return f;
}

/**
 * @brief Who uses an item.
 * @version 1
 * @req REQ-LIB-001
 * @return How many.
 */
int lib_used_by(const sched_rule_t *r, int n, lib_kind_t kind, const char *value,
                const char **names, int max)
{
    int used = 0;
    for (int i = 0; i < n && value; i++) {
        size_t len;
        const char *f = field((sched_rule_t *)&r[i], kind, &len);
        if (!f || strcmp(f, value) != 0) continue;
        if (used < max) names[used] = r[i].name;
        used++;
    }
    return used;
}

/**
 * @brief How profiles name an item.
 * @version 1
 * @req REQ-LIB-001
 */
void lib_ref(lib_kind_t kind, const char *name, char *out, size_t n)
{
    if (kind == LIB_BACKGROUNDS) snprintf(out, n, "A:/backgrounds/%s.png", name);
    else snprintf(out, n, "%s", name);
}

/**
 * @brief Whether a name is safe.
 * @version 1
 * @req REQ-LIB-001
 * @return Whether.
 */
bool lib_name_ok(const char *name)
{
    size_t n = name ? strlen(name) : 0;
    bool ok = n > 0 && n <= LIB_NAME_MAX;
    for (size_t i = 0; ok && i < n; i++) {
        unsigned char c = (unsigned char)name[i];
        ok = isalnum(c) || c == '-' || c == '_';
    }
    return ok;
}

/**
 * @brief Carry a rename into the profiles.
 * @version 1
 * @req REQ-LIB-001
 * @return How many changed.
 */
int lib_rename_refs(sched_rule_t *r, int n, lib_kind_t kind, const char *from, const char *to)
{
    int changed = 0;
    for (int i = 0; i < n; i++) {
        size_t len;
        char *f = field(&r[i], kind, &len);
        if (!f || strcmp(f, from) != 0) continue;
        snprintf(f, len, "%s", to);
        changed++;
    }
    return changed;
}

/**
 * @brief Use an item in a profile.
 * @version 1
 * @req REQ-LIB-001
 * @return Whether the kind is one a profile names.
 */
bool lib_use_in(sched_rule_t *r, lib_kind_t kind, const char *name)
{
    size_t len;
    char *f = field(r, kind, &len);
    if (f) lib_ref(kind, name, f, len);
    return f != NULL;
}

/**
 * @brief How much of pixel [@p i, @p i + 1) lies in [@p a, @p b).
 * @version 1
 * @utility
 * @return 0..1.
 */
static float overlap(int i, float a, float b)
{
    float hi = (float)(i + 1) < b ? (float)(i + 1) : b;
    float lo = (float)i > a ? (float)i : a;
    return hi > lo ? hi - lo : 0.0f;
}

/**
 * @brief The average of @p src over [x0, x1) x [y0, y1) (pixels, fractional edges weighted),
 *        RGBA into @p out.
 * @version 1
 * @utility
 */
static void area_mean(const uint8_t *src, int stride, float x0, float x1, float y0, float y1,
                      uint8_t *out)
{
    float sum[3] = { 0, 0, 0 }, total = 0;
    for (int y = (int)y0; y < (int)(y1 + 0.999f); y++) {
        float wy = overlap(y, y0, y1);
        for (int x = (int)x0; x < (int)(x1 + 0.999f); x++) {
            float w = wy * overlap(x, x0, x1);
            const uint8_t *p = src + (size_t)y * (size_t)stride + (size_t)x * 4;
            sum[0] += w * p[2];   // BGRA in
            sum[1] += w * p[1];
            sum[2] += w * p[0];
            total += w;
        }
    }
    for (int c = 0; c < 3; c++) out[c] = (uint8_t)(total > 0 ? sum[c] / total + 0.5f : 0);
    out[3] = 255;
}

/**
 * @brief Shrink to a thumbnail.
 * @version 1
 * @req REQ-LIB-001
 */
void lib_thumb_scale(const uint8_t *src, int w, int h, int stride, uint8_t *out, int tw, int th)
{
    float sx = (float)w / tw, sy = (float)h / th;
    for (int ty = 0; ty < th; ty++) {
        for (int tx = 0; tx < tw; tx++) {
            area_mean(src, stride, tx * sx, (tx + 1) * sx, ty * sy, (ty + 1) * sy,
                      out + ((size_t)ty * (size_t)tw + (size_t)tx) * 4);
        }
    }
}
