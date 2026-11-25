/**
 * @file ui_theme.c
 * v2: slots (which role fills each themed place).
 * v3: the base theme is named "Default" (was "Greenwood").
 * v4: text straight on the picture (the clock) by APCA, its side from the picture.
 * v5: the alert core: error, error_strong and warn step from it (were fixed hues).
 * v6: the sky core: the sun is it, the moon a pale white of its hue.
 * v7: the aurora and meteor roles, fixed hue steps from the sky core.
 * @version 7
 * @brief The UI colour theme: three OKLCH cores derived into named roles. The only file
 *        that writes colour values down (see ui_theme.h).
 *
 * Every role is a rule on the cores, in OKLCH:
 *   - Surfaces step away from the surface core's lightness toward the middle (lighter on a
 *     dark theme, darker on a light one), keeping its hue and chroma.
 *   - Text roles start from the ink (muted and faint blend toward the surface first) and are
 *     then made legible at their contrast target, keeping the ink's hue.
 *   - The accents and the decorative palettes rotate the accent's hue.
 *   - Error, error_strong and warn step from the alert core (lightness by the theme's
 *     polarity, chroma scaled, warn's hue turned toward amber).
 * The steps are fitted so UI_THEME_BASE reproduces the clock's original hand-picked
 * colours (navy panels, gold accent, violet aurora, coral/red alerts).
 */

#include "ui_theme.h"

#include <math.h>
#include <string.h>

// ---------------------------------------------------------------------------
// The base theme and the derivation constants
// ---------------------------------------------------------------------------

#define UI_SLOTS_DEFAULT {                                   \
    [UI_SLOT_CARD]           = UI_ROLE_SURFACE,               \
    [UI_SLOT_STRIP]          = UI_ROLE_SURFACE_RAISED,        \
    [UI_SLOT_CHIP]           = UI_ROLE_SURFACE,               \
    [UI_SLOT_DIALOG]         = UI_ROLE_SURFACE_RAISED,        \
    [UI_SLOT_CONTROL]        = UI_ROLE_SURFACE_INSET,         \
    [UI_SLOT_ALERT_EXTREME]  = UI_ROLE_ERROR_STRONG,          \
    [UI_SLOT_ALERT_SEVERE]   = UI_ROLE_ERROR,                 \
    [UI_SLOT_ALERT_MODERATE] = UI_ROLE_WARN,                  \
    [UI_SLOT_ALERT_MINOR]    = UI_ROLE_BORDER,                \
    [UI_SLOT_MARKER]         = UI_ROLE_ERROR,                 \
}

const ui_theme_t UI_THEME_BASE = {
    .name     = "Default",
    .ink      = { 1.000f, 0.000f, 0.0f },      // white
    .surface  = { 0.158f, 0.042f, 280.6f },    // navy panel
    .accent   = { 0.834f, 0.117f, 87.4f },     // gold
    .alert    = { 0.530f, 0.210f, 29.0f },     // deep red (extreme)
    .sky      = { 0.834f, 0.117f, 87.4f },     // the sun's gold
    .contrast = 7.0f,
    .slots    = UI_SLOTS_DEFAULT,
};

