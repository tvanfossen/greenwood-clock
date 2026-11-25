/**
 * @file ui_contrast.c
 * v2: OKLCH conversions public; the legibility target is a parameter (ui_legible_to).
 * v3: no colour literals: ui_text_on (unused) is gone; the region mean's fallback is the
 *     caller's.
 * v4: APCA contrast (ui_apca_lc) and ui_legible_apca: the light/dark side from the
 *     background, the lightness moved along it.
 * @version 4
 * @brief Text-legibility helpers: WCAG luminance/contrast, APCA contrast, OKLCH
 *        conversions, minimal OKLCH lightness shift and image-region mean colour.
 */
// Text-legibility helpers — see ui_contrast.h.

#include "ui_contrast.h"
#include <math.h>
#include <stdint.h>

// ---------------------------------------------------------------------------
// sRGB transfer + WCAG luminance
// ---------------------------------------------------------------------------

/**
 * @brief Convert an sRGB channel (0..1) to linear light.
 * @version 1
 * @utility
 * @return Linear channel value.
 */
static float srgb_to_linear(float c)
{
    return (c <= 0.04045f) ? (c / 12.92f)
                           : powf((c + 0.055f) / 1.055f, 2.4f);
}

/**
 * @brief Convert a linear channel to sRGB, clamped to 0..1.
 * @version 1
 * @utility
 * @return sRGB channel value.
 */
static float linear_to_srgb(float c)
{
    if (c < 0.0f) c = 0.0f;
    if (c > 1.0f) c = 1.0f;
    return (c <= 0.0031308f) ? (12.92f * c)
                             : (1.055f * powf(c, 1.0f / 2.4f) - 0.055f);
}

// WCAG relative luminance (0..1).
/**
 * @brief WCAG relative luminance of a colour.
 * @version 1
 * @utility
 * @return Luminance in 0..1.
 */
static float rel_luminance(lv_color_t c)
{
    float r = srgb_to_linear(c.red   / 255.0f);
    float g = srgb_to_linear(c.green / 255.0f);
    float b = srgb_to_linear(c.blue  / 255.0f);
    return 0.2126f * r + 0.7152f * g + 0.0722f * b;
}

/**
 * @brief WCAG contrast ratio between two colours.
 * @version 1
 * @req REQ-BG-002
 * @return Ratio from 1.0 (none) to 21.0 (black/white).
 */
float ui_contrast_ratio(lv_color_t a, lv_color_t b)
{
    float la = rel_luminance(a);
    float lb = rel_luminance(b);
    float hi = fmaxf(la, lb);
    float lo = fminf(la, lb);
    return (hi + 0.05f) / (lo + 0.05f);
}


// ---------------------------------------------------------------------------
// OKLab / OKLCH (Björn Ottosson's matrices) — used to raise a hue's lightness
// while preserving its chroma direction.
// ---------------------------------------------------------------------------

/**
 * @brief Cube a float.
 * @version 1
 * @utility
 * @return x * x * x.
 */
static float cube(float x) { return x * x * x; }

/**
 * @brief Convert an sRGB colour to OKLab (Ottosson's matrices).
 * @version 1
 * @utility
 */
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

/**
 * @brief Convert OKLab to linear sRGB (unclamped: out of gamut when outside 0..1).
 * @version 1
 * @utility
 */
static void oklab_to_linear(float L, float a, float b, float rgb[3])
{
    float l_ = L + 0.3963377774f * a + 0.2158037573f * b;
    float m_ = L - 0.1055613458f * a - 0.0638541728f * b;
    float s_ = L - 0.0894841775f * a - 1.2914855480f * b;

    float l = cube(l_), m = cube(m_), s = cube(s_);

    rgb[0] = +4.0767416621f * l - 3.3077115913f * m + 0.2309699292f * s;
    rgb[1] = -1.2684380046f * l + 2.6097574011f * m - 0.3413193965f * s;
    rgb[2] = -0.0041960863f * l - 0.7034186147f * m + 1.7076147010f * s;
}

