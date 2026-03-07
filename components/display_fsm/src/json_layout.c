// components/display_fsm/src/json_layout.c
//
// Layer 1: JsonLayout — creates LVGL widgets from JSON DSL.
// Supports: label, lottie, image widget types.
// Caller must hold LVGL lock for all functions.
//
// JSON format:
// {
//   "bg_color": "#1a1a2e",
//   "bg_image": "A:/backgrounds/birthday.png",
//   "duration_s": 86400,
//   "children": [
//     {"type": "label", "text": "Hello!", "font": "nunito_128", "color": "#ff69b4", "x": 100, "y": 200},
//     {"type": "image", "src": "A:/photos/pic.png", "x": 0, "y": 0, "w": 1024, "h": 600},
//     {"type": "lottie", "src": "A:/lottie/hearts.json", "x": 262, "y": 50, "w": 500, "h": 500}
//   ]
// }

#include "display_widgets.h"
#include "cJSON.h"
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

static lv_color_t parse_color(const char *hex_str)
{
    if (!hex_str || hex_str[0] != '#' || strlen(hex_str) < 7) {
        return lv_color_white();
    }
    uint32_t val = (uint32_t)strtol(hex_str + 1, NULL, 16);
    return lv_color_hex(val);
}

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

    if (color && cJSON_IsString(color)) {
        lv_obj_set_style_text_color(lbl, parse_color(color->valuestring), 0);
    } else {
        lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
    }

    if (x && cJSON_IsNumber(x) && y && cJSON_IsNumber(y)) {
        lv_obj_set_pos(lbl, x->valueint, y->valueint);
    } else if (y && cJSON_IsNumber(y)) {
        lv_obj_align(lbl, LV_ALIGN_TOP_MID, 0, y->valueint);
    } else {
        lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 0);
    }

    return lbl;
}

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

    // Container
    jl->container = lv_obj_create(parent);
    lv_obj_set_size(jl->container, 1024, 600);
    lv_obj_align(jl->container, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_border_width(jl->container, 0, 0);
    lv_obj_set_style_pad_all(jl->container, 0, 0);
    lv_obj_set_style_radius(jl->container, 0, 0);
    lv_obj_clear_flag(jl->container, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    // Background color
    const cJSON *bg_color = cJSON_GetObjectItem(root, "bg_color");
    if (bg_color && cJSON_IsString(bg_color)) {
        lv_obj_set_style_bg_color(jl->container, parse_color(bg_color->valuestring), 0);
        lv_obj_set_style_bg_opa(jl->container, LV_OPA_COVER, 0);
    } else {
        lv_obj_set_style_bg_color(jl->container, lv_color_hex(0x1a1a2e), 0);
        lv_obj_set_style_bg_opa(jl->container, LV_OPA_COVER, 0);
    }

    // Background image
    const cJSON *bg_image = cJSON_GetObjectItem(root, "bg_image");
    if (bg_image && cJSON_IsString(bg_image)) {
        lv_obj_t *bg = lv_image_create(jl->container);
        lv_image_set_src(bg, bg_image->valuestring);
        lv_obj_align(bg, LV_ALIGN_TOP_LEFT, 0, 0);
        if (jl->child_count < MAX_CHILDREN) {
            jl->children[jl->child_count++] = bg;
        }
    }

    // Children
    const cJSON *children = cJSON_GetObjectItem(root, "children");
    if (children && cJSON_IsArray(children)) {
        const cJSON *child;
        cJSON_ArrayForEach(child, children) {
            if (jl->child_count >= MAX_CHILDREN) break;

            const cJSON *type = cJSON_GetObjectItem(child, "type");
            if (!type || !cJSON_IsString(type)) continue;

            lv_obj_t *obj = NULL;
            if (strcmp(type->valuestring, "label") == 0) {
                obj = create_label_child(jl->container, child);
            } else if (strcmp(type->valuestring, "image") == 0) {
                obj = create_image_child(jl->container, child);
            }
            // Note: "lottie" type deferred — requires lv_lottie integration
            // which is complex. For now, log and skip.
            else if (strcmp(type->valuestring, "lottie") == 0) {
                ESP_LOGW(TAG, "Lottie child type not yet implemented in JSON layout");
            }

            if (obj) {
                jl->children[jl->child_count++] = obj;
            }
        }
    }

    cJSON_Delete(root);
    ESP_LOGI(TAG, "JsonLayout created: %d children", jl->child_count);
    return jl;
}

void json_layout_destroy(json_layout_t *jl)
{
    if (!jl) return;
    // Deleting container deletes all children
    if (jl->container) lv_obj_del(jl->container);
    lv_free(jl);
    ESP_LOGI(TAG, "JsonLayout destroyed");
}