#define MOON_L        0.95f   // the moon's light: near-white ...
#define MOON_C        0.20f   // ... with a little of the sky core's chroma
// The sky events step from the sky core (fitted so the base gold gives a natural green and
// violet aurora and blue-white meteors).
#define AURORA_L      0.80f
#define AURORA_C      1.45f
#define AURORA_DH     58.0f
#define AURORA_ALT_L  0.62f
#define AURORA_ALT_C  1.25f
#define AURORA_ALT_DH 213.0f
#define METEOR_L      0.95f
#define METEOR_C      0.35f
#define METEOR_DH     148.0f
#define STEP_RAISED   0.07f   // surface -> raised, toward mid lightness
#define STEP_INSET    0.14f   // surface -> inset
#define STEP_BORDER   0.38f   // surface -> border
#define BORDER_CHROMA 2.0f    // border chroma, x the surface's
#define SCREEN_DARK   0.35f   // dark theme: screen lightness, x the surface's
#define SCREEN_LIGHT  0.03f   // light theme: screen this much lighter than the surface
#define MUTED_BLEND   0.25f   // muted text: this far from the ink toward the surface
#define FAINT_BLEND   0.45f   // faint text: this far
#define FAINT_TARGET  3.0f    // WCAG ratio for faint text (large-text AA)
#define ALT_HUE       215.0f  // second accent: hue rotation
#define ALT_L         0.83f   // second accent: lightness, x the accent's
#define ALT_C         0.65f   // second accent: chroma, x the accent's
// Alert roles from the alert core (fitted so the base core gives the original colours).
#define ERR_DL_DARK    0.17f   // error: lightness step from the core on a dark theme
#define ERR_DL_LIGHT  -0.04f   //        and on a light one
#define ERR_C          0.81f   //        chroma, x the core's
#define ERR_DH        -2.0f    //        hue turn
#define WARN_DL_DARK   0.32f   // warn (amber): lightness step, dark theme
#define WARN_DL_LIGHT  0.22f   //               light theme
#define WARN_C         0.62f   //               chroma, x the core's
#define WARN_DH       51.0f    //               hue turn toward amber
#define SWATCH_L      0.48f   // profile swatches
#define SWATCH_C      0.10f
#define SWATCH_STEP   45.0f
#define TONE_CHROMA   0.3f    // tone(): chroma, x the surface's
#define SHADOW_DARK_L 0.08f   // shadow_for(): the dark extreme's lightness
#define DEFAULT_CONTRAST 7.0f

/** One leaf of the autumn palette: hue offset from the accent, lightness, chroma. */
typedef struct {
    float dh;
    float l;
    float c;
} leaf_recipe_t;

// Maple red, orange, gold, brown, rust: fitted to the base theme's gold accent.
static const leaf_recipe_t LEAVES[UI_THEME_LEAF_COUNT] = {
    { -53.5f, 0.529f, 0.167f },
    { -40.4f, 0.640f, 0.164f },
    {  -6.6f, 0.758f, 0.147f },
    { -25.8f, 0.513f, 0.091f },
    { -44.5f, 0.513f, 0.135f },
};

static const char *const ROLE_NAMES[UI_ROLE_COUNT] = {
    [UI_ROLE_SCREEN]         = "screen",
    [UI_ROLE_SURFACE]        = "surface",
    [UI_ROLE_SURFACE_RAISED] = "surface_raised",
    [UI_ROLE_SURFACE_INSET]  = "surface_inset",
    [UI_ROLE_BORDER]         = "border",
    [UI_ROLE_TEXT]           = "text",
    [UI_ROLE_TEXT_MUTED]     = "text_muted",
    [UI_ROLE_TEXT_FAINT]     = "text_faint",
    [UI_ROLE_ACCENT]         = "accent",
    [UI_ROLE_ACCENT_ALT]     = "accent_alt",
    [UI_ROLE_ERROR]          = "error",
    [UI_ROLE_ERROR_STRONG]   = "error_strong",
    [UI_ROLE_WARN]           = "warn",
    [UI_ROLE_LIGHT]          = "light",
    [UI_ROLE_SUN]            = "sun",
    [UI_ROLE_MOON]           = "moon",
    [UI_ROLE_AURORA]         = "aurora",
    [UI_ROLE_AURORA_ALT]     = "aurora_alt",
    [UI_ROLE_METEOR]         = "meteor",
};

