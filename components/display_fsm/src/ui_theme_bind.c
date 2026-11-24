/**
 * @file ui_theme_bind.c
 * @version 1
 * @brief Theme bindings: each bound object's fill, text and border follow a role, repainted
 *        on every theme or background change. See ui_theme_bind.h.
 */

#include "ui_theme_bind.h"
#include "esp_log.h"

#include <string.h>

static const char *TAG = "ui_bind";

#define NONE (-1)

/** One bound object: which role (or slot) each of its colours follows; NONE = unbound. */
typedef struct {
    lv_obj_t *obj;
    int8_t    fill_slot;
    int8_t    fill_role;
    int8_t    text_role;
    int8_t    border_role;
} binding_t;

static binding_t            *s_binds = NULL;
static int                   s_count = 0;
static int                   s_cap = 0;
static const lv_image_dsc_t *s_picture = NULL;

/**
 * @brief Forget @p obj's binding when it is deleted.
 * @version 1
 * @callback
 */
static void on_delete(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_target(e);
    for (int i = 0; i < s_count; i++) {
        if (s_binds[i].obj == obj) {
            s_binds[i] = s_binds[--s_count];
            break;
        }
    }
}

/**
 * @brief @p obj's binding, created (and its delete hook added) if it has none.
 * @version 1
 * @utility
 * @return The binding, or NULL when out of memory (logged).
 */
static binding_t *binding_of(lv_obj_t *obj)
{
    for (int i = 0; i < s_count; i++) {
        if (s_binds[i].obj == obj) return &s_binds[i];
    }
    if (s_count == s_cap) {
        int cap = s_cap ? s_cap * 2 : 64;
        binding_t *grown = lv_realloc(s_binds, (size_t)cap * sizeof(binding_t));
        if (!grown) {
            ESP_LOGE(TAG, "out of memory growing bindings to %d", cap);
            return NULL;
        }
        s_binds = grown;
        s_cap = cap;
    }
    binding_t *b = &s_binds[s_count++];
    *b = (binding_t){ obj, NONE, NONE, NONE, NONE };
    lv_obj_add_event_cb(obj, on_delete, LV_EVENT_DELETE, NULL);
    return b;
}

/**
 * @brief The topmost ancestor of @p obj (a screen or a display layer).
 * @version 1
 * @utility
 * @return The root object.
 */
static lv_obj_t *root_of(lv_obj_t *obj)
{
    while (lv_obj_get_parent(obj)) obj = lv_obj_get_parent(obj);
    return obj;
}

/**
 * @brief The outermost ancestor of @p obj below @p root that draws a fill (bg opacity > 0):
 *        the panel whose area the picture is sampled under. @p obj itself when none.
 * @version 1
 * @utility
 * @return The panel.
 */
static lv_obj_t *outer_panel(lv_obj_t *obj, lv_obj_t *root)
{
    lv_obj_t *panel = obj;
    for (lv_obj_t *o = obj; o && o != root; o = lv_obj_get_parent(o)) {
        if (lv_obj_get_style_bg_opa(o, LV_PART_MAIN) > LV_OPA_TRANSP) panel = o;
    }
    return panel;
}

/**
 * @brief The colour under every fill of @p obj's tree: the picture under @p panel when there
 *        is one, else @p root's colour when it is opaque, else the active screen's.
 * v2: the screen colour is the sampling fallback.
 * @version 2
 * @utility
 * @return The base colour.
 */
static lv_color_t base_under(lv_obj_t *root, lv_obj_t *panel)
{
    lv_obj_t *scr = lv_obj_get_style_bg_opa(root, LV_PART_MAIN) >= LV_OPA_COVER
                        ? root : lv_screen_active();
    lv_color_t base = scr ? lv_obj_get_style_bg_color(scr, LV_PART_MAIN)
                          : ui_theme_color(UI_ROLE_SCREEN);
    if (s_picture) {
        lv_obj_update_layout(panel);
        lv_area_t a;
        lv_obj_get_coords(panel, &a);
        base = ui_image_region_mean(s_picture, LV_MAX(a.x1, 0), LV_MAX(a.y1, 0),
                                    lv_area_get_width(&a), lv_area_get_height(&a), base);
    }
    return base;
}

