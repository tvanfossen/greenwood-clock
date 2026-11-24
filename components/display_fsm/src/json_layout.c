/**
 * @file json_layout.c
 * v2: colours are theme role names ("text", "accent", "surface_raised", ...), not hex.
 * @version 2
 * @brief JsonLayout: builds a full-screen LVGL container with background, label and image children from a JSON layout string.
 */
// Layer 1: JsonLayout — creates LVGL widgets from JSON DSL.
// Supports: label, lottie, image widget types.
// Caller must hold LVGL lock for all functions.
//
// JSON format:
// {
//   "bg_color": "surface_raised",
//   "bg_image": "A:/backgrounds/birthday.png",
//   "duration_s": 86400,
//   "children": [
//     {"type": "label", "text": "Hello!", "font": "nunito_128", "color": "accent_alt", "x": 100, "y": 200},
//     {"type": "image", "src": "A:/photos/pic.png", "x": 0, "y": 0, "w": 1024, "h": 600},
//     {"type": "lottie", "src": "A:/lottie/hearts.json", "x": 262, "y": 50, "w": 500, "h": 500}
//   ]
// }
// Colours name theme roles (ui_theme.h); a label's colour is made legible against the
// layout's background. Anything else (a hex string included) falls back to the default.

#include "display_widgets.h"
#include "cJSON.h"
#include "ui_theme_bind.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "json_layout";

LV_FONT_DECLARE(nunito_48);
LV_FONT_DECLARE(nunito_128);
LV_FONT_DECLARE(nunito_256);

#define MAX_CHILDREN 16

struct json_layout_t {
    lv_obj_t *container;
    lv_obj_t *children[MAX_CHILDREN];
    int       child_count;
};

/**
 * @brief The theme role named by @p item's string value, or @p fallback when it is missing or
 *        names no role (logged: a hex colour is refused, not drawn).
 * v2: role names (was "#RRGGBB").
 * @version 2
 * @utility
 * @return The role.
 */
static ui_role_t parse_role(const cJSON *item, ui_role_t fallback)
{
    ui_role_t role = fallback;
    if (item && cJSON_IsString(item) && !ui_theme_role_by_name(item->valuestring, &role)) {
        ESP_LOGW(TAG, "layout colour \"%s\" is not a theme role; using %s", item->valuestring,
                 ui_theme_role_name(fallback));
        role = fallback;
    }
    return role;
}

/**
 * @brief Map a font name (nunito_48/128/256, montserrat_14/20/24/32) to its LVGL font.
 * @version 1
 * @utility
 * @return Matching font, or montserrat 24 for unknown or missing names.
 */
static const lv_font_t *parse_font(const char *name)
{
    if (!name) return &lv_font_montserrat_24;
    if (strcmp(name, "nunito_256") == 0) return &nunito_256;
    if (strcmp(name, "nunito_128") == 0) return &nunito_128;
    if (strcmp(name, "nunito_48") == 0)  return &nunito_48;
    if (strcmp(name, "montserrat_32") == 0) return &lv_font_montserrat_32;
    if (strcmp(name, "montserrat_24") == 0) return &lv_font_montserrat_24;
    if (strcmp(name, "montserrat_20") == 0) return &lv_font_montserrat_20;
    if (strcmp(name, "montserrat_14") == 0) return &lv_font_montserrat_14;
    return &lv_font_montserrat_24;
}

/**
 * @brief Create a label child from JSON text, font (default nunito 48), colour role (default
 *        text, legible against the layout background) and x/y (or y-only top-centred, else
 *        centred).
 * v2: colour is a theme role.
 * v3: colours bound to the theme (ui_bind_*): repainted live on a theme change.
 * @version 3
 * @req REQ-DISP-005
 * @return New label, or NULL if "text" is missing.
 */
static lv_obj_t *create_label_child(lv_obj_t *parent, const cJSON *child)
{
    const cJSON *text  = cJSON_GetObjectItem(child, "text");
    const cJSON *font  = cJSON_GetObjectItem(child, "font");
    const cJSON *color = cJSON_GetObjectItem(child, "color");
    const cJSON *x     = cJSON_GetObjectItem(child, "x");
    const cJSON *y     = cJSON_GetObjectItem(child, "y");

    if (!text || !cJSON_IsString(text)) return NULL;

    lv_obj_t *lbl = lv_label_create(parent);
    lv_label_set_text(lbl, text->valuestring);

    if (font && cJSON_IsString(font)) {
        lv_obj_set_style_text_font(lbl, parse_font(font->valuestring), 0);
    } else {
        lv_obj_set_style_text_font(lbl, &nunito_48, 0);
    }

    ui_bind_text(lbl, parse_role(color, UI_ROLE_TEXT));   // legible against the layout bg

    if (x && cJSON_IsNumber(x) && y && cJSON_IsNumber(y)) {
        lv_obj_set_pos(lbl, x->valueint, y->valueint);
    } else if (y && cJSON_IsNumber(y)) {
        lv_obj_align(lbl, LV_ALIGN_TOP_MID, 0, y->valueint);
    } else {
        lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 0);
    }

    return lbl;
}

/**
 * @brief Create an image child from JSON src, optional w/h and x/y (else centred).
 * @version 1
 * @req REQ-DISP-005
 * @return New image, or NULL if "src" is missing.
 */
