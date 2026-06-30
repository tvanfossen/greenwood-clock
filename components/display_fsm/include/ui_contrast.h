// components/display_fsm/include/ui_contrast.h
//
// Text-legibility helpers. Two distinct problems, two tools:
//
//  - ui_text_on(bg): pick black or white — whichever has the higher WCAG
//    contrast against a known solid background. The guaranteed-optimal binary
//    choice (e.g. alert-banner text over a severity colour).
//
//  - ui_legible(desired, bg): keep a *hue* but raise its perceptual lightness
//    (OKLCH L) until it clears the WCAG AA threshold against bg, falling back
//    to ui_text_on() if even that cannot. Use for coloured accent text on a
//    known panel colour.
//
// Neither can guarantee legibility over an *image* (varying luminance) — there,
// use a semi-opaque backing chip or a text outline instead.
#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// WCAG 2.x minimum contrast ratio for normal text (AA).
#define UI_CONTRAST_AA 4.5f

// WCAG contrast ratio between two colours: 1.0 (none) .. 21.0 (black/white).
float ui_contrast_ratio(lv_color_t a, lv_color_t b);

// Black or white, whichever contrasts more with bg.
lv_color_t ui_text_on(lv_color_t bg);

// `desired`, with its OKLCH-L moved away from bg's (darken over a light bg,
// lighten over a dark one) until it meets UI_CONTRAST_AA; falls back to
// ui_text_on(bg) if the hue cannot be made legible either way.
lv_color_t ui_legible(lv_color_t desired, lv_color_t bg);

// Mean colour of a rectangular region of an image buffer (RGB565 or ARGB8888),
// coords in BUFFER space. Pair with ui_legible() for legible text over an image:
// sample the region under the text, then ui_legible(desired, mean).
lv_color_t ui_image_region_mean(const lv_image_dsc_t *img,
                                int32_t x, int32_t y, int32_t w, int32_t h);

#ifdef __cplusplus
}
#endif
