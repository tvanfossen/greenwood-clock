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
    // Keep the user's exact colour where it's already legible.
    if (ui_contrast_ratio(desired, bg) >= UI_CONTRAST_AA) return desired;

    float L, a, b, bgL, bga, bgb;
    srgb_to_oklab(desired, &L, &a, &b);
    srgb_to_oklab(bg, &bgL, &bga, &bgb);

    // Otherwise push the hue's lightness to the EXTREME away from the background
    // (full white over a dark bg, full black over a light one) — keeping the
    // chroma direction a/b — for maximum, crisp contrast rather than a washed-out
    // just-barely-legible tone. Fall back to plain black/white if even that hue
    // can't clear AA (e.g. a saturated hue over a mid-tone bg).
    float ext_L = (bgL > 0.5f) ? 0.0f : 1.0f;
    lv_color_t c = oklab_to_srgb(ext_L, a, b);
    if (ui_contrast_ratio(c, bg) >= UI_CONTRAST_AA) return c;
    return ui_text_on(bg);
}

// Add one pixel's 8-bit R/G/B to the running sums (RGB565 or ARGB/XRGB byte
// order). Pulled out of the sample loop to keep its cognitive complexity down.
static void accum_pixel(const uint8_t *row, int32_t xx, lv_color_format_t cf,
                        uint32_t *sr, uint32_t *sg, uint32_t *sb)
{
    if (cf == LV_COLOR_FORMAT_RGB565) {
        uint16_t p = ((const uint16_t *)row)[xx];
        uint8_t r5 = (p >> 11) & 0x1F, g6 = (p >> 5) & 0x3F, b5 = p & 0x1F;
        *sr += (uint32_t)((r5 << 3) | (r5 >> 2));
        *sg += (uint32_t)((g6 << 2) | (g6 >> 4));
        *sb += (uint32_t)((b5 << 3) | (b5 >> 2));
    } else {   // ARGB8888 / XRGB8888 — BGRA byte order
        const uint8_t *px = row + (uint32_t)xx * 4;
        *sb += px[0]; *sg += px[1]; *sr += px[2];
    }
}

// Clamp a region to the image bounds; returns false if nothing remains.
static bool clamp_region(const lv_image_dsc_t *img,
                         int32_t *x, int32_t *y, int32_t *w, int32_t *h)
{
    if (*x < 0) { *w += *x; *x = 0; }
    if (*y < 0) { *h += *y; *y = 0; }
    if (*x + *w > (int32_t)img->header.w) *w = (int32_t)img->header.w - *x;
    if (*y + *h > (int32_t)img->header.h) *h = (int32_t)img->header.h - *y;
    return (*w > 0 && *h > 0);
}

// Mean colour of a rectangular region of an RGB565/ARGB8888 image buffer
// (coords in BUFFER space). Coarse grid sample (~64x64) for speed.
lv_color_t ui_image_region_mean(const lv_image_dsc_t *img,
                                int32_t x, int32_t y, int32_t w, int32_t h)
{
    lv_color_t fallback = lv_color_make(128, 128, 128);
    if (!img || !img->data || w <= 0 || h <= 0) return fallback;
    if (!clamp_region(img, &x, &y, &w, &h)) return fallback;

    const uint8_t *data = (const uint8_t *)img->data;
    lv_color_format_t cf = img->header.cf;
    uint32_t stride = img->header.stride;
    uint32_t sr = 0, sg = 0, sb = 0, n = 0;
    int32_t step_x = (w > 64) ? (w / 64) : 1;
    int32_t step_y = (h > 64) ? (h / 64) : 1;

    for (int32_t yy = y; yy < y + h; yy += step_y) {
        const uint8_t *row = data + (uint32_t)yy * stride;
        for (int32_t xx = x; xx < x + w; xx += step_x) {
            accum_pixel(row, xx, cf, &sr, &sg, &sb);
            n++;
        }
    }
    if (!n) return fallback;
    return lv_color_make((uint8_t)(sr / n), (uint8_t)(sg / n), (uint8_t)(sb / n));
}
