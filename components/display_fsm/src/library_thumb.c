/**
 * @file library_thumb.c
 * @version 1
 * @brief Thumbnails as LVGL images (see library_thumb.h): LVGL's lodepng decodes into an
 *        lv_draw_buf_t (RGBA), swapped in place to LVGL's BGRA; the draw buffer is the image.
 */
#include "library_thumb.h"
#include "library.h"
#include "src/libs/lodepng/lodepng.h"

#include <string.h>

/**
 * @brief A new thumbnail @p w x @p h, ARGB8888 (to paint into).
 * @version 1
 * @req REQ-LIB-001
 * @return It, or NULL.
 */
lv_image_dsc_t *library_thumb_new(uint32_t w, uint32_t h)
{
    return (lv_image_dsc_t *)lv_draw_buf_create(w, h, LV_COLOR_FORMAT_ARGB8888, 0);
}

/**
 * @brief Decode a thumbnail.
 * @version 1
 * @req REQ-LIB-001
 * @return It, or NULL.
 */
lv_image_dsc_t *library_thumb_decode(const uint8_t *png, size_t len)
{
    unsigned char *out = NULL;   // LVGL's lodepng: an lv_draw_buf_t, RGBA
    unsigned w = 0, h = 0;
    bool ok = png && len && lodepng_decode32(&out, &w, &h, png, len) == 0 && out &&
              w <= 2 * LIB_THUMB_W && h <= 2 * LIB_THUMB_H;
    lv_draw_buf_t *buf = (lv_draw_buf_t *)out;
    for (uint32_t y = 0; ok && y < h; y++) {
        uint8_t *row = buf->data + (size_t)y * buf->header.stride;
        for (uint32_t x = 0; x < w; x++) {   // RGBA to LVGL's BGRA
            uint8_t r = row[x * 4];
            row[x * 4] = row[x * 4 + 2];
            row[x * 4 + 2] = r;
        }
    }
    if (!ok && buf) lv_draw_buf_destroy(buf);
    return ok ? (lv_image_dsc_t *)buf : NULL;
}

/**
 * @brief Free a thumbnail.
 * @version 1
 * @req REQ-LIB-001
 */
void library_thumb_free(lv_image_dsc_t *thumb)
{
    if (thumb) lv_draw_buf_destroy((lv_draw_buf_t *)thumb);
}
