/**
 * @file image_pool.c
 * @version 1
 * @brief Full-screen slots and in-place pixel conversions (see image_pool.h). Pure C.
 */
#include "image_pool.h"

#include <string.h>

/**
 * @brief Set the slots up.
 * @version 1
 * @req REQ-BG-001
 */
void image_pool_setup(image_pool_t *p, uint8_t *const *bufs, int n, size_t cap)
{
    memset(p, 0, sizeof(*p));
    p->n = n < IMAGE_POOL_SLOTS ? n : IMAGE_POOL_SLOTS;
    p->cap = cap;
    for (int i = 0; i < p->n; i++) p->buf[i] = bufs[i];
}

/**
 * @brief Take a slot.
 * @version 1
 * @req REQ-BG-001
 * @return Its buffer, or NULL.
 */
uint8_t *image_pool_take(image_pool_t *p, size_t size)
{
    int i = 0;
    while (i < p->n && (p->used & (1u << i))) i++;
    bool ok = size <= p->cap && i < p->n;
    if (ok) p->used |= 1u << i;
    return ok ? p->buf[i] : NULL;
}

/**
 * @brief Give a slot back.
 * @version 1
 * @req REQ-BG-001
 * @return Whether it was one.
 */
bool image_pool_give(image_pool_t *p, const void *ptr)
{
    int i = 0;
    while (i < p->n && p->buf[i] != ptr) i++;
    bool slot = i < p->n && (p->used & (1u << i));
    if (slot) p->used &= ~(1u << i);
    return slot;
}

/**
 * @brief Free slots.
 * @version 1
 * @req REQ-BG-001
 * @return The count.
 */
int image_pool_free_count(const image_pool_t *p)
{
    int k = 0;
    for (int i = 0; i < p->n; i++) k += (p->used & (1u << i)) ? 0 : 1;
    return k;
}

/**
 * @brief Swap R and B in place.
 * @version 1
 * @req REQ-BG-001
 */
void image_px_swap_rb(uint8_t *px, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        uint8_t r = px[i * 4];
        px[i * 4] = px[i * 4 + 2];
        px[i * 4 + 2] = r;
    }
}

/**
 * @brief RGBA to RGB565 in the same buffer.
 * @version 1
 * @req REQ-BG-001
 */
void image_px_rgba_to_rgb565_inplace(uint8_t *buf, int w, int h, uint32_t dstride)
{
    for (int y = 0; y < h; y++) {
        const uint8_t *s = buf + (size_t)y * (size_t)w * 4;
        uint8_t *d = buf + (size_t)y * dstride;
        for (int x = 0; x < w; x++) {
            uint8_t r = s[x * 4], g = s[x * 4 + 1], b = s[x * 4 + 2];   // read before the write
            uint16_t v = (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
            memcpy(d + x * 2, &v, 2);
        }
    }
}