static const char *const SLOT_NAMES[UI_SLOT_COUNT] = {
    [UI_SLOT_CARD]           = "card",
    [UI_SLOT_STRIP]          = "strip",
    [UI_SLOT_CHIP]           = "chip",
    [UI_SLOT_DIALOG]         = "dialog",
    [UI_SLOT_CONTROL]        = "control",
    [UI_SLOT_ALERT_EXTREME]  = "alert_extreme",
    [UI_SLOT_ALERT_SEVERE]   = "alert_severe",
    [UI_SLOT_ALERT_MODERATE] = "alert_moderate",
    [UI_SLOT_ALERT_MINOR]    = "alert_minor",
    [UI_SLOT_MARKER]         = "marker",
};

static ui_theme_t s_theme;                    // valid once s_installed
static bool       s_installed = false;        // false: UI_THEME_BASE is current
static lv_color_t s_roles[UI_ROLE_COUNT];     // derived colours
static lv_color_t s_sources[UI_ROLE_COUNT];   // text roles: the colour before legibility
static bool       s_derived = false;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/**
 * @brief Clamp @p x to 0..1.
 * @version 1
 * @utility
 * @return The clamped value.
 */
static float unit(float x)
{
    return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x);
}

/**
 * @brief @p c with lightness @p l (clamped), chroma @p chroma, hue @p h, as sRGB.
 * @version 1
 * @utility
 * @return The colour.
 */
static lv_color_t lch(float l, float chroma, float h)
{
    ui_oklch_t c = { unit(l), chroma < 0.0f ? 0.0f : chroma, fmodf(h + 360.0f, 360.0f) };
    return ui_oklch_to_color(c);
}

/**
 * @brief +1 when the surface is dark (tints step lighter), -1 when it is light.
 * @version 1
 * @utility
 * @return The step direction.
 */
static float polarity(const ui_theme_t *t)
{
    return t->surface.l < 0.5f ? 1.0f : -1.0f;
}

/**
 * @brief The surface core stepped @p step toward mid lightness, with @p chroma_scale x its
 *        chroma.
 * @version 1
 * @utility
 * @return The colour.
 */
static lv_color_t surface_step(const ui_theme_t *t, float step, float chroma_scale)
{
    return lch(t->surface.l + polarity(t) * step, t->surface.c * chroma_scale, t->surface.h);
}

/**
 * @brief The ink blended @p k of the way to the surface, in OKLab (straight-line, so a grey
 *        ink takes on the surface's hue instead of painting chroma at its own meaningless
 *        hue of 0).
 * @version 1
 * @utility
 * @return The colour.
 */
static lv_color_t ink_blend(const ui_theme_t *t, float k)
{
    const float rad = 3.14159265f / 180.0f;
    float ia = t->ink.c * cosf(t->ink.h * rad), ib = t->ink.c * sinf(t->ink.h * rad);
    float sa = t->surface.c * cosf(t->surface.h * rad);
    float sb = t->surface.c * sinf(t->surface.h * rad);
    float a = ia + (sa - ia) * k, b = ib + (sb - ib) * k;
    return lch(t->ink.l + (t->surface.l - t->ink.l) * k, sqrtf(a * a + b * b),
               atan2f(b, a) / rad);
}

/**
 * @brief Derive the screen, surface, border and light roles.
 * @version 1
 * @utility
 */
static void derive_surfaces(const ui_theme_t *t, lv_color_t out[UI_ROLE_COUNT])
{
    float screen_l = polarity(t) > 0.0f ? t->surface.l * SCREEN_DARK
                                        : t->surface.l + SCREEN_LIGHT;
    out[UI_ROLE_SCREEN]         = lch(screen_l, t->surface.c * 0.5f, t->surface.h);
    out[UI_ROLE_SURFACE]        = ui_oklch_to_color(t->surface);
    out[UI_ROLE_SURFACE_RAISED] = surface_step(t, STEP_RAISED, 1.0f);
    out[UI_ROLE_SURFACE_INSET]  = surface_step(t, STEP_INSET, 1.0f);
    out[UI_ROLE_BORDER]         = surface_step(t, STEP_BORDER, BORDER_CHROMA);
    out[UI_ROLE_LIGHT]          = lch(0.98f, 0.01f, t->surface.h);
}

