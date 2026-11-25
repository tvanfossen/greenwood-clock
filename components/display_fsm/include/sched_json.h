/**
 * @file sched_json.h
 * @version 1
 * @brief The schedule file (/sdcard/schedule.json) and its API body, read and written. Pure C
 *        over cJSON, host-tested. Readable on purpose:
 *
 *   {"version": 1, "profiles": [
 *     {"name": "Morning", "repeat": "daily", "start": "07:00", "end": "12:00",
 *      "image": "A:/backgrounds/winter_morning.png", "theme": "default"},
 *     {"name": "Evening", "repeat": "daily", "start": "sunset", "end": "22:00"},
 *     {"name": "Christmas", "repeat": "yearly", "from": "12-24", "to": "12-25",
 *      "anim": "Snowfall", "brightness": 80},
 *     {"name": "Birthday", "repeat": "once", "from": "2027-06-03", "to": "2027-06-03",
 *      "start": "sunrise-30", "end": "sunset+60"}]}
 *
 * A profile without "start"/"end" runs all day; "image", "theme", "anim" and "brightness" are
 * optional (absent = the global setting). Window edges: "HH:MM", "sunrise", "sunset", with an
 * optional signed offset in minutes.
 */
#pragma once

#include "sched_rule.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SCHED_MAX_PROFILES 24   /**< Profiles a schedule holds (daily and events together). */
#define SCHED_JSON_VERSION 1

/**
 * @brief Parse a schedule into @p out (at most @p max profiles).
 * @version 1
 * @req REQ-BG-004
 * @return The number of profiles, or -1 when @p json is not a valid schedule (nothing kept).
 */
int sched_from_json(const char *json, sched_rule_t *out, int max);

/**
 * @brief Write @p n profiles as a schedule.
 * @version 1
 * @req REQ-BG-004
 * @return A heap string (free it), or NULL when out of memory.
 */
char *sched_to_json(const sched_rule_t *r, int n);

#ifdef __cplusplus
}
#endif
