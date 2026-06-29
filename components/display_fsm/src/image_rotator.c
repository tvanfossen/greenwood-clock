// components/display_fsm/src/image_rotator.c
//
// Layer 1: ImageRotator — cycles through PNG images from a directory.
//
// The 1024x600 PNG decode is too slow to run on the LVGL render path (LVGL
// would lazily decode on first draw, stalling the transition). So decoding is
// split: image_rotator_decode_current() runs OFF the LVGL lock and produces a
// persistent RGB565 descriptor (RGB565 matches the framebuffer, so the blit is
// a straight PPA-accelerated copy with no per-draw decode); image_rotator_present()
// runs UNDER the lock and just swaps the descriptor in.

#include "display_widgets.h"
#include "image_decode.h"
#include "esp_log.h"
#include <string.h>
#include <strings.h>
#include <dirent.h>
#include <stdio.h>
#include <sys/stat.h>

static const char *TAG = "image_rotator";

#define MAX_IMAGES 50
#define MAX_PATH_LEN 128

// lv_obj_set_style_opa takes 3 args; the anim exec cb is (void*, int32_t).
// Casting directly leaves the selector arg as garbage on RISC-V → crash.
static void opa_anim_cb(void *obj, int32_t v)
{
    lv_obj_set_style_opa((lv_obj_t *)obj, (lv_opa_t)v, 0);
}

struct image_rotator_t {
    lv_obj_t       *container;
    lv_obj_t       *img_current;
    char            paths[MAX_IMAGES][MAX_PATH_LEN];
    int             count;
    int             current_index;
    lv_image_dsc_t *shown_dsc;     // descriptor currently set on img_current
    lv_image_dsc_t *pending_dsc;   // decoded off-lock, awaiting present()
    bool            faded_in;      // first present() fades the container in
};

static bool is_png(const char *name)
{
    size_t len = strlen(name);
    if (len < 5) return false;
    return strcasecmp(name + len - 4, ".png") == 0;
}

// Convert LVGL path (A:/photos/img.png) to POSIX path (/sdcard/photos/img.png)
static void lvgl_to_posix(const char *lvgl_path, char *out, size_t out_sz)
{
    if (strncmp(lvgl_path, "A:/", 3) == 0) {
        snprintf(out, out_sz, "/sdcard/%s", lvgl_path + 3);
    } else {
        snprintf(out, out_sz, "%s", lvgl_path);
    }
}

// Verify file exists and is non-empty before telling LVGL to load it
static bool file_exists(const char *lvgl_path)
{
    char posix[MAX_PATH_LEN];
    lvgl_to_posix(lvgl_path, posix, sizeof(posix));
    struct stat st;
    return stat(posix, &st) == 0 && st.st_size > 0;
}

