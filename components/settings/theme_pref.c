/**
 * @file theme_pref.c
 * @version 1
 * @brief NVS persistence for the global UI theme name.
 */
#include "theme_pref.h"

#include "esp_log.h"
#include "nvs.h"
#include <string.h>

static const char *TAG = "theme_pref";

/** Same namespace as the settings blob; its own key so the blob schema is untouched. */
#define THEME_NAMESPACE "clock_cfg"
#define THEME_KEY       "ui_theme"

/**
 * @brief Read the stored theme name, defaulting to THEME_PREF_DEFAULT.
 * @version 1
 * @req REQ-UI-003
 */
void theme_pref_get(char out[BG_THEME_LEN])
{
    size_t len = BG_THEME_LEN;
    nvs_handle_t h;
    esp_err_t err = nvs_open(THEME_NAMESPACE, NVS_READONLY, &h);
    if (err == ESP_OK) {
        err = nvs_get_str(h, THEME_KEY, out, &len);
        nvs_close(h);
    }
    if (err != ESP_OK || !bg_theme_name_valid(out)) {
        ESP_LOGI(TAG, "no valid stored theme (%s) — default %s", esp_err_to_name(err),
                 THEME_PREF_DEFAULT);
        strcpy(out, THEME_PREF_DEFAULT);
    }
}

/**
 * @brief Write the theme name and commit.
 * @version 1
 * @req REQ-UI-003
 * @return ESP_OK, ESP_ERR_INVALID_ARG, or the failing NVS error (logged).
 */
esp_err_t theme_pref_set(const char *name)
{
    esp_err_t err = ESP_ERR_INVALID_ARG;
    nvs_handle_t h;
    if (bg_theme_name_valid(name)) err = nvs_open(THEME_NAMESPACE, NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_set_str(h, THEME_KEY, name);
        if (err == ESP_OK) err = nvs_commit(h);
        nvs_close(h);
    }
    if (err != ESP_OK) ESP_LOGE(TAG, "set \"%s\" failed: %s", name ? name : "(null)",
                                esp_err_to_name(err));
    else ESP_LOGI(TAG, "saved: %s", name);
    return err;
}
