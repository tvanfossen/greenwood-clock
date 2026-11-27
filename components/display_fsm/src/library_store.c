/**
 * @file library_store.c
 * @version 1
 * @brief The Library on the SD card (see library_store.h).
 */
#include "library_store.h"
#include "sched_store.h"
#include "theme_store.h"
#include "image_decode.h"
#include "display_fsm.h"
#include "display_events.h"
#include "sdcard.h"
#include "clock_anim_pref.h"
#include "src/libs/lodepng/lodepng.h"

#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

static const char *TAG = "library";

#define THUMB_DIR   ".thumbs"
#define THUMB_GAP_MS   1000       // between pictures: the display, Wi-Fi and SD keep their share
#define MAKER_DELAY_MS (120 * 1000)   // after start: let the boot's fetches and decodes settle

static TaskHandle_t s_maker;

/**
 * @brief A schedule-sized scratch copy of the profiles, in PSRAM (allocated once; internal
 *        RAM is short). SD-side callers only: one at a time (the HTTP task or the settings).
 * @version 1
 * @utility
 * @return It, or NULL.
 */
static sched_rule_t *scratch_rules(void)
{
    static sched_rule_t *s_rules;
    if (!s_rules) s_rules = heap_caps_calloc(SCHED_MAX_PROFILES, sizeof(sched_rule_t), MALLOC_CAP_SPIRAM);
    if (!s_rules) ESP_LOGE(TAG, "profiles: out of memory");
    return s_rules;
}

/**
 * @brief The folder of @p kind's files ("/sdcard/backgrounds"), NULL for animations.
 * @version 1
 * @utility
 * @return It.
 */
static const char *folder(lib_kind_t kind)
{
    static const char *const DIRS[LIB_KINDS] = { "/sdcard/backgrounds", "/sdcard/photos",
                                                 "/sdcard/themes", NULL };
    return kind < LIB_KINDS ? DIRS[kind] : NULL;
}

/**
 * @brief The extension of @p kind's files.
 * @version 1
 * @utility
 * @return ".png" or ".json".
 */
static const char *ext(lib_kind_t kind)
{
    return kind == LIB_THEMES ? ".json" : ".png";
}

/**
 * @brief The file of item @p name into @p out.
 * @version 1
 * @utility
 * @return Whether the kind has files.
 */
static bool item_path(lib_kind_t kind, const char *name, char *out, size_t n)
{
    const char *dir = folder(kind);
    if (dir) snprintf(out, n, "%s/%s%s", dir, name, ext(kind));
    return dir != NULL;
}

/**
 * @brief The thumbnail file of @p name.
 * @version 1
 * @req REQ-LIB-001
 * @return Whether there is one.
 */
bool library_store_thumb_path(lib_kind_t kind, const char *name, char *path, size_t n)
{
    struct stat st;
    bool pictures = kind == LIB_BACKGROUNDS || kind == LIB_ALBUM;
    if (pictures) snprintf(path, n, "%s/" THUMB_DIR "/%s.png", folder(kind), name);
    return pictures && lib_name_ok(name) && stat(path, &st) == 0;
}

// ---------------------------------------------------------------------------
// Thumbnails
// ---------------------------------------------------------------------------

/**
 * @brief Make the thumbnail @p dst of picture @p src (decode, average, PNG).
 * @version 1
 * @utility
 * @return Whether it was written.
 */
static bool make_thumb(const char *src, const char *dst)
{
    lv_image_dsc_t *pic = image_decode_png_file(src);
    uint8_t *rgba = pic ? heap_caps_malloc(LIB_THUMB_W * LIB_THUMB_H * 4, MALLOC_CAP_SPIRAM) : NULL;
    unsigned char *png = NULL;
    size_t len = 0;
    bool ok = rgba != NULL;
    if (ok) {
        lib_thumb_scale(pic->data, (int)pic->header.w, (int)pic->header.h, (int)pic->header.stride,
                        rgba, LIB_THUMB_W, LIB_THUMB_H);
        ok = lodepng_encode32(&png, &len, rgba, LIB_THUMB_W, LIB_THUMB_H) == 0;
    }
    char tmp[160];
    snprintf(tmp, sizeof(tmp), "%s.tmp", dst);
    FILE *f = ok ? fopen(tmp, "wb") : NULL;
    ok = f && fwrite(png, 1, len, f) == len;
    if (f) ok = fclose(f) == 0 && ok;
    if (ok) {
        remove(dst);   // FAT's rename does not replace
        ok = rename(tmp, dst) == 0;
    }
    if (!ok) remove(tmp);
    lv_free(png);   // lodepng allocates with lv_malloc
    heap_caps_free(rgba);
    if (pic) image_decode_free(pic);
    return ok;
}

