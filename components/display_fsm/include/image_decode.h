// components/display_fsm/include/image_decode.h
//
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
 * @return Newly allocated descriptor, or NULL on failure (logged).
 */
lv_image_dsc_t *image_decode_png_buf(const uint8_t *png, size_t len);

/**
 * @brief Read a PNG file fully, then decode via image_decode_png_buf().
 *
 * @param path  stdio path (e.g. "/sdcard/backgrounds/foo.png").
 * @return Newly allocated descriptor, or NULL on failure (logged).
 */
lv_image_dsc_t *image_decode_png_file(const char *path);

/**
 * @brief Like image_decode_png_file but produces an opaque RGB565 descriptor.
 *
 * RGB565 matches the framebuffer, so a full-screen background redraws as a
 * straight copy (no per-pixel ARGB->RGB565 conversion) — much cheaper during
 * animations. Drops alpha; use only for opaque images.
 */
lv_image_dsc_t *image_decode_png_file_rgb565(const char *path);

/**
 * @brief Flip a decoded ARGB8888 descriptor vertically, in place.
 *
 * The ESP32-P4 PPA blit path renders a raw pre-decoded descriptor vertically
 * flipped relative to LVGL's own software-decoded images. Raw-dsc images that
 * must match the upright software-decoded appearance (clock background, radar
 * map + overlay) call this after decode. Re-syncs the cache.
 */
void image_decode_flip_vertical(lv_image_dsc_t *dsc);

/**
 * @brief Load a raw RGB565 cache file ([u16 w][u16 h][u32 stride][data]) into a
 * persistent SPIRAM descriptor. Fast (fread, no decode). Returns NULL if the
 * file is missing/invalid — caller should then decode the PNG and save a cache.
 */
lv_image_dsc_t *image_decode_load_raw_rgb565(const char *path);

/**
 * @brief Write an RGB565 descriptor to a raw cache file (atomic via temp+rename).
 */
bool image_decode_save_raw_rgb565(const char *path, const lv_image_dsc_t *dsc);

/**
 * @brief Free a descriptor (and its pixel buffer) returned by the above.
 */
void image_decode_free(lv_image_dsc_t *dsc);

#ifdef __cplusplus
}
#endif
