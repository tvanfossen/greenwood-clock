/**
 * @file ui_contrast.h
 * v2: OKLCH type and conversions; ui_legible_to takes the contrast target.
 * v3: ui_text_on removed; the region mean takes the caller's fallback.
 * @version 3
 * @brief Text-legibility helpers: WCAG contrast ratio, OKLCH conversions, lightness-adjusted
 *        legible colour and image-region mean.
 */
// Text-legibility helpers.
//
//  - ui_legible(desired, bg): the desired colour unchanged when it already clears
//    WCAG AA against bg; otherwise the same OKLCH hue/chroma with its lightness
//    moved only as far as AA needs.
//
// It cannot guarantee legibility over an *image* (varying luminance) — there,
// use a semi-opaque backing chip or a text outline instead.
#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// WCAG 2.x minimum contrast ratio for normal text (AA): ui_legible()'s target.
#define UI_CONTRAST_AA 4.5f

// An OKLCH colour: lightness 0..1, chroma (0 = grey, ~0.37 = gamut edge), hue in degrees.
typedef struct {
    float l;
    float c;
    float h;
} ui_oklch_t;

// The sRGB colour of an OKLCH colour (clamped to the gamut), and the reverse.
lv_color_t ui_oklch_to_color(ui_oklch_t c);
ui_oklch_t ui_color_to_oklch(lv_color_t c);

// WCAG contrast ratio between two colours: 1.0 (none) .. 21.0 (black/white).
float ui_contrast_ratio(lv_color_t a, lv_color_t b);

// `desired` if it clears AA against bg, else its OKLCH lightness minimally shifted
// to clear it (hue/chroma kept).
lv_color_t ui_legible(lv_color_t desired, lv_color_t bg);

// The same, with any contrast target (e.g. 7.0 for WCAG AAA).
lv_color_t ui_legible_to(lv_color_t desired, lv_color_t bg, float target);

// Mean colour of a rectangular region of an image buffer (RGB565 or ARGB8888),
// coords in BUFFER space. Pair with ui_legible() for legible text over an image:
// sample the region under the text, then ui_legible(desired, mean).
// @p fallback when there is no data or the region is empty.
lv_color_t ui_image_region_mean(const lv_image_dsc_t *img, int32_t x, int32_t y, int32_t w,
                                int32_t h, lv_color_t fallback);

#ifdef __cplusplus
}
#endif
