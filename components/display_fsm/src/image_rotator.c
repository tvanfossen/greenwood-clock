/**
 * @file image_rotator.c
 * v2: the first photo fades in over 1.5 s, eased, with the next decode held until it ends
 *     (the decode competing with a 600 ms linear fade made it choppy).
 * v3: the album holds 200 (LIB_ALBUM_MAX), its paths in PSRAM.
 * @version 3
 * @brief ImageRotator: cycles PNGs from a directory with off-lock RGB565 decode, .r565 file cache, RAM cache and next-image prefetch.
 */
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
#include "library.h"
#include "ui_theme_bind.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <strings.h>
#include <dirent.h>
#include <stdio.h>
#include <sys/stat.h>

static const char *TAG = "image_rotator";

#define MAX_IMAGES LIB_ALBUM_MAX   // the album's cap (the Library shows and enforces it)
#define FADE_IN_MS 1500   // the first photo's fade from the screen colour
#define MAX_PATH_LEN 128

// lv_obj_set_style_opa takes 3 args; the anim exec cb is (void*, int32_t).
// Casting directly leaves the selector arg as garbage on RISC-V → crash.
/**
 * @brief lv_anim exec callback: set the object's opacity to @p v.
 * @version 1
 * @callback
 */
static void opa_anim_cb(void *obj, int32_t v)
{
    lv_obj_set_style_opa((lv_obj_t *)obj, (lv_opa_t)v, 0);
}

// Cross-visit RAM cache: the last-shown decoded photo is kept in PSRAM after the
// rotator is destroyed, so re-entering the album shows it INSTANTLY instead of
// paying the ~1s SD read again. Single entry; ownership moves cleanly between
// the static slot and the live rotator (no double-free).
static lv_image_dsc_t *s_persist_dsc;
static int             s_persist_index = -1;

struct image_rotator_t {
    lv_obj_t       *container;
    lv_obj_t       *img_current;
    char          (*paths)[MAX_PATH_LEN];   // MAX_IMAGES of them, in PSRAM
    int             count;
    int             current_index;
    lv_image_dsc_t *shown_dsc;     // descriptor currently set on img_current
    int             shown_index;   // which image shown_dsc holds (-1 = none)
    lv_image_dsc_t *pending_dsc;   // decoded off-lock, awaiting present()
    int             pending_index; // which image pending_dsc holds (-1 = none)
    bool            faded_in;      // first present() fades the container in
    volatile int64_t fade_end_us;  // when that fade ends (the next decode waits for it)
};

/**
 * @brief Check whether a file name ends in .png (case-insensitive, at least one stem character).
 * @version 1
 * @utility
 * @return true for a PNG name, false otherwise.
 */
static bool is_png(const char *name)
{
    size_t len = strlen(name);
    if (len < 5) return false;
    return strcasecmp(name + len - 4, ".png") == 0;
}

// Convert LVGL path (A:/photos/img.png) to POSIX path (/sdcard/photos/img.png)
/**
 * @brief Convert an LVGL "A:/..." path to its /sdcard POSIX path; other paths are copied unchanged.
 * @version 1
 * @utility
 */
static void lvgl_to_posix(const char *lvgl_path, char *out, size_t out_sz)
{
    if (strncmp(lvgl_path, "A:/", 3) == 0) {
        snprintf(out, out_sz, "/sdcard/%s", lvgl_path + 3);
    } else {
        snprintf(out, out_sz, "%s", lvgl_path);
    }
}

// Verify file exists and is non-empty before telling LVGL to load it
/**
 * @brief Check that the file behind an LVGL path exists and is non-empty.
 * @version 1
 * @utility
 * @return true if the file exists with size > 0, false otherwise.
 */
static bool file_exists(const char *lvgl_path)
{
    char posix[MAX_PATH_LEN];
    lvgl_to_posix(lvgl_path, posix, sizeof(posix));
    struct stat st;
    return stat(posix, &st) == 0 && st.st_size > 0;
}

/**
 * @brief Create the full-screen (1024x600) opaque SCREEN-role container that holds the rotating image.
 * v2: colours from the theme roles (ui_theme), no literals.
 * v3: colours bound to the theme (ui_bind_*): repainted live on a theme change.
 * @version 3
 * @utility
 * @return The new container.
 */