/**
 * @brief Derive the accent and alert roles.
 * v2: the alert roles step from the alert core (were fixed hues).
 * v3: the sun and moon roles from the sky core.
 * v4: the aurora and meteor roles.
 * @version 4
 * @utility
 */
static void derive_accents(const ui_theme_t *t, lv_color_t out[UI_ROLE_COUNT])
{
    bool dark = polarity(t) > 0.0f;
    const ui_oklch_t *a = &t->alert;
    out[UI_ROLE_ACCENT]       = ui_oklch_to_color(t->accent);
    out[UI_ROLE_ACCENT_ALT]   = lch(t->accent.l * ALT_L, t->accent.c * ALT_C,
                                    t->accent.h + ALT_HUE);
    out[UI_ROLE_ERROR]        = lch(a->l + (dark ? ERR_DL_DARK : ERR_DL_LIGHT), a->c * ERR_C,
                                    a->h + ERR_DH);
    out[UI_ROLE_ERROR_STRONG] = ui_oklch_to_color(*a);
    out[UI_ROLE_WARN]         = lch(a->l + (dark ? WARN_DL_DARK : WARN_DL_LIGHT), a->c * WARN_C,
                                    a->h + WARN_DH);
    out[UI_ROLE_SUN]          = ui_oklch_to_color(t->sky);
    out[UI_ROLE_MOON]         = lch(MOON_L, t->sky.c * MOON_C, t->sky.h);   // a warm pale white
    out[UI_ROLE_AURORA]       = lch(AURORA_L, t->sky.c * AURORA_C, t->sky.h + AURORA_DH);
    out[UI_ROLE_AURORA_ALT]   = lch(AURORA_ALT_L, t->sky.c * AURORA_ALT_C, t->sky.h + AURORA_ALT_DH);
    out[UI_ROLE_METEOR]       = lch(METEOR_L, t->sky.c * METEOR_C, t->sky.h + METEOR_DH);
}

/**
 * @brief The unshifted source colour of every text-capable role: the ink blends for the
 *        text roles, the role colour itself for the rest.
 * @version 1
 * @utility
 */
static void derive_sources(const ui_theme_t *t, const lv_color_t roles[UI_ROLE_COUNT],
                           lv_color_t src[UI_ROLE_COUNT])
{
    memcpy(src, roles, sizeof(lv_color_t) * UI_ROLE_COUNT);
    src[UI_ROLE_TEXT]       = ui_oklch_to_color(t->ink);
    src[UI_ROLE_TEXT_MUTED] = ink_blend(t, MUTED_BLEND);
    src[UI_ROLE_TEXT_FAINT] = ink_blend(t, FAINT_BLEND);
}

/**
 * @brief Contrast target of @p role under theme @p t.
 * @version 1
 * @utility
 * @return WCAG ratio.
 */
static float target_of(const ui_theme_t *t, ui_role_t role)
{
    float main = t->contrast > 1.0f ? t->contrast : DEFAULT_CONTRAST;
    if (role == UI_ROLE_TEXT) return main;
    if (role == UI_ROLE_TEXT_FAINT) return fminf(FAINT_TARGET, main);
    return fminf(UI_CONTRAST_AA, main);
}

/**
 * @brief The current theme: the installed one, or UI_THEME_BASE before any is installed.
 * @version 1
 * @utility
 * @return The theme.
 */
static const ui_theme_t *current(void)
{
    return s_installed ? &s_theme : &UI_THEME_BASE;
}

/**
 * @brief Derive the current theme's roles and sources, unless already derived.
 * @version 1
 * @utility
 */
