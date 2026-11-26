/**
 * @file library.h
 * @version 1
 * @brief The Library's rules (Settings > Library: backgrounds, the album, themes, clock
 *        animations), apart from the screens and the SD card: which profiles use an item (it
 *        cannot be deleted while any does), the names a renamed item may take, carrying a
 *        rename into the profiles that use it, and shrinking a picture to its thumbnail.
 *        Pure C: host-tested.
 */
#pragma once

#include "sched_rule.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** What the Library holds. */
typedef enum {
    LIB_BACKGROUNDS = 0,   /**< /sdcard/backgrounds/<name>.png; profiles name them by LVGL path. */
    LIB_ALBUM,             /**< /sdcard/photos/<name>.png; no profile names them. */
    LIB_THEMES,            /**< /sdcard/themes/<stem>.json; profiles name them by stem. */
    LIB_ANIMS,             /**< Clock animations; profiles name them. */
    LIB_KINDS
} lib_kind_t;

#define LIB_THUMB_W  170   /**< A thumbnail: the clock's shape, small. */
#define LIB_THUMB_H  100
#define LIB_NAME_MAX 40    /**< A name's longest. */
#define LIB_ALBUM_MAX 200  /**< Photos the album holds. */

/** One thing in the Library. */
typedef struct {
    char     name[LIB_NAME_MAX + 8];   /**< File stem ("winter_morning"), theme stem, or animation. */
    char     label[LIB_NAME_MAX + 8];  /**< What to show (a theme's own name; else the name). */
    uint32_t size;                     /**< Bytes (0 for an animation). */
    uint32_t mtime;                    /**< When it was last changed (epoch s; 0 unknown). */
    bool     thumb;                    /**< A thumbnail exists (backgrounds and the album). */
} lib_item_t;

/**
 * @brief The profiles of @p r (@p n) that use @p value (a background's LVGL path, a theme's
 *        stem, an animation's name; album photos are used by none), by name into @p names.
 * @version 1
 * @req REQ-LIB-001
 * @return How many use it (all counted, at most @p max named).
 */
int lib_used_by(const sched_rule_t *r, int n, lib_kind_t kind, const char *value,
                const char **names, int max);

/**
 * @brief How profiles name item @p name of @p kind into @p out: a background's LVGL path
 *        ("A:/backgrounds/<name>.png"), else the name itself.
 * @version 1
 * @req REQ-LIB-001
 */
void lib_ref(lib_kind_t kind, const char *name, char *out, size_t n);

/**
 * @brief Whether @p name may name an item: 1..LIB_NAME_MAX letters, digits, '-' or '_' (a file
 *        name with no way out of its folder).
 * @version 1
 * @req REQ-LIB-001
 * @return Whether.
 */
bool lib_name_ok(const char *name);

/**
 * @brief Point every profile of @p r (@p n) that uses @p from at @p to instead (a rename).
 * @version 1
 * @req REQ-LIB-001
 * @return How many profiles changed.
 */
int lib_rename_refs(sched_rule_t *r, int n, lib_kind_t kind, const char *from, const char *to);

/**
 * @brief Point profile @p r at @p name of @p kind ("Use in a profile").
 * @version 1
 * @req REQ-LIB-001
 * @return Whether the kind is one a profile names.
 */
bool lib_use_in(sched_rule_t *r, lib_kind_t kind, const char *name);

/**
 * @brief Shrink @p src (BGRA, @p w x @p h, @p stride bytes a row) to @p out (RGBA, @p tw x
 *        @p th) by averaging each thumbnail pixel's area.
 * @version 1
 * @req REQ-LIB-001
 */
void lib_thumb_scale(const uint8_t *src, int w, int h, int stride, uint8_t *out, int tw, int th);

#ifdef __cplusplus
}
#endif
