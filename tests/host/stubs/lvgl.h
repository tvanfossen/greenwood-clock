/**
 * @file lvgl.h
 * @version 1
 * @brief Host-test stand-in for the few LVGL types the pure display helpers use (colours,
 *        image descriptors). Only for tests/host; the firmware uses the real LVGL.
 */
#ifndef HOST_STUB_LVGL_H
#define HOST_STUB_LVGL_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint8_t blue;
    uint8_t green;
    uint8_t red;
} lv_color_t;

typedef enum {
    LV_COLOR_FORMAT_RGB565   = 0x12,
    LV_COLOR_FORMAT_ARGB8888 = 0x10,
    LV_COLOR_FORMAT_XRGB8888 = 0x11,
} lv_color_format_t;

typedef struct {
    uint32_t cf;
    uint32_t w;
    uint32_t h;
    uint32_t stride;
} lv_image_header_t;

typedef struct {
    lv_image_header_t header;
    uint32_t          data_size;
    const uint8_t    *data;
} lv_image_dsc_t;

/**
 * @brief Host stub of lv_color_make.
 * @version 1
 * @utility
 * @return The colour.
 */
static inline lv_color_t lv_color_make(uint8_t r, uint8_t g, uint8_t b)
{
    lv_color_t c = { .blue = b, .green = g, .red = r };
    return c;
}

/**
 * @brief Host stub of lv_color_white.
 * @version 1
 * @utility
 * @return White.
 */
static inline lv_color_t lv_color_white(void) { return lv_color_make(255, 255, 255); }

/**
 * @brief Host stub of lv_color_black.
 * @version 1
 * @utility
 * @return Black.
 */
static inline lv_color_t lv_color_black(void) { return lv_color_make(0, 0, 0); }

/**
 * @brief Host stub of lv_color_hex (0xRRGGBB).
 * @version 1
 * @utility
 * @return The colour.
 */
static inline lv_color_t lv_color_hex(uint32_t c)
{
    return lv_color_make((uint8_t)(c >> 16), (uint8_t)(c >> 8), (uint8_t)c);
}

#endif /* HOST_STUB_LVGL_H */
