/**
 * @file library_store.h
 * @version 1
 * @brief The Library on the clock's SD card (REQ-LIB-001): what each kind holds, thumbnails
 *        made once in the background (/sdcard/<folder>/.thumbs/<name>.png, LIB_THUMB_W x
 *        LIB_THUMB_H), deleting (refused while a profile uses the item) and renaming (the
 *        profiles that use it follow). SD access: call off the LVGL lock.
 */
#pragma once

#include "library.h"

#include "esp_err.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Start the thumbnail maker (a low-priority task): it makes the missing ones now and
 *        again after each library_store_changed(). Call once after the launch window.
 * @version 1
 * @req REQ-LIB-001
 */
void library_store_start(void);

/**
 * @brief Something was added or changed (an upload): make its thumbnail soon. Any task.
 * @version 1
 * @req REQ-LIB-001
 */
void library_store_changed(void);

/**
 * @brief What @p kind holds, by name, into @p out (at most @p max).
 * @version 1
 * @req REQ-LIB-001
 * @return How many.
 */
int library_store_list(lib_kind_t kind, lib_item_t *out, int max);

/**
 * @brief How many items @p kind holds (cheaper than a list: for the album's cap).
 * @version 1
 * @req REQ-LIB-001
 * @return The count.
 */
int library_store_count(lib_kind_t kind);

/**
 * @brief The thumbnail file of @p name (a background or album photo) into @p path.
 * @version 1
 * @req REQ-LIB-001
 * @return Whether there is one.
 */
bool library_store_thumb_path(lib_kind_t kind, const char *name, char *path, size_t n);

/**
 * @brief Delete @p name of @p kind (and its thumbnail); refused while a profile uses it, or
 *        for what cannot go (the Default theme, the clock animations). @p why says why not.
 * @version 1
 * @req REQ-LIB-001
 * @return ESP_OK, ESP_ERR_INVALID_STATE (in use / cannot go), or the file error.
 */
esp_err_t library_store_delete(lib_kind_t kind, const char *name, char *why, size_t n);

/**
 * @brief Rename @p from to @p to (a safe name, not taken); the profiles that use it follow.
 * @version 1
 * @req REQ-LIB-001
 * @return ESP_OK, ESP_ERR_INVALID_ARG (the name), ESP_ERR_INVALID_STATE (cannot), or an error.
 */
esp_err_t library_store_rename(lib_kind_t kind, const char *from, const char *to, char *why,
                               size_t n);

/**
 * @brief The card's size and free space, MB.
 * @version 1
 * @req REQ-LIB-001
 */
void library_store_storage(uint32_t *total_mb, uint32_t *free_mb);

#ifdef __cplusplus
}
#endif