/**
 * @brief Whether picture @p src needs its thumbnail @p dst (missing or older).
 * @version 1
 * @utility
 * @return Whether.
 */
static bool thumb_stale(const char *src, const char *dst)
{
    struct stat s, t;
    return stat(src, &s) == 0 && (stat(dst, &t) != 0 || t.st_mtime < s.st_mtime);
}

/**
 * @brief The stem of file @p fname if it ends in @p want, into @p out.
 * @version 1
 * @utility
 * @return Whether it does (and the stem is a safe name).
 */
static bool stem_of(const char *fname, const char *want, char *out, size_t n)
{
    size_t len = strlen(fname), e = strlen(want);
    bool ok = len > e && len - e < n && strcasecmp(fname + len - e, want) == 0;
    if (ok) {
        memcpy(out, fname, len - e);
        out[len - e] = '\0';
    }
    return ok && lib_name_ok(out);
}

/**
 * @brief Make the missing thumbnails (and .r565 caches) of @p kind's pictures.
 * v2: also the picture's .r565 cache, so showing it never needs the decoder's peak.
 * @version 2
 * @utility
 * @return How many were made.
 */
static int thumbs_for(lib_kind_t kind)
{
    char dirpath[64];
    snprintf(dirpath, sizeof(dirpath), "%s/" THUMB_DIR, folder(kind));
    mkdir(dirpath, 0775);
    DIR *d = opendir(folder(kind));
    struct dirent *e;
    int made = 0;
    while (d && (e = readdir(d)) != NULL) {
        char name[LIB_NAME_MAX + 8], src[160], dst[160];
        if (e->d_type != DT_REG || !stem_of(e->d_name, ".png", name, sizeof(name))) continue;
        item_path(kind, name, src, sizeof(src));
        snprintf(dst, sizeof(dst), "%s/%s.png", dirpath, name);
        if (!image_decode_make_cache_rgb565(src)) ESP_LOGW(TAG, "%s: no .r565 cache yet", name);
        if (!thumb_stale(src, dst)) continue;
        made += make_thumb(src, dst) ? 1 : 0;
        vTaskDelay(pdMS_TO_TICKS(THUMB_GAP_MS));
    }
    if (d) closedir(d);
    return made;
}

/**
 * @brief The thumbnail maker: make what is missing, then wait to be told of a change.
 * v2: starts 2 minutes after the display (a burst of SD writes at boot starved the Wi-Fi link's DMA memory).
 * @version 2
 * @callback
 */
static void maker_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(MAKER_DELAY_MS));
    for (;;) {
        int made = sdcard_is_mounted() ? thumbs_for(LIB_BACKGROUNDS) + thumbs_for(LIB_ALBUM) : 0;
        if (made) ESP_LOGI(TAG, "%d thumbnail(s) made", made);
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        vTaskDelay(pdMS_TO_TICKS(500));   // let an upload's rename land
    }
}

/**
 * @brief Start the thumbnail maker.
 * @version 1
 * @req REQ-LIB-001
 */
void library_store_start(void)
{
    if (s_maker) return;
    if (xTaskCreate(maker_task, "lib_thumbs", 12288, NULL, 1, &s_maker) != pdPASS) {
        ESP_LOGE(TAG, "thumbnail task: could not start");
        s_maker = NULL;
    }
}

/**
 * @brief Something changed: make its thumbnail soon.
 * @version 1
 * @req REQ-LIB-001
 */
void library_store_changed(void)
{
    if (s_maker) xTaskNotifyGive(s_maker);
}

// ---------------------------------------------------------------------------
// Listing
// ---------------------------------------------------------------------------

/**
 * @brief The files of @p kind into @p out.
 * @version 1
 * @utility
 * @return How many.
 */
