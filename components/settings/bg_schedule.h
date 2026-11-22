/**
 * @file bg_schedule.h
 * @version 1
 * @brief Background schedule store: the bg_schedule_t model persisted in NVS under its own
 *        key (beside the settings blob, so changing it can never reset Wi-Fi), with a
 *        RAM copy guarded by a mutex. Safe from any task.
 */
#ifndef BG_SCHEDULE_H
#define BG_SCHEDULE_H

#include <time.h>
#include "esp_err.h"
#include "bg_schedule_model.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Load the stored schedule into RAM (defaults if missing or invalid). Call once
 *        before any other bg_schedule_* function (settings_init does).
 * @version 1
 * @req REQ-BG-004
 * @return ESP_OK, or ESP_ERR_NO_MEM if the mutex could not be created.
 */
esp_err_t bg_schedule_init(void);

/**
 * @brief Copy the current schedule.
 * @version 1
 * @req REQ-BG-004
 */
void bg_schedule_get(bg_schedule_t *out);

/**
 * @brief Validate, persist and adopt @p s.
 * @version 1
 * @req REQ-BG-004
 * @return ESP_OK, ESP_ERR_INVALID_ARG for an invalid schedule, or the NVS error.
 */
esp_err_t bg_schedule_set(const bg_schedule_t *s);

/**
 * @brief The profile active at local time @p now.
 * @version 1
 * @req REQ-BG-004
 * @return The profile index; its contents are copied to @p out.
 */
int bg_schedule_active(const struct tm *now, bg_profile_t *out);

#ifdef __cplusplus
}
#endif

#endif /* BG_SCHEDULE_H */