static void ensure_derived(void)
{
    if (s_derived) return;
    ui_theme_derive(current(), s_roles);
    derive_sources(current(), s_roles, s_sources);
    s_derived = true;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

/**
 * @brief Derive every role colour of @p t into @p out.
 * @version 1
 * @req REQ-UI-003
 */
void ui_theme_derive(const ui_theme_t *t, lv_color_t out[UI_ROLE_COUNT])
{
    derive_surfaces(t, out);
    derive_accents(t, out);
    lv_color_t src[UI_ROLE_COUNT];
    derive_sources(t, out, src);
    lv_color_t surface = out[UI_ROLE_SURFACE];
    out[UI_ROLE_TEXT]       = ui_legible_to(src[UI_ROLE_TEXT], surface,
                                            target_of(t, UI_ROLE_TEXT));
    out[UI_ROLE_TEXT_MUTED] = ui_legible_to(src[UI_ROLE_TEXT_MUTED], surface,
                                            target_of(t, UI_ROLE_TEXT_MUTED));
    out[UI_ROLE_TEXT_FAINT] = ui_legible_to(src[UI_ROLE_TEXT_FAINT], surface,
                                            target_of(t, UI_ROLE_TEXT_FAINT));
}

/**
 * @brief Install @p t (NULL: UI_THEME_BASE) as the current theme.
 * @version 1
 * @req REQ-UI-003
 */
void ui_theme_set(const ui_theme_t *t)
{
    s_theme = t ? *t : UI_THEME_BASE;
    s_installed = true;
    s_derived = false;
    ensure_derived();
}

/**
 * @brief The current theme.
 * @version 1
 * @req REQ-UI-003
 * @return The installed theme.
 */
const ui_theme_t *ui_theme_get(void)
{
    return current();
}

/**
 * @brief Whether the current theme is dark.
 * @version 1
 * @req REQ-UI-003
 * @return true for a dark theme.
 */
bool ui_theme_is_dark(void)
{
    return polarity(current()) > 0.0f;
}

/**
 * @brief The current theme's colour for @p role.
 * @version 1
 * @req REQ-UI-003
 * @return The colour; SURFACE for an out-of-range role.
 */
lv_color_t ui_theme_color(ui_role_t role)
{
    ensure_derived();
    return s_roles[(unsigned)role < UI_ROLE_COUNT ? role : UI_ROLE_SURFACE];
}

/**
 * @brief The contrast target of @p role under the current theme.
 * @version 1
 * @req REQ-UI-003
 * @return WCAG ratio.
 */
float ui_theme_target(ui_role_t role)
{
    return target_of(current(), role);
}

/**
 * @brief The text role @p role made legible over @p backdrop, from its unshifted source.
 * @version 1
 * @req REQ-UI-003
 * @req REQ-BG-002
 * @return The text colour.
 */
lv_color_t ui_theme_text_on(ui_role_t role, lv_color_t backdrop)
{
    ensure_derived();
    ui_role_t r = (unsigned)role < UI_ROLE_COUNT ? role : UI_ROLE_TEXT;
    return ui_legible_to(s_sources[r], backdrop, target_of(current(), r));
}

/**
 * @brief Leaf @p i of the theme's autumn palette.
 * @version 1
 * @req REQ-UI-003
 * @return The leaf colour.
 */
lv_color_t ui_theme_leaf(int i)
{
    const leaf_recipe_t *r = &LEAVES[(unsigned)i % UI_THEME_LEAF_COUNT];
    return lch(r->l, r->c, current()->accent.h + r->dh);
}

/**
 * @brief Swatch @p i: evenly spaced hues from the accent.
 * @version 1
 * @req REQ-UI-003
 * @return The swatch colour.
 */
lv_color_t ui_theme_swatch(int i)
{
    float step = (float)((unsigned)i % UI_THEME_SWATCH_COUNT) * SWATCH_STEP;
    return lch(SWATCH_L, SWATCH_C, current()->accent.h + step);
}

/**
 * @brief A neutral of the surface hue at lightness @p l.
 * @version 1
 * @req REQ-UI-003
 * @return The colour.
 */
lv_color_t ui_theme_tone(float l)
{
    return lch(l, current()->surface.c * TONE_CHROMA, current()->surface.h);
}

/**
 * @brief The theme's dark or light extreme, whichever contrasts more with @p fg.
 * @version 1
 * @req REQ-UI-003
 * @return The colour.
 */
lv_color_t ui_theme_shadow_for(lv_color_t fg)
{
    lv_color_t dark = ui_theme_tone(SHADOW_DARK_L);
    lv_color_t light = ui_theme_color(UI_ROLE_LIGHT);
    return ui_contrast_ratio(dark, fg) >= ui_contrast_ratio(light, fg) ? dark : light;
}

/**
 * @brief The fill role of @p slot under the current theme.
 * @version 1
 * @req REQ-UI-003
 * @return The role; SURFACE for an out-of-range slot.
 */
ui_role_t ui_theme_slot_role(ui_slot_t slot)
{
    ui_role_t r = UI_ROLE_SURFACE;
    if ((unsigned)slot < UI_SLOT_COUNT && current()->slots[slot] < UI_ROLE_COUNT) {
        r = (ui_role_t)current()->slots[slot];
    }
    return r;
}

/**
 * @brief The JSON name of @p slot.
 * @version 1
 * @req REQ-UI-003
 * @return The name; "" for an out-of-range slot.
 */
const char *ui_theme_slot_name(ui_slot_t slot)
{
    return (unsigned)slot < UI_SLOT_COUNT ? SLOT_NAMES[slot] : "";
}

/**
 * @brief The slot named @p name.
 * @version 1
 * @req REQ-UI-003
 * @return Whether the name is a slot.
 */
bool ui_theme_slot_by_name(const char *name, ui_slot_t *out)
{
    bool found = false;
    for (int i = 0; name && !found && i < UI_SLOT_COUNT; i++) {
        if (strcmp(name, SLOT_NAMES[i]) == 0) {
            *out = (ui_slot_t)i;
            found = true;
        }
    }
    return found;
}

/**
 * @brief The theme's ink made legible over the picture under @p a (or @p fallback).
 * v2: by APCA with the light/dark side from the picture (ui_legible_apca): the ink gives
 *     hue and chroma only (was WCAG from the ink's own lightness, so a mid-tone photo
 *     turned white ink black).
 * @version 2
 * @req REQ-UI-003
 * @req REQ-BG-002
 * @return The text colour.
 */
lv_color_t ui_theme_ink_on_picture(const lv_image_dsc_t *picture, const lv_area_t *a,
                                   lv_color_t fallback)
{
    lv_color_t behind = fallback;
    if (picture) {
        behind = ui_image_region_mean(picture, a->x1, a->y1 < 0 ? 0 : a->y1, a->x2 - a->x1 + 1,
                                      a->y2 - a->y1 + 1, fallback);
    }
    return ui_legible_apca(ui_oklch_to_color(current()->ink), behind, UI_APCA_LARGE);
}

/**
 * @brief The role named @p name.
 * @version 1
 * @req REQ-UI-003
 * @return Whether the name is a role.
 */
bool ui_theme_role_by_name(const char *name, ui_role_t *out)
{
    bool found = false;
    for (int i = 0; name && !found && i < UI_ROLE_COUNT; i++) {
        if (strcmp(name, ROLE_NAMES[i]) == 0) {
            *out = (ui_role_t)i;
            found = true;
        }
    }
    return found;
}

/**
 * @brief The JSON / CSS name of @p role.
 * @version 1
 * @req REQ-UI-003
 * @return The name; "" for an out-of-range role.
 */
const char *ui_theme_role_name(ui_role_t role)
{
    return (unsigned)role < UI_ROLE_COUNT ? ROLE_NAMES[role] : "";
}
