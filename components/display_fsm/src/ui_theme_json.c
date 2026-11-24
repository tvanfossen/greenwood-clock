/**
 * @file ui_theme_json.c
 * @version 1
 * @brief Parse a theme file (three OKLCH cores and a contrast target) into a ui_theme_t.
 *        Pure C over cJSON: host-tested.
 */

#include "ui_theme.h"
#include "cJSON.h"

#include <string.h>

#define MAX_CHROMA   0.4f    // past the sRGB gamut edge (~0.37)
#define MAX_CONTRAST 21.0f   // WCAG maximum (black on white)

/**
 * @brief Read the [L, C, h] array @p key of @p root into @p out, range-checked.
 * @version 1
 * @utility
 * @return Whether it is present, three numbers, and in range.
 */
static bool read_oklch(const cJSON *root, const char *key, ui_oklch_t *out)
{
    const cJSON *arr = cJSON_GetObjectItemCaseSensitive(root, key);
    if (!cJSON_IsArray(arr) || cJSON_GetArraySize(arr) != 3) return false;
    const cJSON *l = cJSON_GetArrayItem(arr, 0);
    const cJSON *c = cJSON_GetArrayItem(arr, 1);
    const cJSON *h = cJSON_GetArrayItem(arr, 2);
    if (!cJSON_IsNumber(l) || !cJSON_IsNumber(c) || !cJSON_IsNumber(h)) return false;
    ui_oklch_t v = { (float)l->valuedouble, (float)c->valuedouble, (float)h->valuedouble };
    bool ok = v.l >= 0.0f && v.l <= 1.0f && v.c >= 0.0f && v.c <= MAX_CHROMA &&
              v.h >= 0.0f && v.h <= 360.0f;
    if (ok) *out = v;
    return ok;
}

/**
 * @brief Read the optional "contrast" of @p root into @p out (7.0 when absent).
 * @version 1
 * @utility
 * @return Whether it is absent or a number in 1..21.
 */
static bool read_contrast(const cJSON *root, float *out)
{
    const cJSON *c = cJSON_GetObjectItemCaseSensitive(root, "contrast");
    *out = 7.0f;
    if (!c) return true;
    bool ok = cJSON_IsNumber(c) && c->valuedouble >= 1.0 && c->valuedouble <= MAX_CONTRAST;
    if (ok) *out = (float)c->valuedouble;
    return ok;
}

/**
 * @brief Parse a theme file into @p out.
 * @version 1
 * @req REQ-UI-003
 * @return Whether @p json is a complete, in-range theme.
 */
bool ui_theme_parse(const char *json, ui_theme_t *out)
{
    cJSON *root = json ? cJSON_Parse(json) : NULL;
    ui_theme_t t;
    memset(&t, 0, sizeof(t));
    const cJSON *name = cJSON_GetObjectItemCaseSensitive(root, "name");
    bool ok = cJSON_IsString(name) && name->valuestring[0] &&
              read_oklch(root, "ink", &t.ink) && read_oklch(root, "surface", &t.surface) &&
              read_oklch(root, "accent", &t.accent) && read_contrast(root, &t.contrast);
    if (ok) {
        strncpy(t.name, name->valuestring, sizeof(t.name) - 1);
        *out = t;
    }
    cJSON_Delete(root);
    return ok;
}