// Full-screen black container that holds the rotating image.
static lv_obj_t *make_container(lv_obj_t *parent)
{
    lv_obj_t *c = lv_obj_create(parent);
    lv_obj_set_size(c, 1024, 600);
    lv_obj_align(c, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(c, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(c, 0, 0);
    lv_obj_set_style_pad_all(c, 0, 0);
    lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return c;
}

// Scan dir_path for readable PNG files into r->paths (up to MAX_IMAGES).
// GIFs need lv_gif_create — not supported here.
static void scan_png_dir(image_rotator_t *r, const char *dir_path)
{
    char fs_path[MAX_PATH_LEN];
    lvgl_to_posix(dir_path, fs_path, sizeof(fs_path));

    DIR *d = opendir(fs_path);
    if (!d) {
        ESP_LOGW(TAG, "Cannot open directory: %s", fs_path);
        return;
    }
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL && r->count < MAX_IMAGES) {
        if (ent->d_type != DT_REG || !is_png(ent->d_name)) continue;

        char lvgl_path[MAX_PATH_LEN];
        snprintf(lvgl_path, sizeof(lvgl_path), "%s/%s", dir_path, ent->d_name);
        if (!file_exists(lvgl_path)) {
            ESP_LOGW(TAG, "  skip (not found): %s", lvgl_path);
            continue;
        }
        snprintf(r->paths[r->count], MAX_PATH_LEN, "%s", lvgl_path);
        ESP_LOGI(TAG, "  [%d] %s", r->count, lvgl_path);
        r->count++;
    }
    closedir(d);
}

image_rotator_t *image_rotator_create(lv_obj_t *parent, const char *dir_path,
                                      int start_index)
{
    image_rotator_t *r = lv_malloc(sizeof(image_rotator_t));
    if (!r) {
        ESP_LOGE(TAG, "Failed to allocate image_rotator_t");
        return NULL;
    }
    memset(r, 0, sizeof(*r));

    r->container = make_container(parent);
    r->img_current = lv_image_create(r->container);
    lv_obj_align(r->img_current, LV_ALIGN_CENTER, 0, 0);

    scan_png_dir(r, dir_path);

    if (r->count == 0) {
        ESP_LOGW(TAG, "ImageRotator: no PNG images found in %s", dir_path);
        return r;
    }

    // Resume from the requested index (wrap/clamp into range). The image is NOT
    // decoded here — the caller drives decode_current() off-lock then present().
    r->current_index = ((start_index % r->count) + r->count) % r->count;
    ESP_LOGI(TAG, "ImageRotator created: %d PNG images from %s (start=%d)",
             r->count, dir_path, r->current_index);
    return r;
}

void image_rotator_destroy(image_rotator_t *r)
{
    if (!r) return;
    if (r->container) {
        lv_anim_delete(r->container, NULL);
        // Detach the descriptor before the image object is freed so LVGL drops
        // its reference, then free our persistent descriptors.
        if (r->img_current) lv_image_set_src(r->img_current, NULL);
        lv_obj_delete(r->container);
    }
    if (r->shown_dsc)   image_decode_free(r->shown_dsc);
    if (r->pending_dsc) image_decode_free(r->pending_dsc);
    lv_free(r);
    ESP_LOGI(TAG, "ImageRotator destroyed");
}

void image_rotator_step(image_rotator_t *r)
{
    if (!r || r->count == 0) return;
    r->current_index = (r->current_index + 1) % r->count;
}

void image_rotator_decode_current(image_rotator_t *r)
{
    if (!r || r->count == 0) return;
    // Drop any unconsumed prior decode, then decode the current image off-lock.
    if (r->pending_dsc) { image_decode_free(r->pending_dsc); r->pending_dsc = NULL; }

    char posix[MAX_PATH_LEN];
    lvgl_to_posix(r->paths[r->current_index], posix, sizeof(posix));
    r->pending_dsc = image_decode_png_file_rgb565(posix);
    if (!r->pending_dsc) {
        ESP_LOGW(TAG, "decode failed [%d] %s", r->current_index, posix);
    }
}

void image_rotator_present(image_rotator_t *r)
{
    if (!r || !r->pending_dsc) return;

    // Swap in the freshly decoded descriptor; free the previously shown one only
    // after LVGL is pointed at the new buffer.
    lv_image_set_src(r->img_current, r->pending_dsc);
    if (r->shown_dsc) image_decode_free(r->shown_dsc);
    r->shown_dsc   = r->pending_dsc;
    r->pending_dsc = NULL;
    lv_obj_align(r->img_current, LV_ALIGN_CENTER, 0, 0);

    if (!r->faded_in) {
        r->faded_in = true;
        lv_obj_set_style_opa(r->container, LV_OPA_TRANSP, 0);
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, r->container);
        lv_anim_set_values(&a, LV_OPA_TRANSP, LV_OPA_COVER);
        lv_anim_set_duration(&a, 600);
        lv_anim_set_exec_cb(&a, opa_anim_cb);
        lv_anim_start(&a);
    }
    ESP_LOGI(TAG, "Present [%d] %s", r->current_index, r->paths[r->current_index]);
}

int image_rotator_count(const image_rotator_t *r)
{
    return r ? r->count : 0;
}

int image_rotator_index(const image_rotator_t *r)
{
    return r ? r->current_index : 0;
}
