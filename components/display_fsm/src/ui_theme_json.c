/**
 * @file ui_theme_json.c
 * v2: optional "slots" map.
 * v3: ui_theme_to_json (the theme editor saves with it).
 * v4: the optional fourth core, "alert".
 * @version 4
 * @brief Parse a theme file (three OKLCH cores, an optional alert core and a contrast
 *        target) into a ui_theme_t, and write one back.
 *        Pure C over cJSON: host-tested.
 */

#include "ui_theme.h"
#include "cJSON.h"

#include <stdarg.h>
#include <stdio.h>
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
 * @brief Read the optional "alert" core of @p root into @p out (left as it is when absent).
 * @version 1
 * @utility
 * @return Whether it is absent or a valid [L, C, h].
 */
static bool read_alert(const cJSON *root, ui_oklch_t *out)
{
    return !cJSON_GetObjectItemCaseSensitive(root, "alert") || read_oklch(root, "alert", out);
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
 * @brief Apply the optional "slots" object of @p root ({"slot": "role"}) onto @p slots.
 * @version 1
 * @utility
 * @return Whether it is absent, or an object whose every entry names a slot and a role.
 */
static bool read_slots(const cJSON *root, uint8_t slots[UI_SLOT_COUNT])
{
    const cJSON *obj = cJSON_GetObjectItemCaseSensitive(root, "slots");
    bool ok = !obj || cJSON_IsObject(obj);
    const cJSON *e = obj ? obj->child : NULL;
    for (; ok && e; e = e->next) {
        ui_slot_t slot;
        ui_role_t role;
        ok = cJSON_IsString(e) && ui_theme_slot_by_name(e->string, &slot) &&
             ui_theme_role_by_name(e->valuestring, &role);
        if (ok) slots[slot] = (uint8_t)role;
    }
    return ok;
}

/**
 * @brief Parse a theme file into @p out.
 * v2: optional "slots".
 * v3: optional "alert" (the base theme's core when absent).
 * @version 3
 * @req REQ-UI-003
 * @return Whether @p json is a complete, in-range theme.
 */
bool ui_theme_parse(const char *json, ui_theme_t *out)
{
    cJSON *root = json ? cJSON_Parse(json) : NULL;
    ui_theme_t t;
    memset(&t, 0, sizeof(t));
    memcpy(t.slots, UI_THEME_BASE.slots, sizeof(t.slots));
    t.alert = UI_THEME_BASE.alert;
    const cJSON *name = cJSON_GetObjectItemCaseSensitive(root, "name");
    bool ok = cJSON_IsString(name) && name->valuestring[0] &&
              read_oklch(root, "ink", &t.ink) && read_oklch(root, "surface", &t.surface) &&
              read_oklch(root, "accent", &t.accent) && read_alert(root, &t.alert) &&
              read_contrast(root, &t.contrast) &&
              read_slots(root, t.slots);
    if (ok) {
        strncpy(t.name, name->valuestring, sizeof(t.name) - 1);
        *out = t;
    }
    cJSON_Delete(root);
    return ok;
}

/**
 * @brief Append printf-style text at @p *at in @p buf (of @p n bytes), tracking overflow.
 * @version 1
 * @utility
 * @return Whether everything so far fit.
 */
__attribute__((format(printf, 5, 6)))
static bool append(char *buf, size_t n, size_t *at, bool ok, const char *fmt, ...)
{
    if (!ok || *at >= n) return false;
    va_list ap;
    va_start(ap, fmt);
    int w = vsnprintf(buf + *at, n - *at, fmt, ap);
    va_end(ap);
    ok = w >= 0 && (size_t)w < n - *at;
    *at += ok ? (size_t)w : 0;
    return ok;
}

/**
 * @brief Write @p t as a theme file.
 * v2: the alert core too.
 * @version 2
 * @req REQ-UI-003
 * @return Whether it fit in @p n bytes.
 */
bool ui_theme_to_json(const ui_theme_t *t, char *buf, size_t n)
{
    size_t at = 0;
    if (n) buf[0] = '\0';
    const ui_oklch_t *c[4] = { &t->ink, &t->surface, &t->accent, &t->alert };
    const char *key[4] = { "ink", "surface", "accent", "alert" };
    bool ok = append(buf, n, &at, true, "{\n  \"name\": \"%s\",\n", t->name);
    for (int i = 0; i < 4; i++) {
        ok = append(buf, n, &at, ok, "  \"%s\": [%.4f, %.4f, %.2f],\n", key[i],
                    (double)c[i]->l, (double)c[i]->c, (double)c[i]->h);
    }
    ok = append(buf, n, &at, ok, "  \"contrast\": %.2f,\n  \"slots\": {", (double)t->contrast);
    for (int i = 0; i < UI_SLOT_COUNT; i++) {
        ok = append(buf, n, &at, ok, "%s\"%s\": \"%s\"", i ? ", " : "",
                    ui_theme_slot_name((ui_slot_t)i), ui_theme_role_name((ui_role_t)t->slots[i]));
    }
    ok = append(buf, n, &at, ok, "}\n}\n");
    if (!ok && n) buf[0] = '\0';
    return ok;
}
