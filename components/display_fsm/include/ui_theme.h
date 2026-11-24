/**
 * @file ui_theme.h
 * v2: slots — the themed places (cards, strips, alerts, ...) and which fill role each takes.
 * @version 2
 * @brief The UI colour theme: three OKLCH core colours (ink, surface, accent) and a
 *        contrast target, mapped to the named roles every widget draws with.
 *
 * ui_theme.c is the only place colour values are written down. Everything else asks for
 * a role. Colours that come from an image, a GIF or a Lottie file are that asset's own
 * and are not themed.
 *
 * Text roles carry a contrast target: TEXT reaches the theme's target, TEXT_MUTED WCAG AA
 * (or the theme's target when lower), TEXT_FAINT 3:1. ui_theme_color() judges them against
 * SURFACE. Text over anything else asks ui_theme_text_on() with the colour really behind
 * it, which keeps the role's hue and shifts only its OKLCH lightness.
 *
 * Pure C (no ESP-IDF): host-tested. ui_theme_set() runs under the LVGL lock.
 */
#pragma once

#include "lvgl.h"
#include "ui_contrast.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define UI_THEME_NAME_LEN 24

/** What a colour is for. Text roles end in _TEXT or begin with TEXT. */
typedef enum {
    UI_ROLE_SCREEN = 0,        /**< The screen behind everything (letterboxing, gaps). */
    UI_ROLE_SURFACE,           /**< Panels and cards. */
    UI_ROLE_SURFACE_RAISED,    /**< Strips and dialogs, a step off the surface. */
    UI_ROLE_SURFACE_INSET,     /**< Buttons, tracks and rows, two steps off. */
    UI_ROLE_BORDER,            /**< Outlines, dividers and informational fills. */
    UI_ROLE_TEXT,              /**< Primary text, at the theme's contrast target. */
    UI_ROLE_TEXT_MUTED,        /**< Secondary text, at AA. */
    UI_ROLE_TEXT_FAINT,        /**< Hints and captions, at 3:1. */
    UI_ROLE_ACCENT,            /**< Highlights and progress. */
    UI_ROLE_ACCENT_ALT,        /**< A second accent, a hue rotation of the first. */
    UI_ROLE_ERROR,             /**< Errors and severe alerts. */
    UI_ROLE_ERROR_STRONG,      /**< Extreme alerts, destructive buttons. */
    UI_ROLE_WARN,              /**< Warnings and moderate alerts. */
    UI_ROLE_LIGHT,             /**< The theme's near-white (snow, markers). */
    UI_ROLE_COUNT
} ui_role_t;

/** The themed places a fill role is chosen for. A theme file may move any slot to any role
 *  ("slots": {"alert_minor": "accent"}); the defaults are the original look. */
typedef enum {
    UI_SLOT_CARD = 0,          /**< Weather card, astronomy / ambient panels. */
    UI_SLOT_STRIP,             /**< Forecast strip. */
    UI_SLOT_CHIP,              /**< Radar time chip, photo notice. */
    UI_SLOT_DIALOG,            /**< Settings panels and modal dialogs. */
    UI_SLOT_CONTROL,           /**< Grid buttons, progress tracks. */
    UI_SLOT_ALERT_EXTREME,     /**< Alert banner, Extreme severity. */
    UI_SLOT_ALERT_SEVERE,      /**< Alert banner, Severe (and unknown) severity. */
    UI_SLOT_ALERT_MODERATE,    /**< Alert banner, Moderate severity. */
    UI_SLOT_ALERT_MINOR,       /**< Alert banner, Minor severity. */
    UI_SLOT_MARKER,            /**< Radar home marker. */
    UI_SLOT_COUNT
} ui_slot_t;

/** A theme as written in its JSON file: three core colours and a contrast target. */
typedef struct {
    char       name[UI_THEME_NAME_LEN];   /**< Shown in the theme picker. */
    ui_oklch_t ink;        /**< Text hue (and lightness, where it is legible as is). */
    ui_oklch_t surface;    /**< Panel hue and lightness; its polarity orders the tints. */
    ui_oklch_t accent;     /**< Highlights, focus, progress, the decorative palettes. */
    float      contrast;   /**< WCAG ratio primary text reaches (e.g. 7.0). */
    uint8_t    slots[UI_SLOT_COUNT];   /**< ui_role_t filling each slot. */
} ui_theme_t;


/** Number of leaf colours in the theme's autumn palette (ui_theme_leaf). */
#define UI_THEME_LEAF_COUNT 5

/** Number of distinct profile swatches (ui_theme_swatch). */
#define UI_THEME_SWATCH_COUNT 8

/** The compiled-in theme: the clock's original look. Used when no theme file loads. */
extern const ui_theme_t UI_THEME_BASE;

/**
 * @brief Derive every role colour of @p t into @p out (pure; what ui_theme_set installs).
 * @version 1
 * @req REQ-UI-003
 */
void ui_theme_derive(const ui_theme_t *t, lv_color_t out[UI_ROLE_COUNT]);

