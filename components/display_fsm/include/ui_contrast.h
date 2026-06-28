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

// `desired`, lightened along OKLCH-L until it meets UI_CONTRAST_AA against bg;
// falls back to ui_text_on(bg) if the hue cannot be made legible.
lv_color_t ui_legible(lv_color_t desired, lv_color_t bg);

#ifdef __cplusplus
}
#endif
