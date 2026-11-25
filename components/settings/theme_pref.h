/**
 * @file theme_pref.h
 * v2: default stem "default" (was "greenwood").
 * @version 2
 * @brief The global UI theme: the file stem of a theme in /sdcard/themes ("default").
 *
 * Its own NVS key beside the settings blob, like clock_anim_pref: a field in clock_settings_t
 * would bump SETTINGS_VERSION, and a version mismatch resets every setting (WiFi included).
 * A schedule profile may name its own theme, which wins while the profile is active.
 */
#ifndef THEME_PREF_H
#define THEME_PREF_H

#include <stddef.h>
#include "esp_err.h"
#include "bg_schedule_model.h"   // BG_THEME_LEN, bg_theme_name_valid

#ifdef __cplusplus
extern "C" {
#endif

/** The theme used when none is stored (the base look). */
#define THEME_PREF_DEFAULT "default"

/**
 * @brief Read the stored theme name into @p out (BG_THEME_LEN bytes); THEME_PREF_DEFAULT when
 *        none is stored or the stored one is not a valid name.
 * @version 1
 * @req REQ-UI-003
 */
void theme_pref_get(char out[BG_THEME_LEN]);

/**
 * @brief Persist @p name as the global theme.
 * @version 1
 * @req REQ-UI-003
 * @return ESP_OK, ESP_ERR_INVALID_ARG for an invalid name, or the NVS error.
 */
esp_err_t theme_pref_set(const char *name);

#ifdef __cplusplus
}
#endif

#endif /* THEME_PREF_H */