/**
 * @brief Convert OKLab to an sRGB colour (channels clamped).
 * v2: the matrix half is oklab_to_linear (shared with the gamut-mapped conversion).
 * @version 2
 * @utility
 * @return sRGB colour.
 */
static lv_color_t oklab_to_srgb(float L, float a, float b)
{
    float rgb[3];
    oklab_to_linear(L, a, b, rgb);
    return lv_color_make((uint8_t)(linear_to_srgb(rgb[0]) * 255.0f + 0.5f),
                         (uint8_t)(linear_to_srgb(rgb[1]) * 255.0f + 0.5f),
                         (uint8_t)(linear_to_srgb(rgb[2]) * 255.0f + 0.5f));
}

#define GAMUT_EPS 0.0005f   // linear-channel slack treated as in gamut

/**
 * @brief Whether OKLab (@p L, @p a, @p b) is inside the sRGB gamut.
 * @version 1
 * @utility
 * @return True when every linear channel is within 0..1.
 */
static bool in_gamut(float L, float a, float b)
{
    float rgb[3];
    oklab_to_linear(L, a, b, rgb);
    bool ok = true;
    for (int i = 0; i < 3; i++) ok = ok && rgb[i] >= -GAMUT_EPS && rgb[i] <= 1.0f + GAMUT_EPS;
    return ok;
}

/**
 * @brief Convert OKLab to sRGB keeping lightness and hue: an out-of-gamut colour has its
 *        chroma reduced (bisected) until it fits, instead of each channel clamped (which
 *        bends the hue: a darkened gold turns orange).
 * @version 1
 * @utility
 * @return sRGB colour.
 */
static lv_color_t oklab_to_srgb_mapped(float L, float a, float b)
{
    float lo = 0.0f, hi = 1.0f;
    if (!in_gamut(L, a, b)) {
        for (int i = 0; i < 16; i++) {
            float mid = 0.5f * (lo + hi);
            if (in_gamut(L, a * mid, b * mid)) lo = mid;
            else hi = mid;
        }
        a *= lo;
        b *= lo;
    }
    return oklab_to_srgb(L, a, b);
}

#define LEGIBLE_STEPS 16   /* bisection steps on OKLCH L: 2^-16 is far below one 8-bit step */

/** A contrast measure of text against a background; larger is more legible. */
typedef float (*contrast_fn)(lv_color_t text, lv_color_t bg);

/**
 * @brief APCA contrast magnitude (the polarity sign dropped), as a contrast_fn.
 * @version 1
 * @utility
 * @return |Lc|.
 */
static float apca_abs(lv_color_t text, lv_color_t bg)
{
    return fabsf(ui_apca_lc(text, bg));
}

/**
 * @brief Contrast by @p fn against @p bg of the OKLab colour (@p L, @p a, @p b).
 * v2: the measure is a parameter (WCAG or APCA).
 * @version 2
 * @utility
 * @return The contrast.
 */
static float contrast_at(contrast_fn fn, float L, float a, float b, lv_color_t bg)
{
    return fn(oklab_to_srgb(L, a, b), bg);
}

/**
 * @brief Smallest move of lightness from @p L toward @p end (0 or 1) that reaches
 *        @p target against @p bg by @p fn; @p L itself is known to fall short.
 * v2: the target is a parameter (was always UI_CONTRAST_AA).
 * v3: the measure is a parameter.
 * @version 3
 * @utility
 * @return Whether that direction reaches the target at all (@p out is set when it does).
 */
static bool shift_toward(contrast_fn fn, float L, float a, float b, lv_color_t bg, float end,
                         float target, float *out)
{
    if (contrast_at(fn, end, a, b, bg) < target) return false;
    float fail = L, pass = end;
    for (int i = 0; i < LEGIBLE_STEPS; i++) {
        float mid = 0.5f * (fail + pass);
        if (contrast_at(fn, mid, a, b, bg) >= target) pass = mid;
        else fail = mid;
    }
    *out = pass;
    return true;
}