static int list_files(lib_kind_t kind, lib_item_t *out, int max)
{
    DIR *d = opendir(folder(kind));
    struct dirent *e;
    int n = 0;
    while (d && n < max && (e = readdir(d)) != NULL) {
        lib_item_t *it = &out[n];
        memset(it, 0, sizeof(*it));
        if (e->d_type != DT_REG || !stem_of(e->d_name, ext(kind), it->name, sizeof(it->name))) continue;
        char path[160], tpath[160];
        struct stat st;
        item_path(kind, it->name, path, sizeof(path));
        if (stat(path, &st) == 0) {
            it->size = (uint32_t)st.st_size;
            it->mtime = (uint32_t)st.st_mtime;
        }
        const char *label = kind == LIB_THEMES ? theme_store_name(it->name) : NULL;
        snprintf(it->label, sizeof(it->label), "%s", label ? label : it->name);
        it->thumb = library_store_thumb_path(kind, it->name, tpath, sizeof(tpath));
        n++;
    }
    if (d) closedir(d);
    return n;
}

/**
 * @brief How many items a kind holds.
 * @version 1
 * @req REQ-LIB-001
 * @return The count.
 */
int library_store_count(lib_kind_t kind)
{
    if (kind == LIB_ANIMS) return CLOCK_ANIM_COUNT;
    DIR *d = sdcard_is_mounted() ? opendir(folder(kind)) : NULL;
    struct dirent *e;
    int n = 0;
    char stem[LIB_NAME_MAX + 8];
    while (d && (e = readdir(d)) != NULL) {
        n += e->d_type == DT_REG && stem_of(e->d_name, ext(kind), stem, sizeof(stem)) ? 1 : 0;
    }
    if (d) closedir(d);
    return n;
}

/**
 * @brief What a kind holds.
 * @version 1
 * @req REQ-LIB-001
 * @return How many.
 */
int library_store_list(lib_kind_t kind, lib_item_t *out, int max)
{
    if (kind != LIB_ANIMS) return sdcard_is_mounted() ? list_files(kind, out, max) : 0;
    int n = 0;
    for (int i = 0; i < CLOCK_ANIM_COUNT && n < max; i++, n++) {
        memset(&out[n], 0, sizeof(out[n]));
        snprintf(out[n].name, sizeof(out[n].name), "%s", clock_anim_name(CLOCK_ANIM_MENU_ORDER[i]));
        snprintf(out[n].label, sizeof(out[n].label), "%s", out[n].name);
    }
    return n;
}

// ---------------------------------------------------------------------------
// Deleting and renaming
// ---------------------------------------------------------------------------

/**
 * @brief Whether a profile uses @p name; @p why names them.
 * @version 1
 * @utility
 * @return Whether.
 */
static bool in_use(lib_kind_t kind, const char *name, char *why, size_t n)
{
    sched_rule_t *r = scratch_rules();
    const char *who[4];
    char ref[SCHED_IMAGE_LEN];
    lib_ref(kind, name, ref, sizeof(ref));
    int k = r ? lib_used_by(r, sched_store_get(r, SCHED_MAX_PROFILES), kind, ref, who, 4) : 0;
    if (k && why) {
        size_t at = (size_t)snprintf(why, n, "Used by %s", who[0]);
        for (int i = 1; i < k && i < 4 && at < n; i++) at += (size_t)snprintf(why + at, n - at, ", %s", who[i]);
        if (k > 4 && at < n) snprintf(why + at, n - at, " and %d more", k - 4);
    }
    return k > 0;
}

/**
 * @brief Tell the clock its content changed: the album rescans; themes rescan.
 * @version 1
 * @utility
 */
static void changed(lib_kind_t kind)
{
    display_event_t evt = { .type = kind == LIB_ALBUM ? DISPLAY_EVT_PHOTOS_CHANGED : DISPLAY_EVT_REFRESH_THEME };
    if (kind == LIB_ALBUM || kind == LIB_THEMES) display_fsm_send_event(&evt);
}

/**
 * @brief Delete an item.
 * v2: also its .r565 cache.
 * @version 2
 * @req REQ-LIB-001
 * @return ESP_OK, ESP_ERR_INVALID_STATE, or ESP_FAIL.
 */
esp_err_t library_store_delete(lib_kind_t kind, const char *name, char *why, size_t n)
{
    char path[160];
    esp_err_t err = ESP_OK;
    if (!lib_name_ok(name) || !item_path(kind, name, path, sizeof(path))) {
        snprintf(why, n, "%s", kind == LIB_ANIMS ? "Clock animations are built in" : "No such item");
        err = ESP_ERR_INVALID_STATE;
    } else if (kind == LIB_THEMES && strcmp(name, "default") == 0) {
        snprintf(why, n, "The Default theme is the clock's own");
        err = ESP_ERR_INVALID_STATE;
    } else if (in_use(kind, name, why, n)) {
        err = ESP_ERR_INVALID_STATE;
    }
    if (err == ESP_OK && remove(path) != 0) {
        snprintf(why, n, "Could not delete it");
        err = ESP_FAIL;
    }
    char thumb[160];
    if (err == ESP_OK && library_store_thumb_path(kind, name, thumb, sizeof(thumb))) remove(thumb);
    if (err == ESP_OK) image_decode_drop_cache(path);
    if (err == ESP_OK) changed(kind);
    ESP_LOGI(TAG, "delete %s: %s", name, err == ESP_OK ? "done" : why);
    return err;
}

