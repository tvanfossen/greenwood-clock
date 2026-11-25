/**
 * @file sched_store.h
 * @version 1
 * @brief The device's schedule: /sdcard/schedule.json (see sched_json.h) with a RAM copy
 *        guarded by a mutex, saved only when no profile overlaps another of its kind (checked
 *        a year ahead with the device's own sun), and the profile showing now. The first
 *        start without the file converts the old weekly NVS schedule (its first weekday) into
 *        daily profiles. SD I/O: init and set run off the LVGL lock.
 */
#pragma once

#include <stddef.h>
#include <time.h>
#include "esp_err.h"
#include "sched_engine.h"
#include "sched_json.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SCHED_STORE_PATH "/sdcard/schedule.json"

/**
 * @brief Load the schedule (or convert the old one); call once after the SD card mounts.
 * @version 1
 * @req REQ-BG-004
 * @return ESP_OK, or ESP_ERR_NO_MEM when the mutex could not be made.
 */
esp_err_t sched_store_init(void);

/**
 * @brief Copy the schedule's profiles into @p out (at most @p max).
 * @version 1
 * @req REQ-BG-004
 * @return How many.
 */
int sched_store_get(sched_rule_t *out, int max);

/**
 * @brief Check, write and adopt @p n profiles. Refused (nothing changes) when one overlaps
 *        another of its kind; @p why (when non-NULL, @p why_n bytes) then says which.
 * @version 1
 * @req REQ-BG-004
 * @return ESP_OK, ESP_ERR_INVALID_ARG when refused, or the write's error.
 */
esp_err_t sched_store_set(const sched_rule_t *r, int n, char *why, size_t why_n);

/**
 * @brief The profile showing at local time @p now.
 * @version 1
 * @req REQ-BG-004
 * @return Its index, or -1 (the global settings); the profile is copied to @p out when
 *         non-NULL (zeroed, brightness inheriting, when none).
 */
int sched_store_active(const struct tm *now, sched_rule_t *out);

/**
 * @brief The device's sun (sunrise and sunset at its location) for the engine.
 * @version 1
 * @req REQ-BG-004
 * @return The sun source (static).
 */
const sched_sun_src_t *sched_store_sun(void);

#ifdef __cplusplus
}
#endif