/**
 * @brief @p desired when it already reaches @p target against @p bg; otherwise the same
 *        OKLCH hue and chroma with lightness moved only as far as @p target needs, in
 *        whichever direction needs the smaller move (the extreme with the better contrast
 *        when neither reaches it).
 * v2: shift_toward and contrast_at take the measure (WCAG here).
 * @version 2
 * @req REQ-BG-002
 * @return Legible text colour.
 */
lv_color_t ui_legible_to(lv_color_t desired, lv_color_t bg, float target)
{
    if (ui_contrast_ratio(desired, bg) >= target) return desired;
    float L, a, b, up_L = 1.0f, down_L = 0.0f;
    srgb_to_oklab(desired, &L, &a, &b);
    contrast_fn fn = ui_contrast_ratio;
    bool up = shift_toward(fn, L, a, b, bg, 1.0f, target, &up_L);
    bool down = shift_toward(fn, L, a, b, bg, 0.0f, target, &down_L);
    float out;
    if (up && down) out = (up_L - L <= L - down_L) ? up_L : down_L;
    else if (up || down) out = up ? up_L : down_L;
    else out = (contrast_at(fn, 1.0f, a, b, bg) >= contrast_at(fn, 0.0f, a, b, bg)) ? 1.0f : 0.0f;
    return oklab_to_srgb(out, a, b);
}

// ---------------------------------------------------------------------------
// APCA (Accessible Perceptual Contrast Algorithm, apca-w3 0.0.98G-4g constants)
// ---------------------------------------------------------------------------

#define APCA_BLK_THRS  0.022f    // soft black clamp: luminance below this is lifted
#define APCA_BLK_CLMP  1.414f
#define APCA_NORM_BG   0.56f     // dark text on a light background
#define APCA_NORM_TXT  0.57f
#define APCA_REV_BG    0.65f     // light text on a dark background
#define APCA_REV_TXT   0.62f
#define APCA_SCALE     1.14f
#define APCA_OFFSET    0.027f
#define APCA_LO_CLIP   0.1f
#define APCA_DELTA_MIN 0.0005f

/**
 * @brief APCA screen luminance of @p c (simple 2.4 power, soft-clamped near black).
 * @version 1
 * @utility
 * @return Luminance 0..1.
 */
static float apca_y(lv_color_t c)
{
    float y = 0.2126729f * powf(c.red / 255.0f, 2.4f) +
              0.7151522f * powf(c.green / 255.0f, 2.4f) +
              0.0721750f * powf(c.blue / 255.0f, 2.4f);
    return y > APCA_BLK_THRS ? y : y + powf(APCA_BLK_THRS - y, APCA_BLK_CLMP);
}

/**
 * @brief APCA lightness contrast Lc of @p text on @p bg.
 * @version 1
 * @req REQ-BG-002
 * @return Lc: positive for dark text on a light background, negative for light on dark,
 *         about -108..106; 0 when too close to tell.
 */
float ui_apca_lc(lv_color_t text, lv_color_t bg)
{
    float yt = apca_y(text), yb = apca_y(bg), lc = 0.0f;
    if (fabsf(yb - yt) >= APCA_DELTA_MIN) {
        bool normal = yb > yt;
        float sapc = normal ? (powf(yb, APCA_NORM_BG) - powf(yt, APCA_NORM_TXT)) * APCA_SCALE
                            : (powf(yb, APCA_REV_BG) - powf(yt, APCA_REV_TXT)) * APCA_SCALE;
        if (normal && sapc >= APCA_LO_CLIP) lc = (sapc - APCA_OFFSET) * 100.0f;
        else if (!normal && sapc <= -APCA_LO_CLIP) lc = (sapc + APCA_OFFSET) * 100.0f;
    }
    return lc;
}

