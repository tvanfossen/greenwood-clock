/**
 * @file sched_store.h
 * v2: sched_store_sun_table (the phone panel's sun).
 * @version 2
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
 * v2: a gap's look is sched_rule_gap.
 * @version 2
 * @req REQ-BG-004
 * @return Its index, or -1 (a gap); the profile is copied to @p out when non-NULL (the gap's
 *         look, sched_rule_gap, when none).
 */
int sched_store_active(const struct tm *now, sched_rule_t *out);

/**
 * @brief The clock moved (Settings > Location): read the place again and forget the sun times
 *        found for the old one. Safe before init.
 * @version 1
 * @req REQ-LOC-001
 */
void sched_store_location_changed(void);

/**
 * @brief The device's sun (sunrise and sunset at its location) for the engine.
 * @version 1
 * @req REQ-BG-004
 * @return The sun source (static).
 */
const sched_sun_src_t *sched_store_sun(void);

/**
 * @brief Sunrise and sunset for @p days days from @p from at the device's location, into
 *        @p out (safe from any task; the phone panel's overlap check uses them).
 * @version 1
 * @req REQ-BG-004
 */
void sched_store_sun_table(sched_date_t from, int days, sched_sun_t *out);

/**
 * @brief Today as a local date.
 * @version 1
 * @req REQ-BG-004
 * @return The date.
 */
sched_date_t sched_store_today(void);

#ifdef __cplusplus
}
#endif
