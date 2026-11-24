/**
 * @file theme_store.h
 * v2: theme_store_save.
 * @version 2
 * @brief Theme files on the SD card (/sdcard/themes/<name>.json): the list of installed
 *        themes (cached, so menus never touch the card under the LVGL lock) and loading one.
 *        Rescan and load do file I/O: call them OFF the LVGL lock (fsm_task).
 */
#pragma once

#include "ui_theme.h"
#include "bg_schedule_model.h"   // BG_THEME_LEN

#ifdef __cplusplus
extern "C" {
#endif

#define THEME_STORE_DIR "/sdcard/themes"
#define THEME_STORE_MAX 12

/** One installed theme: its file stem and the name inside it. */
typedef struct {
    char stem[BG_THEME_LEN];          /**< File name without ".json" ("greenwood-night"). */
    char name[UI_THEME_NAME_LEN];     /**< Display name ("Greenwood Night"). */
} theme_entry_t;

/**
 * @brief List the valid theme files into @p out directly (no cache, no LVGL lock: for HTTP
 *        handlers, which may run before the display exists). SD I/O.
 * @version 1
 * @req REQ-UI-003
 * @return How many.
 */
int theme_store_scan(theme_entry_t out[THEME_STORE_MAX]);

/**
 * @brief Re-read the list of valid theme files into the cache. Off-lock (SD I/O).
 * @version 1
 * @req REQ-UI-003
 */
void theme_store_rescan(void);

/**
 * @brief Number of cached themes.
 * @version 1
 * @req REQ-UI-003
 * @return 0..THEME_STORE_MAX.
 */
int theme_store_count(void);

/**
 * @brief Cached theme @p i.
 * @version 1
 * @req REQ-UI-003
 * @return The entry, or NULL when out of range.
 */
const theme_entry_t *theme_store_entry(int i);

/**
 * @brief The display name of theme @p stem from the cache.
 * @version 1
 * @req REQ-UI-003
 * @return The name, or @p stem itself when it is not cached.
 */
const char *theme_store_name(const char *stem);

/**
 * @brief Read and parse theme @p stem. Off-lock (SD I/O).
 * @version 1
 * @req REQ-UI-003
 * @return Whether it loaded (@p out set only then).
 */
bool theme_store_load(const char *stem, ui_theme_t *out);

/**
 * @brief Write @p t to THEME_STORE_DIR/<stem>.json (replacing it). Off-lock (SD I/O).
 * @version 1
 * @req REQ-UI-004
 * @return Whether it was written.
 */
bool theme_store_save(const char *stem, const ui_theme_t *t);

#ifdef __cplusplus
}
#endif
