/**
 * @file clock_anim_pref.c
 * @brief NVS persistence for the clock-screen animation choice.
 */
#include "clock_anim_pref.h"

#include "esp_log.h"
#include "nvs.h"
#include <strings.h>

static const char *TAG = "clock_anim";

/** Same namespace as the settings blob; separate key so the blob schema is untouched. */
#define CLOCK_ANIM_NAMESPACE "clock_cfg"
#define CLOCK_ANIM_KEY       "clock_anim"

static const char *const s_names[CLOCK_ANIM_COUNT] = {
    [CLOCK_ANIM_HUMMINGBIRD] = "Hummingbird",
    [CLOCK_ANIM_SNOWFALL]    = "Snowfall",
    [CLOCK_ANIM_NONE]        = "None",
    [CLOCK_ANIM_LEAVES]      = "Falling Leaves",
};

const clock_anim_t CLOCK_ANIM_MENU_ORDER[CLOCK_ANIM_COUNT] = {
    CLOCK_ANIM_HUMMINGBIRD, CLOCK_ANIM_SNOWFALL, CLOCK_ANIM_LEAVES, CLOCK_ANIM_NONE,
};

/**
 * @brief Read the stored choice, defaulting to the hummingbird.
 * @version 1
 * @req REQ-ANIM-001
 * @return Stored choice or CLOCK_ANIM_HUMMINGBIRD.
 */
clock_anim_t clock_anim_pref_get(void)
{
    uint8_t raw = CLOCK_ANIM_HUMMINGBIRD;
    nvs_handle_t h;
    esp_err_t err = nvs_open(CLOCK_ANIM_NAMESPACE, NVS_READONLY, &h);
    if (err == ESP_OK) {
        err = nvs_get_u8(h, CLOCK_ANIM_KEY, &raw);
        nvs_close(h);
    }
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "no stored choice (%s) — default %s", esp_err_to_name(err),
                 s_names[CLOCK_ANIM_HUMMINGBIRD]);
        raw = CLOCK_ANIM_HUMMINGBIRD;
    } else if (raw >= CLOCK_ANIM_COUNT) {
        ESP_LOGW(TAG, "invalid stored value %u — default %s", raw, s_names[CLOCK_ANIM_HUMMINGBIRD]);
        raw = CLOCK_ANIM_HUMMINGBIRD;
    }
    return (clock_anim_t)raw;
}

/**
 * @brief Write the choice and commit.
 * @version 1
 * @req REQ-ANIM-001
 * @return ESP_OK or the failing NVS error (logged).
 */
esp_err_t clock_anim_pref_set(clock_anim_t anim)
{
    if ((unsigned)anim >= CLOCK_ANIM_COUNT) {
        ESP_LOGE(TAG, "set: out-of-range value %d", (int)anim);
        return ESP_ERR_INVALID_ARG;
    }
    nvs_handle_t h;
    esp_err_t err = nvs_open(CLOCK_ANIM_NAMESPACE, NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_set_u8(h, CLOCK_ANIM_KEY, (uint8_t)anim);
        if (err == ESP_OK) err = nvs_commit(h);
        nvs_close(h);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "set %s failed: %s", s_names[anim], esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "saved: %s", s_names[anim]);
    }
    return err;
}

/**
 * @brief Display name for a choice.
 * @version 1
 * @req REQ-ANIM-001
 * @return Static name string, or "?".
 */
const char *clock_anim_name(clock_anim_t anim)
{
    return ((unsigned)anim < CLOCK_ANIM_COUNT) ? s_names[anim] : "?";
}

/**
 * @brief Parse a display name (case-insensitive) into a choice.
 * @version 1
 * @req REQ-ANIM-001
 * @return The matching choice, or CLOCK_ANIM_COUNT when unrecognised.
 */
clock_anim_t clock_anim_from_name(const char *name)
{
    clock_anim_t found = CLOCK_ANIM_COUNT;
    for (int i = 0; name && i < CLOCK_ANIM_COUNT; i++) {
        if (strcasecmp(name, s_names[i]) == 0) found = (clock_anim_t)i;
    }
    return found;
}
