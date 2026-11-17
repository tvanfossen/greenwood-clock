/**
 * @file clock_anim_pref.h
 * @brief Which ambient animation the clock screen shows (hummingbird / snowfall / none).
 *
 * Stored as its own NVS key beside the settings blob, NOT inside
 * clock_settings_t: adding a field there bumps SETTINGS_VERSION, and
 * settings_load() resets every setting (WiFi included) on a version mismatch.
 */
#ifndef CLOCK_ANIM_PREF_H
#define CLOCK_ANIM_PREF_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CLOCK_ANIM_HUMMINGBIRD = 0,  /**< Default; matches pre-existing behaviour. */
    CLOCK_ANIM_SNOWFALL    = 1,
    CLOCK_ANIM_NONE        = 2,
    CLOCK_ANIM_LEAVES      = 3,  /**< Appended: stored NVS values keep their meaning. */
    CLOCK_ANIM_COUNT
} clock_anim_t;

/**
 * @brief Read the stored choice; missing/invalid values yield CLOCK_ANIM_HUMMINGBIRD.
 * @version 1
 * @req REQ-ANIM-001
 * @return The stored animation choice.
 */
clock_anim_t clock_anim_pref_get(void);

/**
 * @brief Persist the choice to NVS.
 * @version 1
 * @req REQ-ANIM-001
 * @return ESP_OK, ESP_ERR_INVALID_ARG for an out-of-range value, or the NVS error.
 */
esp_err_t clock_anim_pref_set(clock_anim_t anim);

/**
 * @brief Human-readable name for menus and logs.
 * @version 1
 * @req REQ-ANIM-001
 * @return Static string; "?" for out-of-range values.
 */
const char *clock_anim_name(clock_anim_t anim);

/**
 * @brief Parse a display name (case-insensitive), e.g. "snowfall".
 * @version 1
 * @req REQ-ANIM-001
 * @return The matching choice, or CLOCK_ANIM_COUNT when unrecognised.
 */
clock_anim_t clock_anim_from_name(const char *name);

/** Menu display order ("None" last), independent of the stored enum values. */
extern const clock_anim_t CLOCK_ANIM_MENU_ORDER[CLOCK_ANIM_COUNT];

#ifdef __cplusplus
}
#endif

#endif /* CLOCK_ANIM_PREF_H */
