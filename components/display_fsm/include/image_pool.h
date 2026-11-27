/**
 * @file image_pool.h
 * @version 1
 * @brief Fixed slots for full-screen pictures, and the in-place pixel conversions that let a
 *        decode need one big buffer instead of two. The slots are allocated once (at display
 *        start), so a profile change or the next album photo never asks the heap for a
 *        multi-MB block after the PSRAM has fragmented (seen on device: 13 MB free, no block
 *        of 2.5 MB). Pure C (the caller supplies the buffers and any locking): host-tested.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define IMAGE_POOL_SLOTS 4   /**< Background current + incoming, album shown + next. */

/** The slots: their buffers, each @c cap bytes, and which are in use. */
typedef struct {
    uint8_t *buf[IMAGE_POOL_SLOTS];
    size_t   cap;
    int      n;
    uint32_t used;   /**< Bit i: slot i is taken. */
} image_pool_t;

/**
 * @brief Set @p p up over @p n buffers (@p bufs) of @p cap bytes each, all free.
 * @version 1
 * @req REQ-BG-001
 */
void image_pool_setup(image_pool_t *p, uint8_t *const *bufs, int n, size_t cap);

/**
 * @brief Take a free slot for @p size bytes.
 * @version 1
 * @req REQ-BG-001
 * @return Its buffer, or NULL (none free, or @p size larger than a slot).
 */
uint8_t *image_pool_take(image_pool_t *p, size_t size);

/**
 * @brief Give back the slot holding @p ptr.
 * @version 1
 * @req REQ-BG-001
 * @return Whether @p ptr was a slot (else the caller frees it as heap).
 */
bool image_pool_give(image_pool_t *p, const void *ptr);

/**
 * @brief How many slots are free.
 * @version 1
 * @req REQ-BG-001
 * @return The count.
 */
int image_pool_free_count(const image_pool_t *p);

/**
 * @brief Swap R and B of @p n RGBA pixels in place (RGBA <-> LVGL's BGRA).
 * @version 1
 * @req REQ-BG-001
 */
void image_px_swap_rb(uint8_t *px, size_t n);

/**
 * @brief Pack @p w x @p h RGBA pixels (rows of w * 4 bytes) into RGB565 rows of @p dstride
 *        bytes, in the same buffer (dstride <= w * 4: each write lands at or before what is
 *        still to be read).
 * @version 1
 * @req REQ-BG-001
 */
void image_px_rgba_to_rgb565_inplace(uint8_t *buf, int w, int h, uint32_t dstride);

#ifdef __cplusplus
}
#endif