/**
 * @brief @p ink made legible over @p bg by APCA: the light or dark side is whichever of
 *        white and black reads better on @p bg (not the ink's own); the ink's lightness is
 *        mirrored onto that side when it is on the other, kept when it already reaches
 *        @p lc_target, else moved only as far as the target needs (to the extreme when even
 *        that falls short). Hue and chroma are the ink's; chroma is reduced, never the hue
 *        bent, when the new lightness cannot hold it.
 * @version 1
 * @req REQ-BG-002
 * @return Legible text colour.
 */
lv_color_t ui_legible_apca(lv_color_t ink, lv_color_t bg, float lc_target)
{
    float L, a, b;
    srgb_to_oklab(ink, &L, &a, &b);
    bool light = apca_abs(lv_color_white(), bg) >= apca_abs(lv_color_black(), bg);
    float end = light ? 1.0f : 0.0f;
    float start = (light == (L >= 0.5f)) ? L : 1.0f - L;
    float out = start;
    if (contrast_at(apca_abs, start, a, b, bg) < lc_target &&
        !shift_toward(apca_abs, start, a, b, bg, end, lc_target, &out)) {
        out = end;
    }
    return out == L ? ink : oklab_to_srgb_mapped(out, a, b);
}

/**
 * @brief ui_legible_to() at WCAG AA.
 * v3: delegates to ui_legible_to (the target became a parameter).
 * v2: minimal shift with pass-through (was: lightness always pushed to L <= 0.06 or >= 0.95,
 *     which turned a legible white black over a light background).
 * @version 3
 * @req REQ-BG-002
 * @return Legible text colour.
 */
lv_color_t ui_legible(lv_color_t desired, lv_color_t bg)
{
    return ui_legible_to(desired, bg, UI_CONTRAST_AA);
}

/**
 * @brief The sRGB colour of an OKLCH colour (channels clamped to the gamut).
 * @version 1
 * @req REQ-UI-003
 * @return sRGB colour.
 */
lv_color_t ui_oklch_to_color(ui_oklch_t c)
{
    float hr = c.h * (3.14159265f / 180.0f);
    return oklab_to_srgb(c.l, c.c * cosf(hr), c.c * sinf(hr));
}

/**
 * @brief The OKLCH coordinates of an sRGB colour (hue in degrees, 0..360).
 * @version 1
 * @req REQ-UI-003
 * @return OKLCH colour.
 */
ui_oklch_t ui_color_to_oklch(lv_color_t c)
{
    float L, a, b;
    srgb_to_oklab(c, &L, &a, &b);
    float h = atan2f(b, a) * (180.0f / 3.14159265f);
    ui_oklch_t out = { L, sqrtf(a * a + b * b), h < 0.0f ? h + 360.0f : h };
    return out;
}

// Add one pixel's 8-bit R/G/B to the running sums (RGB565 or ARGB/XRGB byte
// order). Pulled out of the sample loop to keep its cognitive complexity down.
/**
 * @brief Add one RGB565 or BGRA pixel's 8-bit R/G/B to the running sums.
 * @version 1
 * @utility
 */
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
/**
 * @brief Clamp a region to the image bounds.
 * @version 1
 * @utility
 * @return true if a non-empty region remains, false otherwise.
 */
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
/**
 * @brief Mean colour of a region of an RGB565/ARGB8888 image buffer, sampled on a ~64x64 grid.
 * v2: the caller gives the fallback (was a fixed mid-grey).
 * @version 2
 * @req REQ-BG-002
 * @return Mean colour, or @p fallback for missing data or an empty region.
 */
lv_color_t ui_image_region_mean(const lv_image_dsc_t *img, int32_t x, int32_t y, int32_t w,
                                int32_t h, lv_color_t fallback)
{
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
