// components/display_fsm/src/image_rotator.c
//
// Layer 1: ImageRotator — cycles through images from a directory.
// Supports PNG and GIF files from SD card.
// Caller must hold LVGL lock for all functions.

#include "display_widgets.h"
#include "esp_log.h"
#include <string.h>
#include <dirent.h>
#include <stdio.h>

static const char *TAG = "image_rotator";

#define MAX_IMAGES 50
#define MAX_PATH_LEN 128

struct image_rotator_t {
    lv_obj_t *container;
    lv_obj_t *img_current;
    char      paths[MAX_IMAGES][MAX_PATH_LEN];
    int       count;
    int       current_index;
};

static bool has_image_extension(const char *name)
{
    size_t len = strlen(name);
    if (len < 5) return false;
    const char *ext = name + len - 4;
    return (strcasecmp(ext, ".png") == 0 || strcasecmp(ext, ".gif") == 0);
}

image_rotator_t *image_rotator_create(lv_obj_t *parent, const char *dir_path)
{
    image_rotator_t *r = lv_malloc(sizeof(image_rotator_t));
    if (!r) {
        ESP_LOGE(TAG, "Failed to allocate image_rotator_t");
        return NULL;
    }
    memset(r, 0, sizeof(*r));

    // Container — full screen
    r->container = lv_obj_create(parent);
    lv_obj_set_size(r->container, 1024, 600);
    lv_obj_align(r->container, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(r->container, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(r->container, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(r->container, 0, 0);
    lv_obj_set_style_pad_all(r->container, 0, 0);
    lv_obj_clear_flag(r->container, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);

    // Image widget
    r->img_current = lv_image_create(r->container);
    lv_obj_align(r->img_current, LV_ALIGN_CENTER, 0, 0);

    // Scan directory for image files
    // dir_path is an LVGL path like "A:/photos"
    // Convert to filesystem path: "A:/photos" → "/sdcard/photos"
    char fs_path[MAX_PATH_LEN];
    if (strncmp(dir_path, "A:/", 3) == 0) {
        snprintf(fs_path, sizeof(fs_path), "/sdcard/%s", dir_path + 3);
    } else {
        strncpy(fs_path, dir_path, sizeof(fs_path) - 1);
        fs_path[sizeof(fs_path) - 1] = '\0';
    }

    DIR *d = opendir(fs_path);
    if (d) {
        struct dirent *ent;
        while ((ent = readdir(d)) != NULL && r->count < MAX_IMAGES) {
            if (ent->d_type == DT_REG && has_image_extension(ent->d_name)) {
                snprintf(r->paths[r->count], MAX_PATH_LEN, "%s/%s",
                         dir_path, ent->d_name);
                r->count++;
            }
        }
        closedir(d);
    }

    r->current_index = 0;

    // Show first image if available
    if (r->count > 0) {
        lv_image_set_src(r->img_current, r->paths[0]);
        ESP_LOGI(TAG, "ImageRotator created: %d images from %s", r->count, dir_path);
    } else {
        ESP_LOGW(TAG, "ImageRotator: no images found in %s", dir_path);
    }

    return r;
}

void image_rotator_destroy(image_rotator_t *r)
{
    if (!r) return;
    if (r->container) lv_obj_del(r->container);
    lv_free(r);
    ESP_LOGI(TAG, "ImageRotator destroyed");
}

void image_rotator_advance(image_rotator_t *r)
{
    if (!r || r->count == 0) return;

    r->current_index = (r->current_index + 1) % r->count;
    lv_image_set_src(r->img_current, r->paths[r->current_index]);
}

int image_rotator_count(const image_rotator_t *r)
{
    return r ? r->count : 0;
}