/**
 * @brief Point the profiles that use @p from at @p to.
 * @version 1
 * @utility
 * @return ESP_OK, or the schedule's error.
 */
static esp_err_t follow_rename(lib_kind_t kind, const char *from, const char *to)
{
    sched_rule_t *r = scratch_rules();
    char a[SCHED_IMAGE_LEN], b[SCHED_IMAGE_LEN], why[96];
    lib_ref(kind, from, a, sizeof(a));
    lib_ref(kind, to, b, sizeof(b));
    int n = r ? sched_store_get(r, SCHED_MAX_PROFILES) : 0;
    return r && lib_rename_refs(r, n, kind, a, b) ? sched_store_set(r, n, why, sizeof(why)) : ESP_OK;
}

/**
 * @brief Whether @p from may become @p to: safe names, not the Default theme, a kind with
 *        files, the new name free; their files into @p a and @p b. @p why says why not.
 * @version 1
 * @utility
 * @return ESP_OK, ESP_ERR_INVALID_ARG or ESP_ERR_INVALID_STATE.
 */
static esp_err_t rename_check(lib_kind_t kind, const char *from, const char *to, char *a, char *b,
                              char *why, size_t n)
{
    struct stat st;
    bool fixed = kind == LIB_THEMES && (strcmp(from, "default") == 0 || strcmp(to, "default") == 0);
    esp_err_t err = ESP_OK;
    if (!lib_name_ok(to)) {
        snprintf(why, n, "Letters, digits, - and _ only (up to %d)", LIB_NAME_MAX);
        err = ESP_ERR_INVALID_ARG;
    } else if (fixed || !lib_name_ok(from) || !item_path(kind, from, a, 160)) {
        snprintf(why, n, "That cannot be renamed");
        err = ESP_ERR_INVALID_STATE;
    } else if (item_path(kind, to, b, 160) && stat(b, &st) == 0) {
        snprintf(why, n, "That name is taken");
        err = ESP_ERR_INVALID_ARG;
    }
    return err;
}

/**
 * @brief Rename an item; the profiles follow.
 * v2: the checks are rename_check.
 * v3: drops the old name's .r565 cache (made again under the new one).
 * @version 3
 * @req REQ-LIB-001
 * @return ESP_OK, ESP_ERR_INVALID_ARG, ESP_ERR_INVALID_STATE, or ESP_FAIL.
 */
esp_err_t library_store_rename(lib_kind_t kind, const char *from, const char *to, char *why,
                               size_t n)
{
    char a[160], b[160], ta[160], tb[160];
    esp_err_t err = rename_check(kind, from, to, a, b, why, n);
    if (err == ESP_OK && rename(a, b) != 0) {
        snprintf(why, n, "Could not rename it");
        err = ESP_FAIL;
    }
    if (err == ESP_OK) image_decode_drop_cache(a);   // made again under the new name
    if (err == ESP_OK && library_store_thumb_path(kind, from, ta, sizeof(ta))) {
        library_store_thumb_path(kind, to, tb, sizeof(tb));   // the path, whether or not it exists
        rename(ta, tb);
    }
    if (err == ESP_OK) err = follow_rename(kind, from, to);
    if (err == ESP_OK) changed(kind);
    ESP_LOGI(TAG, "rename %s -> %s: %s", from, to, err == ESP_OK ? "done" : why);
    return err;
}

/**
 * @brief The card's size and free space.
 * @version 1
 * @req REQ-LIB-001
 */
void library_store_storage(uint32_t *total_mb, uint32_t *free_mb)
{
    sdcard_info_t info;
    memset(&info, 0, sizeof(info));
    bool ok = sdcard_get_info(&info) == ESP_OK;
    *total_mb = ok ? (uint32_t)(info.total_bytes >> 20) : 0;
    *free_mb = ok ? (uint32_t)((info.total_bytes - info.used_bytes) >> 20) : 0;
}