static lv_obj_t *create_image_child(lv_obj_t *parent, const cJSON *child)
{
    const cJSON *src = cJSON_GetObjectItem(child, "src");
    const cJSON *x   = cJSON_GetObjectItem(child, "x");
    const cJSON *y   = cJSON_GetObjectItem(child, "y");
    const cJSON *w   = cJSON_GetObjectItem(child, "w");
    const cJSON *h   = cJSON_GetObjectItem(child, "h");

    if (!src || !cJSON_IsString(src)) return NULL;

    lv_obj_t *img = lv_image_create(parent);
    lv_image_set_src(img, src->valuestring);

    if (w && cJSON_IsNumber(w) && h && cJSON_IsNumber(h)) {
        lv_obj_set_size(img, w->valueint, h->valueint);
    }
    if (x && cJSON_IsNumber(x) && y && cJSON_IsNumber(y)) {
        lv_obj_set_pos(img, x->valueint, y->valueint);
    } else {
        lv_obj_align(img, LV_ALIGN_CENTER, 0, 0);
    }

    return img;
}

// Container geometry + background colour from the root object.
/**
 * @brief Create the 1024x600 layout container and set its background from the "bg_color" role
 *        (default surface_raised).
 * v2: colour is a theme role.
 * v3: colours bound to the theme (ui_bind_*): repainted live on a theme change.
 * @version 3
 * @req REQ-DISP-005
 */
static void setup_container(json_layout_t *jl, lv_obj_t *parent, const cJSON *root)
{
    jl->container = lv_obj_create(parent);
    lv_obj_set_size(jl->container, 1024, 600);
    lv_obj_align(jl->container, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_border_width(jl->container, 0, 0);
    lv_obj_set_style_pad_all(jl->container, 0, 0);
    lv_obj_set_style_radius(jl->container, 0, 0);
    lv_obj_clear_flag(jl->container, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    ui_role_t bg = parse_role(cJSON_GetObjectItem(root, "bg_color"), UI_ROLE_SURFACE_RAISED);
    lv_obj_set_style_bg_opa(jl->container, LV_OPA_COVER, 0);
    ui_bind_fill_role(jl->container, bg);
}

// Append an object to the child list if there's room.
/**
 * @brief Append @p obj to the layout's child list if non-NULL and there is room (16 max).
 * @version 1
 * @utility
 */
static void layout_add_child(json_layout_t *jl, lv_obj_t *obj)
{
    if (obj && jl->child_count < MAX_CHILDREN) {
        jl->children[jl->child_count++] = obj;
    }
}

// Create a child object from its "type" field; NULL for unknown/handled types.
/**
 * @brief Create a child by its "type": label or image; lottie is logged as not implemented.
 * @version 1
 * @req REQ-DISP-005
 * @return New child object, or NULL for missing, unknown or unimplemented types.
 */
static lv_obj_t *create_typed_child(lv_obj_t *parent, const cJSON *child)
{
    const cJSON *type = cJSON_GetObjectItem(child, "type");
    if (!type || !cJSON_IsString(type)) return NULL;

    const char *t = type->valuestring;
    if (strcmp(t, "label") == 0) return create_label_child(parent, child);
    if (strcmp(t, "image") == 0) return create_image_child(parent, child);
    if (strcmp(t, "lottie") == 0) {
        // Deferred — needs lv_lottie integration. Log and skip.
        ESP_LOGW(TAG, "Lottie child type not yet implemented in JSON layout");
    }
    return NULL;
}

// Add the optional bg_image plus every entry in the "children" array.
/**
 * @brief Add the optional "bg_image" and every entry of the "children" array, up to 16 objects.
 * @version 1
 * @req REQ-DISP-005
 */
static void add_children(json_layout_t *jl, const cJSON *root)
{
    const cJSON *bg_image = cJSON_GetObjectItem(root, "bg_image");
    if (bg_image && cJSON_IsString(bg_image)) {
        lv_obj_t *bg = lv_image_create(jl->container);
        lv_image_set_src(bg, bg_image->valuestring);
        lv_obj_align(bg, LV_ALIGN_TOP_LEFT, 0, 0);
        layout_add_child(jl, bg);
    }

    const cJSON *children = cJSON_GetObjectItem(root, "children");
    if (!children || !cJSON_IsArray(children)) return;

    const cJSON *child;
    cJSON_ArrayForEach(child, children) {
        if (jl->child_count >= MAX_CHILDREN) break;
        layout_add_child(jl, create_typed_child(jl->container, child));
    }
}

/**
 * @brief Parse @p json_str and build the layout on @p parent; caller holds the LVGL lock.
 * @version 1
 * @req REQ-DISP-005
 * @req REQ-SURP-001
 * @return New layout, or NULL on NULL input, parse failure or allocation failure.
 */
json_layout_t *json_layout_create(lv_obj_t *parent, const char *json_str)
{
    if (!json_str) return NULL;

    cJSON *root = cJSON_Parse(json_str);
    if (!root) {
        ESP_LOGE(TAG, "Failed to parse JSON layout: %s", cJSON_GetErrorPtr());
        return NULL;
    }

    json_layout_t *jl = lv_malloc(sizeof(json_layout_t));
    if (!jl) {
        cJSON_Delete(root);
        ESP_LOGE(TAG, "Failed to allocate json_layout_t");
        return NULL;
    }
    memset(jl, 0, sizeof(*jl));

    setup_container(jl, parent, root);
    add_children(jl, root);

    cJSON_Delete(root);
    ESP_LOGI(TAG, "JsonLayout created: %d children", jl->child_count);
    return jl;
}

/**
 * @brief Delete the layout container (and all children) and free the layout (no-op if NULL).
 * @version 1
 * @req REQ-DISP-005
 */
void json_layout_destroy(json_layout_t *jl)
{
    if (!jl) return;
    // Deleting container deletes all children
    if (jl->container) {
        lv_anim_delete(jl->container, NULL);
        lv_obj_delete(jl->container);
    }
    lv_free(jl);
    ESP_LOGI(TAG, "JsonLayout destroyed");
}
