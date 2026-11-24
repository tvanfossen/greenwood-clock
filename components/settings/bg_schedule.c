/**
 * @file bg_schedule.c
 * @version 1
 * @brief Background schedule store: NVS blob "bg_sched" in the settings namespace, cached in
 *        RAM behind a mutex.
 */
#include "bg_schedule.h"
#include "clock_anim_pref.h"

#include <string.h>
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "nvs.h"

_Static_assert(BG_SCHED_ANIM_COUNT == CLOCK_ANIM_COUNT, "bg_schedule anim range out of date");

static const char *TAG = "bg_schedule";

/** Same namespace as the settings blob; its own key so the blob schema is untouched. */
#define BG_SCHED_NAMESPACE "clock_cfg"
#define BG_SCHED_KEY       "bg_sched"

static bg_schedule_t     s_sched;
static SemaphoreHandle_t s_lock;

/**
 * @brief Read the stored blob into @p out.
 * @version 1
 * @utility
 * @return ESP_OK, or why nothing valid was stored.
 */
static esp_err_t read_stored(bg_schedule_t *out)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(BG_SCHED_NAMESPACE, NVS_READONLY, &h);
    if (err != ESP_OK) return err;
    size_t len = sizeof(*out);
    err = nvs_get_blob(h, BG_SCHED_KEY, out, &len);
    nvs_close(h);
    if (err == ESP_OK && (len != sizeof(*out) || !bg_schedule_valid(out))) err = ESP_ERR_INVALID_STATE;
    return err;
}

/**
 * @brief Load the stored schedule into RAM (defaults if missing or invalid). Call once
 *        before any other bg_schedule_* function (settings_init does).
 * @version 1
 * @req REQ-BG-004
 * @return ESP_OK, or ESP_ERR_NO_MEM if the mutex could not be created.
 */
esp_err_t bg_schedule_init(void)
{
    if (!s_lock) s_lock = xSemaphoreCreateMutex();
    if (!s_lock) {
        ESP_LOGE(TAG, "mutex create failed");
        return ESP_ERR_NO_MEM;
    }
    bg_schedule_t loaded;
    esp_err_t err = read_stored(&loaded);
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "no valid stored schedule (%s): default (one slot, inherit all)",
                 esp_err_to_name(err));
        bg_schedule_defaults(&loaded);
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_sched = loaded;
    xSemaphoreGive(s_lock);
    ESP_LOGI(TAG, "loaded: %u slot(s), %u profile(s)", loaded.slot_count, loaded.profile_count);
    return ESP_OK;
}

/**
 * @brief Copy the current schedule.
 * @version 1
 * @req REQ-BG-004
 */
void bg_schedule_get(bg_schedule_t *out)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    *out = s_sched;
    xSemaphoreGive(s_lock);
}

/**
 * @brief Write @p s to NVS and commit.
 * @version 1
 * @utility
 * @return ESP_OK or the NVS error.
 */
static esp_err_t write_stored(const bg_schedule_t *s)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(BG_SCHED_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) return err;
    err = nvs_set_blob(h, BG_SCHED_KEY, s, sizeof(*s));
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err;
}

/**
 * @brief Validate, persist and adopt @p s.
 * @version 1
 * @req REQ-BG-004
 * @return ESP_OK, ESP_ERR_INVALID_ARG for an invalid schedule, or the NVS error.
 */
esp_err_t bg_schedule_set(const bg_schedule_t *s)
{
    if (!bg_schedule_valid(s)) {
        ESP_LOGW(TAG, "rejected invalid schedule");
        return ESP_ERR_INVALID_ARG;
    }
    xSemaphoreTake(s_lock, portMAX_DELAY);
    esp_err_t err = write_stored(s);
    if (err == ESP_OK) s_sched = *s;
    xSemaphoreGive(s_lock);
    if (err != ESP_OK) ESP_LOGE(TAG, "save failed: %s", esp_err_to_name(err));
    else ESP_LOGI(TAG, "saved: %u slot(s), %u profile(s)", s->slot_count, s->profile_count);
    return err;
}

/**
 * @brief The profile active at local time @p now.
 * @version 1
 * @req REQ-BG-004
 * @return The profile index; its contents are copied to @p out.
 */
int bg_schedule_active(const struct tm *now, bg_profile_t *out)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    int idx = bg_schedule_profile_at(&s_sched, now->tm_wday, now->tm_hour * 60 + now->tm_min);
    if (out) *out = s_sched.profiles[idx];
    xSemaphoreGive(s_lock);
    return idx;
}
