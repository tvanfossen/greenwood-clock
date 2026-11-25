/**
 * @file panel_find.c
 * @version 1
 * @brief A test hook for the phone panel: find a visible control by the text it shows, so the
 *        screenshot and end-to-end scripts tap by label and the same steps work at every
 *        screen size. Exported to the page as _panel_find; the found area goes to
 *        window.panelFound as [x1, y1, x2, y2] in canvas pixels.
 */
#include "lvgl.h"

#include <emscripten.h>
#include <string.h>

/** Publish the found area (or nothing) as window.panelFound. */
EM_JS(void, js_found, (int ok, int x1, int y1, int x2, int y2),
      { window.panelFound = ok ? [x1, y1, x2, y2] : null; });

/**
 * @brief Whether @p obj is what @p text asks for: a label whose text contains it (menu
 *        entries start with their icon), or, for "slider", a slider.
 * @version 1
 * @utility
 * @return Whether it matches.
 */
static bool matches(lv_obj_t *obj, const char *text)
{
    if (strcmp(text, "slider") == 0) return lv_obj_check_type(obj, &lv_slider_class);
    if (!lv_obj_check_type(obj, &lv_label_class)) return false;
    const char *t = lv_label_get_text(obj);
    return t && strstr(t, text) != NULL;
}

#define FOUND_MAX 32

/** The visible matches, in drawing order (top of the screen first within a screen). */
typedef struct {
    lv_obj_t *obj[FOUND_MAX];
    int       n;
} found_t;

/**
 * @brief Collect the visible matches under @p obj, depth first, skipping hidden screens.
 * @version 1
 * @utility
 */
static void find_under(lv_obj_t *obj, const char *text, found_t *f)
{
    uint32_t n = lv_obj_get_child_count(obj);
    for (uint32_t i = 0; i < n && f->n < FOUND_MAX; i++) {
        lv_obj_t *c = lv_obj_get_child(obj, (int32_t)i);
        if (lv_obj_has_flag(c, LV_OBJ_FLAG_HIDDEN)) continue;
        if (matches(c, text)) f->obj[f->n++] = c;   // scrolled out of view counts too
        find_under(c, text, f);
    }
}

/**
 * @brief Find match @p nth (0: the first; -1: the last) of the controls showing @p text: a
 *        matching label's button when it is in one, else the label (or the slider) itself;
 *        its area (scrolled into view) is published as window.panelFound.
 * @version 1
 * @utility
 * @return 1 when found, else 0.
 */
EMSCRIPTEN_KEEPALIVE int panel_find(const char *text, int nth)
{
    found_t f = { .n = 0 };
    find_under(lv_screen_active(), text, &f);
    int i = nth < 0 ? f.n + nth : nth;
    lv_obj_t *hit = i >= 0 && i < f.n ? f.obj[i] : NULL;
    lv_obj_t *parent = hit ? lv_obj_get_parent(hit) : NULL;
    if (parent && lv_obj_has_class(parent, &lv_button_class)) hit = parent;   // list buttons too
    lv_area_t a = { 0, 0, 0, 0 };
    if (hit) {
        lv_obj_scroll_to_view_recursive(hit, LV_ANIM_OFF);   // as a person would scroll to it
        lv_obj_update_layout(hit);
        lv_obj_get_coords(hit, &a);
        lv_obj_get_transformed_area(hit, &a, LV_OBJ_POINT_TRANSFORM_FLAG_RECURSIVE);   // scaled
        // parents (the narrow theme editor) draw it, and take its touches, where it appears
    }
    js_found(hit != NULL, a.x1, a.y1, a.x2, a.y2);
    return hit != NULL;
}