/**
 * @brief Install @p t (NULL: UI_THEME_BASE) as the current theme. Caller holds the LVGL lock.
 * @version 1
 * @req REQ-UI-003
 */
void ui_theme_set(const ui_theme_t *t);

/**
 * @brief The current theme.
 * @version 1
 * @req REQ-UI-003
 * @return The installed theme (UI_THEME_BASE before any ui_theme_set).
 */
const ui_theme_t *ui_theme_get(void);

/**
 * @brief Whether the current theme is dark (its surface below mid lightness).
 * @version 1
 * @req REQ-UI-003
 * @return true for a dark theme.
 */
bool ui_theme_is_dark(void);

/**
 * @brief The current theme's colour for @p role (text roles judged against SURFACE).
 * @version 1
 * @req REQ-UI-003
 * @return The colour; SURFACE for an out-of-range role.
 */
lv_color_t ui_theme_color(ui_role_t role);

/**
 * @brief The text role @p role made legible over @p backdrop, the colour really behind the
 *        text: its hue kept, its lightness moved only as far as the role's target needs.
 * @version 1
 * @req REQ-UI-003
 * @req REQ-BG-002
 * @return The text colour.
 */
lv_color_t ui_theme_text_on(ui_role_t role, lv_color_t backdrop);

/**
 * @brief The contrast target of @p role: the theme's for TEXT, AA for TEXT_MUTED (or the
 *        theme's when lower), 3:1 for TEXT_FAINT, AA for any other role used as text.
 * @version 1
 * @req REQ-UI-003
 * @return WCAG ratio.
 */
float ui_theme_target(ui_role_t role);

/**
 * @brief Leaf @p i (mod UI_THEME_LEAF_COUNT) of the theme's autumn palette, a spread of
 *        hues around the accent.
 * @version 1
 * @req REQ-UI-003
 * @return The leaf colour.
 */
lv_color_t ui_theme_leaf(int i);

/**
 * @brief Swatch @p i (mod UI_THEME_SWATCH_COUNT): evenly spaced hues from the accent, for
 *        telling list entries (schedule profiles) apart.
 * @version 1
 * @req REQ-UI-003
 * @return The swatch colour.
 */
lv_color_t ui_theme_swatch(int i);

/**
 * @brief A neutral of the surface hue at lightness @p l (0..1), for drawn greys such as
 *        the moon's brightness.
 * @version 1
 * @req REQ-UI-003
 * @return The colour.
 */
lv_color_t ui_theme_tone(float l);

/**
 * @brief The theme's dark or light extreme (a deep tone of the surface hue, or LIGHT),
 *        whichever contrasts more with @p fg: for shadows and outlines under text.
 * @version 1
 * @req REQ-UI-003
 * @return The colour.
 */
lv_color_t ui_theme_shadow_for(lv_color_t fg);

/**
 * @brief The role named @p name ("text", "surface_raised", ...), as layout JSON spells it.
 * @version 1
 * @req REQ-UI-003
 * @return Whether the name is a role (@p out set when it is).
 */
bool ui_theme_role_by_name(const char *name, ui_role_t *out);

/**
 * @brief The JSON / CSS name of @p role.
 * @version 1
 * @req REQ-UI-003
 * @return The name; "" for an out-of-range role.
 */
const char *ui_theme_role_name(ui_role_t role);

/**
 * @brief The fill role of @p slot under the current theme.
 * @version 1
 * @req REQ-UI-003
 * @return The role; SURFACE for an out-of-range slot.
 */
ui_role_t ui_theme_slot_role(ui_slot_t slot);

/**
 * @brief The JSON name of @p slot ("alert_severe").
 * @version 1
 * @req REQ-UI-003
 * @return The name; "" for an out-of-range slot.
 */
const char *ui_theme_slot_name(ui_slot_t slot);

/**
 * @brief The slot named @p name.
 * @version 1
 * @req REQ-UI-003
 * @return Whether the name is a slot (@p out set when it is).
 */
bool ui_theme_slot_by_name(const char *name, ui_slot_t *out);

/**
 * @brief Parse a theme file:
 *        {"name": "...", "ink": [L, C, h], "surface": [L, C, h], "accent": [L, C, h],
 *         "contrast": 7.0, "slots": {"alert_minor": "accent"}}. "contrast" (7.0) and
 *        "slots" (the defaults) are optional; an unknown slot or role refuses the file.
 * @version 1
 * @req REQ-UI-003
 * @return Whether @p json is a complete, in-range theme (@p out set only when it is).
 */
bool ui_theme_parse(const char *json, ui_theme_t *out);

/**
 * @brief Write @p t as a theme file (the format ui_theme_parse reads, every slot spelled out).
 * @version 1
 * @req REQ-UI-003
 * @return Whether it fit in @p n bytes (@p buf is NUL-terminated either way when n > 0).
 */
bool ui_theme_to_json(const ui_theme_t *t, char *buf, size_t n);

#ifdef __cplusplus
}
#endif
