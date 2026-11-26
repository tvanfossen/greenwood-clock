/**
 * @file settings_theme.h
 * v2: settings_theme_edit (the Library's Edit).
 * @version 2
 * @brief Customize a theme from the settings: the on-screen theme editor over the running
 *        theme; Save asks for a name and keeps the theme as its own file (the name's stem,
 *        sched_theme_stem), then tells the caller, which points a profile at it. Pure LVGL
 *        over the settings backend. Caller holds the LVGL lock.
 */
#pragma once

#include "settings_app.h"
#include "ui_theme.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Open the theme editor; once a theme is saved under a name, @p cb gets its file stem.
 *        Back without saving restores the theme the editor opened with and calls nothing.
 * @version 1
 * @req REQ-UI-004
 */
void settings_theme_customize(settings_pick_cb_t cb, void *ctx);

/**
 * @brief Open the editor on theme @p start (file @p stem); Save keeps it under that stem and
 *        name, then @p cb gets the stem. The theme showing before is restored on close.
 * @version 1
 * @req REQ-LIB-001
 */
void settings_theme_edit(const ui_theme_t *start, const char *stem, settings_pick_cb_t cb, void *ctx);

#ifdef __cplusplus
}
#endif
