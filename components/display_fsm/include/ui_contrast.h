/**
 * @file ui_contrast.h
 * v2: OKLCH type and conversions; ui_legible_to takes the contrast target.
 * @version 2
 * @brief Text-legibility helpers: WCAG contrast ratio, black/white pick and OKLCH lightness-adjusted legible colour.
 */
// Text-legibility helpers. Two distinct problems, two tools:
//
//  - ui_text_on(bg): pick black or white — whichever has the higher WCAG
//    contrast against a known solid background. The guaranteed-optimal binary
//    choice (e.g. alert-banner text over a severity colour).
//
//  - ui_legible(desired, bg): the desired colour unchanged when it already clears
//    WCAG AA against bg; otherwise the same OKLCH hue/chroma with its lightness
//    moved only as far as AA needs.
//
// Neither can guarantee legibility over an *image* (varying luminance) — there,
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

// Black or white, whichever contrasts more with bg.
lv_color_t ui_text_on(lv_color_t bg);

// `desired` if it clears AA against bg, else its OKLCH lightness minimally shifted
// to clear it (hue/chroma kept).
lv_color_t ui_legible(lv_color_t desired, lv_color_t bg);

// The same, with any contrast target (e.g. 7.0 for WCAG AAA).
lv_color_t ui_legible_to(lv_color_t desired, lv_color_t bg, float target);

// Mean colour of a rectangular region of an image buffer (RGB565 or ARGB8888),
// coords in BUFFER space. Pair with ui_legible() for legible text over an image:
// sample the region under the text, then ui_legible(desired, mean).
lv_color_t ui_image_region_mean(const lv_image_dsc_t *img,
                                int32_t x, int32_t y, int32_t w, int32_t h);

#ifdef __cplusplus
}
#endif
