/**
 * @file theme_editor.h
 * v2: the fourth core (alert) has its tab.
 * @version 2
 * @brief On-screen theme editor: a live design preview beside OKLCH pickers for the four
 *        cores, the contrast target, and the slot grid (which role fills each themed place).
 *
 * Edits apply to the running theme as they are made (every bound object repaints), so the
 * whole screen is the preview. Save hands the edited theme to the caller; closing without
 * saving restores the theme the editor opened with. Pure LVGL + ui_theme: no ESP-IDF, so the
 * host preview renders it too. Caller holds the LVGL lock.
 */
#pragma once

#include "lvgl.h"
#include "ui_theme.h"

#ifdef __cplusplus
extern "C" {
#endif

/** What the editor is editing: a core colour, the contrast target, or the slots. */
typedef enum {
    THEME_EDITOR_INK = 0,
    THEME_EDITOR_SURFACE,
    THEME_EDITOR_ACCENT,
    THEME_EDITOR_ALERT,      /**< The alert core: the error / warning severities step from it. */
    THEME_EDITOR_CONTRAST,
    THEME_EDITOR_SLOTS,
    THEME_EDITOR_TAB_COUNT
} theme_editor_tab_t;

/** Called with the edited theme when the user saves (the caller persists it): for the active
 *  schedule profile only when @p for_profile, else as the global theme. */
typedef void (*theme_editor_save_cb_t)(const ui_theme_t *theme, bool for_profile);

/** Called when the user closes the editor (after save, or Back): the caller deletes it (or its
 *  parent). Deleting it without saving restores the theme it opened with. */
typedef void (*theme_editor_close_cb_t)(void);

/**
 * @brief Build the editor full-width on @p parent from @p y down, starting from the current
 *        theme. With @p profile (the active schedule profile's name; NULL or "" for none),
 *        Save asks whether the theme is for all the time or for that profile only.
 * v2: takes the active profile's name: Save asks global or profile.
 * @version 2
 * @req REQ-UI-004
 * @return The editor's root object (delete it, or close the editor, to remove it).
 */
lv_obj_t *theme_editor_create(lv_obj_t *parent, int32_t y, const char *profile,
                              theme_editor_save_cb_t on_save, theme_editor_close_cb_t on_close);

/**
 * @brief Press Save (as tapping it would): with a profile, shows the where-to-save choice.
 * @version 1
 * @req REQ-UI-004
 */
void theme_editor_press_save(void);

/**
 * @brief Show tab @p tab (as tapping it would).
 * @version 1
 * @req REQ-UI-004
 */
void theme_editor_set_tab(theme_editor_tab_t tab);

/**
 * @brief Set the edited theme's core @p tab (INK / SURFACE / ACCENT / ALERT) to @p c and apply it, as
 *        dragging its sliders would.
 * @version 1
 * @req REQ-UI-004
 */
void theme_editor_set_core(theme_editor_tab_t tab, ui_oklch_t c);

/**
 * @brief The theme as edited so far.
 * @version 1
 * @req REQ-UI-004
 * @return The working theme.
 */
const ui_theme_t *theme_editor_theme(void);

#ifdef __cplusplus
}
#endif
