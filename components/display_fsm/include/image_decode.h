/**
 * @file image_decode.h
 * v2: reserved full-screen slots, the shared .r565 cache (image_decode_cached_rgb565).
 * @version 2
 * @brief Shared PNG decoder into persistent SPIRAM LVGL descriptors (ARGB8888/RGB565), avoiding LVGL cache re-decodes.
 */
// Shared PNG → persistent SPIRAM ARGB8888 decoder.
//
// LVGL's on-demand image decoder caches decoded bitmaps and, under memory
// pressure, evicts them and re-decodes from disk on the next draw. For large
// full-screen images (radar map, clock background) that re-decode happens on
// the LVGL render path: an SD round-trip plus a multi-MB allocation that stalls
// the display — and fails outright when internal DRAM is exhausted, leaving the
// image blank. Decoding once into a persistent SPIRAM buffer and handing LVGL a
// static descriptor avoids both the eviction churn and the disk round-trip.
#pragma once

#include "lvgl.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Decode an in-memory PNG into a persistent SPIRAM ARGB8888 descriptor.
 *
 * Output pixels are 64-byte aligned and cache-synced for LVGL/PPA. The result
 * (descriptor + pixel buffer) lives in SPIRAM until image_decode_free().
 *
 * Must be called from a task with a deep stack (~8KB): stb_image PNG inflate is
 * stack-heavy. Do NOT call while holding the LVGL lock — this allocates several
 * MB and is slow.
 *
 * @version 2
 * @req REQ-BG-001
 * @return Newly allocated descriptor, or NULL on failure (logged).
 */
lv_image_dsc_t *image_decode_png_buf(const uint8_t *png, size_t len);

/**
 * @brief Read a PNG file fully, then decode via image_decode_png_buf().
 *
 * @param path  stdio path (e.g. "/sdcard/backgrounds/foo.png").
 * @version 2
 * @req REQ-BG-001
 * @return Newly allocated descriptor, or NULL on failure (logged).
 */
lv_image_dsc_t *image_decode_png_file(const char *path);

/**
 * @brief Like image_decode_png_file but produces an opaque RGB565 descriptor.
 *
 * RGB565 matches the framebuffer, so a full-screen background redraws as a
 * straight copy (no per-pixel ARGB->RGB565 conversion) — much cheaper during
 * animations. Drops alpha; use only for opaque images. Into the heap (showing a picture
 * goes through image_decode_cached_rgb565, which uses the reserved slots).
 * @version 3
 * @req REQ-BG-001
 * @return Newly allocated descriptor, or NULL on failure (logged).
 */
lv_image_dsc_t *image_decode_png_file_rgb565(const char *path);

/**
 * @brief Load a raw RGB565 cache file ([u16 w][u16 h][u32 stride][data]) into a
 * persistent SPIRAM descriptor. Fast (fread, no decode). Returns NULL if the
 * file is missing/invalid — caller should then decode the PNG and save a cache.
 * @version 2
 * @req REQ-PHOTO-001
 * @return The descriptor, or NULL.
 */
lv_image_dsc_t *image_decode_load_raw_rgb565(const char *path);

/**
 * @brief Write an RGB565 descriptor to a raw cache file (atomic via temp+rename).
 * @version 1
 * @req REQ-PHOTO-001
 * @return Whether it was written.
 */
bool image_decode_save_raw_rgb565(const char *path, const lv_image_dsc_t *dsc);

/**
 * @brief Free a descriptor (and its pixel buffer, or its reserved slot) returned by the above.
 * @version 2
 * @req REQ-BG-001
 */
void image_decode_free(lv_image_dsc_t *dsc);

/**
 * @brief Reserve the full-screen picture slots (once, at display start, after the launch
 *        window): background current + incoming, album shown + next.
 * @version 1
 * @req REQ-BG-001
 */
void image_decode_pool_init(void);

/**
 * @brief A full-screen opaque picture (a background, an album photo) for showing: its .r565
 *        cache (made from the PNG when missing or older) in a reserved slot.
 * @version 1
 * @req REQ-BG-001
 * @return New descriptor (free with image_decode_free), or NULL.
 */
lv_image_dsc_t *image_decode_cached_rgb565(const char *png);

/**
 * @brief Make PNG @p png's .r565 cache when missing or stale (off the display path).
 * @version 1
 * @req REQ-LIB-001
 * @return Whether a fresh cache exists now.
 */
bool image_decode_make_cache_rgb565(const char *png);

/**
 * @brief Remove PNG @p png's .r565 cache.
 * @version 1
 * @req REQ-LIB-001
 */
void image_decode_drop_cache(const char *png);

/**
 * @brief Half-resolution copy of an ARGB8888 descriptor (2x2 box filter).
 * @version 1
 * @req REQ-RADAR-003
 * @return New descriptor (free with image_decode_free), or NULL on bad input or OOM.
 */
lv_image_dsc_t *image_decode_halve_argb(const lv_image_dsc_t *src);

#ifdef __cplusplus
}
#endif
