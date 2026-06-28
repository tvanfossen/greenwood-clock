// components/display_fsm/src/ui_contrast.c
//
// Text-legibility helpers — see ui_contrast.h.

#include "ui_contrast.h"
#include <math.h>
#include <stdint.h>

// ---------------------------------------------------------------------------
// sRGB transfer + WCAG luminance
// ---------------------------------------------------------------------------

static float srgb_to_linear(float c)
{
    return (c <= 0.04045f) ? (c / 12.92f)
                           : powf((c + 0.055f) / 1.055f, 2.4f);
}

static float linear_to_srgb(float c)
{
    if (c < 0.0f) c = 0.0f;
    if (c > 1.0f) c = 1.0f;
    return (c <= 0.0031308f) ? (12.92f * c)
                             : (1.055f * powf(c, 1.0f / 2.4f) - 0.055f);
}

// WCAG relative luminance (0..1).
static float rel_luminance(lv_color_t c)
{
    float r = srgb_to_linear(c.red   / 255.0f);
    float g = srgb_to_linear(c.green / 255.0f);
    float b = srgb_to_linear(c.blue  / 255.0f);
    return 0.2126f * r + 0.7152f * g + 0.0722f * b;
}

float ui_contrast_ratio(lv_color_t a, lv_color_t b)
{
    float la = rel_luminance(a);
    float lb = rel_luminance(b);
    float hi = fmaxf(la, lb);
    float lo = fminf(la, lb);
    return (hi + 0.05f) / (lo + 0.05f);
}

lv_color_t ui_text_on(lv_color_t bg)
{
    lv_color_t white = lv_color_white();
    lv_color_t black = lv_color_black();
    return (ui_contrast_ratio(white, bg) >= ui_contrast_ratio(black, bg))
               ? white : black;
}

// ---------------------------------------------------------------------------
// OKLab / OKLCH (Björn Ottosson's matrices) — used to raise a hue's lightness
// while preserving its chroma direction.
// ---------------------------------------------------------------------------

static float cube(float x) { return x * x * x; }

static void srgb_to_oklab(lv_color_t c, float *L, float *a, float *b)
{
    float r  = srgb_to_linear(c.red   / 255.0f);
    float g  = srgb_to_linear(c.green / 255.0f);
    float bl = srgb_to_linear(c.blue  / 255.0f);

    float l_ = cbrtf(0.4122214708f * r + 0.5363325363f * g + 0.0514459929f * bl);
    float m_ = cbrtf(0.2119034982f * r + 0.6806995451f * g + 0.1073969566f * bl);
    float s_ = cbrtf(0.0883024619f * r + 0.2817188376f * g + 0.6299787005f * bl);

    *L = 0.2104542553f * l_ + 0.7936177850f * m_ - 0.0040720468f * s_;
    *a = 1.9779984951f * l_ - 2.4285922050f * m_ + 0.4505937099f * s_;
    *b = 0.0259040371f * l_ + 0.7827717662f * m_ - 0.8086757660f * s_;
}

static lv_color_t oklab_to_srgb(float L, float a, float b)
{
    float l_ = L + 0.3963377774f * a + 0.2158037573f * b;
    float m_ = L - 0.1055613458f * a - 0.0638541728f * b;
    float s_ = L - 0.0894841775f * a - 1.2914855480f * b;

    float l = cube(l_), m = cube(m_), s = cube(s_);

    float r  = +4.0767416621f * l - 3.3077115913f * m + 0.2309699292f * s;
    float g  = -1.2684380046f * l + 2.6097574011f * m - 0.3413193965f * s;
    float bl = -0.0041960863f * l - 0.7034186147f * m + 1.7076147010f * s;

    return lv_color_make((uint8_t)(linear_to_srgb(r)  * 255.0f + 0.5f),
                         (uint8_t)(linear_to_srgb(g)  * 255.0f + 0.5f),
                         (uint8_t)(linear_to_srgb(bl) * 255.0f + 0.5f));
}

lv_color_t ui_legible(lv_color_t desired, lv_color_t bg)
{
    if (ui_contrast_ratio(desired, bg) >= UI_CONTRAST_AA) return desired;

    float L, a, b;
    srgb_to_oklab(desired, &L, &a, &b);

    // Raise lightness toward white (keeping chroma direction a/b) until legible.
    for (int i = 1; i <= 20; i++) {
        float try_L = L + (1.0f - L) * (i / 20.0f);
        lv_color_t c = oklab_to_srgb(try_L, a, b);
        if (ui_contrast_ratio(c, bg) >= UI_CONTRAST_AA) return c;
    }
    return ui_text_on(bg);  // guarantee a legible result regardless of hue
}
