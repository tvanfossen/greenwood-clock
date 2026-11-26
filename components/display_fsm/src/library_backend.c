/**
 * @file library_backend.c
 * @version 1
 * @brief The clock's Library calls (see library_backend.h). Called under the LVGL lock; each
 *        releases it around the SD card.
 */
#include "library_backend.h"
#include "library_store.h"
#include "library_thumb.h"
#include "theme_store.h"
#include "display_fsm.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"

#include <stdio.h>
#include <stdlib.h>

static const char *TAG = "library_be";

#define THUMB_FILE_MAX (64 * 1024)   // a thumbnail PNG's largest

/**
 * @brief What a kind holds.
 * @version 1
 * @callback
 */
static void be_list(lib_kind_t kind, settings_library_cb_t done, void *ctx)
{
    lib_item_t *items = heap_caps_malloc(sizeof(lib_item_t) * LIB_ALBUM_MAX, MALLOC_CAP_SPIRAM);
    uint32_t total = 0, free_mb = 0;
    int n = 0;
    lvgl_port_unlock();
    if (items) n = library_store_list(kind, items, LIB_ALBUM_MAX);
    library_store_storage(&total, &free_mb);
    lvgl_port_lock(0);
    if (!items) ESP_LOGE(TAG, "list: out of memory");
    done(items != NULL, items ? "" : "Out of memory", items, n, total, free_mb, ctx);
    heap_caps_free(items);
}

/**
 * @brief Read file @p path (at most @p max bytes) into a new buffer.
 * @version 1
 * @utility
 * @return The buffer (heap_caps_free it) and its length in @p len, or NULL.
 */
static uint8_t *read_file(const char *path, size_t max, size_t *len)
{
    FILE *f = fopen(path, "rb");
    uint8_t *buf = f ? heap_caps_malloc(max, MALLOC_CAP_SPIRAM) : NULL;
    *len = buf ? fread(buf, 1, max, f) : 0;
    if (f) fclose(f);
    if (buf && (*len == 0 || *len == max)) {   // empty, or larger than a thumbnail
        heap_caps_free(buf);
        buf = NULL;
    }
    return buf;
}

/**
 * @brief A picture's thumbnail.
 * @version 1
 * @callback
 */
static void be_thumb(lib_kind_t kind, const char *name, settings_thumb_cb_t done, void *ctx)
{
    char path[160];
    size_t len = 0;
    lvgl_port_unlock();
    uint8_t *png = library_store_thumb_path(kind, name, path, sizeof(path)) ? read_file(path, THUMB_FILE_MAX, &len) : NULL;
    lvgl_port_lock(0);
    lv_image_dsc_t *d = png ? library_thumb_decode(png, len) : NULL;   // small: under the lock
    heap_caps_free(png);
    done(d, ctx);
}

/**
 * @brief Delete an item.
 * @version 1
 * @callback
 */
static void be_delete(lib_kind_t kind, const char *name, settings_done_cb_t done, void *ctx)
{
    char why[128] = "";
    lvgl_port_unlock();
    esp_err_t err = library_store_delete(kind, name, why, sizeof(why));
    lvgl_port_lock(0);
    done(err == ESP_OK, why, ctx);
}

/**
 * @brief Rename an item; the profiles follow, and the background with them.
 * @version 1
 * @callback
 */
static void be_rename(lib_kind_t kind, const char *from, const char *to, settings_done_cb_t done,
                      void *ctx)
{
    char why[128] = "";
    lvgl_port_unlock();
    esp_err_t err = library_store_rename(kind, from, to, why, sizeof(why));
    lvgl_port_lock(0);
    if (err == ESP_OK) display_fsm_refresh_background();
    done(err == ESP_OK, why, ctx);
}

/**
 * @brief Read a theme file.
 * @version 1
 * @callback
 */
static void be_theme_load(const char *stem, settings_theme_cb_t done, void *ctx)
{
    static ui_theme_t t;
    lvgl_port_unlock();
    bool ok = theme_store_load(stem, &t);
    lvgl_port_lock(0);
    if (!ok) ESP_LOGW(TAG, "theme %s: could not read it", stem);
    done(ok, &t, ctx);
}

/**
 * @brief Fill the backend's Library calls.
 * @version 1
 * @req REQ-LIB-001
 */
void library_backend_fill(settings_backend_t *be)
{
    be->library_list = be_list;
    be->library_thumb = be_thumb;
    be->library_delete = be_delete;
    be->library_rename = be_rename;
    be->theme_load = be_theme_load;
}