static lv_obj_t *make_container(lv_obj_t *parent)
{
    lv_obj_t *c = lv_obj_create(parent);
    lv_obj_set_size(c, 1024, 600);
    lv_obj_align(c, LV_ALIGN_CENTER, 0, 0);
    ui_bind_fill_role(c, UI_ROLE_SCREEN);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(c, 0, 0);
    lv_obj_set_style_pad_all(c, 0, 0);
    lv_obj_clear_flag(c, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    return c;
}

// Scan dir_path for readable PNG files into r->paths (up to MAX_IMAGES).
// GIFs need lv_gif_create — not supported here.
/**
 * @brief Scan a directory for readable PNG files into the rotator's path list (up to 50).
 * @version 1
 * @req REQ-PHOTO-001
 */
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

/**
 * @brief Create a rotator on @p parent over the PNGs in @p dir_path, starting at @p start_index (wrapped) or at the RAM-cached photo; does not decode.
 * v2: the path list in PSRAM, LIB_ALBUM_MAX (200) of them.
 * @version 2
 * @req REQ-PHOTO-001
 * @return New rotator (possibly with zero images), or NULL if allocation fails.
 */
image_rotator_t *image_rotator_create(lv_obj_t *parent, const char *dir_path,
                                      int start_index)
{
    image_rotator_t *r = lv_malloc(sizeof(image_rotator_t));
    if (!r) {
        ESP_LOGE(TAG, "Failed to allocate image_rotator_t");
        return NULL;
    }
    memset(r, 0, sizeof(*r));
    r->pending_index = -1;
    r->shown_index   = -1;
    r->paths = heap_caps_calloc(MAX_IMAGES, MAX_PATH_LEN, MALLOC_CAP_SPIRAM);
    if (!r->paths) {
        ESP_LOGE(TAG, "Failed to allocate the album's %d paths", MAX_IMAGES);
        lv_free(r);
        return NULL;
    }

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

    // If the last-shown photo is still cached in RAM, re-show it instantly on
    // re-entry: start there so the first decode_current() is a zero-cost hit.
    if (s_persist_dsc && s_persist_index >= 0 && s_persist_index < r->count) {
        r->current_index = s_persist_index;
    }
    ESP_LOGI(TAG, "ImageRotator created: %d PNG images from %s (start=%d)",
             r->count, dir_path, r->current_index);
    return r;
}

/**
 * @brief Delete the rotator's objects, keep its shown photo as the RAM cache for the next visit, discard the prefetched one and free it.
 * v2: frees the path list.
 * @version 2
 * @req REQ-PHOTO-001
 */
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
    // Keep the last-shown photo in RAM for an instant re-entry next visit;
    // free whatever was previously persisted. pending (prefetched next) is
    // discarded — it would be re-prefetched after the persisted one shows.
    if (r->shown_dsc) {
        if (s_persist_dsc) image_decode_free(s_persist_dsc);
        s_persist_dsc   = r->shown_dsc;
        s_persist_index = r->shown_index;
    }
    if (r->pending_dsc) image_decode_free(r->pending_dsc);
    heap_caps_free(r->paths);
    lv_free(r);
    ESP_LOGI(TAG, "ImageRotator destroyed (persisted [%d])", s_persist_index);
}

/**
 * @brief Advance the current index to the next image (wrapping).
 * @version 1
 * @req REQ-PHOTO-001
 */
void image_rotator_step(image_rotator_t *r)
{
    if (!r || r->count == 0) return;
    r->current_index = (r->current_index + 1) % r->count;
}

// Decode image `idx` into pending_dsc (off-lock). No-op if pending already holds
// it (prefetch hit) — that makes the next present() an instant swap.
/**
 * @brief Load image @p idx into the pending slot off the LVGL lock: prefetch hit, RAM-cache hit, .r565 cache file, or PNG decode + cache write.
 * v2: no flip after decode (decode is pinned top-down; existing .r565 caches are top-down).
 * v3: through image_decode_cached_rgb565 (a slot; the cache checked against its PNG).
 * @version 3
 * @req REQ-PHOTO-001
 */