/**
 * @brief The colour really behind @p obj.
 * @version 1
 * @req REQ-UI-003
 * @req REQ-BG-002
 * @return The backdrop colour.
 */
lv_color_t ui_bind_backdrop(lv_obj_t *obj)
{
    lv_obj_t *root = root_of(obj);
    lv_color_t c = base_under(root, outer_panel(obj, root));
    lv_obj_t *chain[16];
    int n = 0;
    for (lv_obj_t *o = obj; o && o != root && n < 16; o = lv_obj_get_parent(o)) chain[n++] = o;
    while (n-- > 0) {   // outermost first
        lv_opa_t opa = lv_obj_get_style_bg_opa(chain[n], LV_PART_MAIN);
        if (opa > LV_OPA_TRANSP) c = lv_color_mix(lv_obj_get_style_bg_color(chain[n], 0), c, opa);
    }
    return c;
}

/**
 * @brief Paint @p b's fill and border (pass 0) or its text (pass 1).
 * @version 1
 * @utility
 */
static void paint(const binding_t *b, int pass)
{
    if (pass == 0) {
        int role = b->fill_slot != NONE ? (int)ui_theme_slot_role((ui_slot_t)b->fill_slot)
                                        : b->fill_role;
        if (role != NONE) lv_obj_set_style_bg_color(b->obj, ui_theme_color((ui_role_t)role), 0);
        if (b->border_role != NONE) {
            lv_obj_set_style_border_color(b->obj, ui_theme_color((ui_role_t)b->border_role), 0);
        }
    } else if (b->text_role != NONE) {
        lv_color_t c = ui_theme_text_on((ui_role_t)b->text_role, ui_bind_backdrop(b->obj));
        lv_obj_set_style_text_color(b->obj, c, 0);
    }
}

/**
 * @brief Bind @p obj's background colour to @p slot's role.
 * @version 1
 * @req REQ-UI-003
 */
void ui_bind_fill(lv_obj_t *obj, ui_slot_t slot)
{
    binding_t *b = binding_of(obj);
    if (!b) return;
    b->fill_slot = (int8_t)slot;
    paint(b, 0);
}

/**
 * @brief Bind @p obj's background colour to @p role directly.
 * @version 1
 * @req REQ-UI-003
 */
void ui_bind_fill_role(lv_obj_t *obj, ui_role_t role)
{
    binding_t *b = binding_of(obj);
    if (!b) return;
    b->fill_slot = NONE;
    b->fill_role = (int8_t)role;
    paint(b, 0);
}

/**
 * @brief Bind @p obj's text colour to @p role, legible against its real backdrop.
 * @version 1
 * @req REQ-UI-003
 * @req REQ-BG-002
 */
void ui_bind_text(lv_obj_t *obj, ui_role_t role)
{
    binding_t *b = binding_of(obj);
    if (!b) return;
    b->text_role = (int8_t)role;
    paint(b, 1);
}

/**
 * @brief Bind @p obj's border colour to @p role.
 * @version 1
 * @req REQ-UI-003
 */
void ui_bind_border(lv_obj_t *obj, ui_role_t role)
{
    binding_t *b = binding_of(obj);
    if (!b) return;
    b->border_role = (int8_t)role;
    paint(b, 0);
}

/**
 * @brief Set the picture text backdrops sample; repaint bound text when it changed.
 * @version 1
 * @req REQ-UI-003
 * @req REQ-BG-002
 */
void ui_bind_set_picture(const lv_image_dsc_t *picture)
{
    if (picture == s_picture) return;
    s_picture = picture;
    for (int i = 0; i < s_count; i++) paint(&s_binds[i], 1);
}

/**
 * @brief Repaint every bound object: fills and borders, then text against them.
 * @version 1
 * @req REQ-UI-003
 */
void ui_bind_refresh_all(void)
{
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < s_count; i++) paint(&s_binds[i], pass);
    }
    ESP_LOGI(TAG, "repainted %d bound object(s)", s_count);
}