static void decode_into_pending(image_rotator_t *r, int idx)
{
    if (!r || r->count == 0) return;
    if (r->pending_dsc && r->pending_index == idx) return;   // already prefetched

    // Cross-visit RAM hit: reuse the persisted descriptor (ownership moves to
    // pending; the static slot is cleared so it isn't double-freed).
    if (s_persist_dsc && s_persist_index == idx) {
        if (r->pending_dsc) image_decode_free(r->pending_dsc);
        r->pending_dsc   = s_persist_dsc;
        r->pending_index = idx;
        s_persist_dsc    = NULL;
        s_persist_index  = -1;
        ESP_LOGI(TAG, "RAM-cache hit [%d] %s", idx, r->paths[idx]);
        return;
    }

    if (r->pending_dsc) { image_decode_free(r->pending_dsc); r->pending_dsc = NULL; }
    r->pending_index = -1;

    char posix[MAX_PATH_LEN];
    lvgl_to_posix(r->paths[idx], posix, sizeof(posix));
    int64_t t0 = esp_timer_get_time();

    // Its .r565 cache (made from the PNG when missing or older), into a reserved slot.
    lv_image_dsc_t *dsc = image_decode_cached_rgb565(posix);
    if (!dsc) {
        ESP_LOGW(TAG, "decode failed [%d] %s", idx, posix);
        return;
    }
    r->pending_dsc   = dsc;
    r->pending_index = idx;
    ESP_LOGI(TAG, "Loaded [%d] %s (%lld ms)", idx, r->paths[idx],
             (esp_timer_get_time() - t0) / 1000);
}

/**
 * @brief Decode the current image into the pending slot (off the LVGL lock).
 * @version 1
 * @req REQ-PHOTO-001
 */
void image_rotator_decode_current(image_rotator_t *r)
{
    if (r) decode_into_pending(r, r->current_index);
}

// Prefetch the NEXT image (off-lock) so the upcoming advance is an instant swap.
/**
 * @brief Prefetch the next image into the pending slot (off the LVGL lock) so the next advance is an instant swap.
 * v2: waits for the first photo's fade to end: a decode during it starves the render.
 * @version 2
 * @req REQ-PHOTO-001
 */
void image_rotator_prefetch_next(image_rotator_t *r)
{
    if (!r || r->count == 0) return;
    int64_t wait_us = r->fade_end_us - esp_timer_get_time();
    if (wait_us > 0) vTaskDelay(pdMS_TO_TICKS(wait_us / 1000 + 1));
    decode_into_pending(r, (r->current_index + 1) % r->count);
}

/**
 * @brief Swap the pending descriptor in if it holds the current image (under the LVGL lock); fades the image in over 1.5 s on first present.
 * v2: 1.5 s, eased in and out (was 600 ms linear); notes when it ends for the prefetch.
 * @version 2
 * @req REQ-PHOTO-001
 */
void image_rotator_present(image_rotator_t *r)
{
    // Only present if pending holds the image we currently want to show.
    if (!r || !r->pending_dsc || r->pending_index != r->current_index) return;

    // Swap in the freshly decoded descriptor; free the previously shown one only
    // after LVGL is pointed at the new buffer.
    lv_image_set_src(r->img_current, r->pending_dsc);
    if (r->shown_dsc) image_decode_free(r->shown_dsc);
    r->shown_dsc     = r->pending_dsc;
    r->shown_index   = r->pending_index;
    r->pending_dsc   = NULL;
    r->pending_index = -1;
    lv_obj_align(r->img_current, LV_ALIGN_CENTER, 0, 0);

    if (!r->faded_in) {
        r->faded_in = true;
        // Fade the IMAGE in over the opaque black container — NOT the container
        // itself. Fading the container would make it transparent and briefly
        // reveal the clock background behind it ("black → clock bg → photo").
        lv_obj_set_style_opa(r->img_current, LV_OPA_TRANSP, 0);
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, r->img_current);
        lv_anim_set_values(&a, LV_OPA_TRANSP, LV_OPA_COVER);
        lv_anim_set_duration(&a, FADE_IN_MS);
        lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
        lv_anim_set_exec_cb(&a, opa_anim_cb);
        lv_anim_start(&a);
        r->fade_end_us = esp_timer_get_time() + (int64_t)FADE_IN_MS * 1000;
    }
    ESP_LOGI(TAG, "Present [%d] %s", r->current_index, r->paths[r->current_index]);
}

/**
 * @brief Get the descriptor currently shown.
 * @version 1
 * @req REQ-PHOTO-001
 * @return Shown descriptor, or NULL.
 */
const lv_image_dsc_t *image_rotator_shown_dsc(const image_rotator_t *r)
{
    return r ? r->shown_dsc : NULL;
}

/**
 * @brief Get the number of images found.
 * @version 1
 * @req REQ-PHOTO-001
 * @return Image count, or 0 if @p r is NULL.
 */
int image_rotator_count(const image_rotator_t *r)
{
    return r ? r->count : 0;
}

/**
 * @brief Get the current image index.
 * @version 1
 * @req REQ-PHOTO-001
 * @return Current index, or 0 if @p r is NULL.
 */
int image_rotator_index(const image_rotator_t *r)
{
    return r ? r->current_index : 0;
}
