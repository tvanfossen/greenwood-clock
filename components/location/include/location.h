/**
 * @file location.h
 * @version 1
 * @brief The clock's place and timezone (REQ-LOC-001): look an address up with the Census
 *        geocoder (HTTPS, once), keep the coordinates and a short label, set the IANA zone
 *        and apply it at once. The address, the place and its coordinates are never logged.
 *        ESP-IDF; the request and answer are pure C (location_census.h, tz.h).
 */
#pragma once

#include "esp_err.h"
#include "location_census.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LOCATION_TZ_LEN 48

/** Where the clock is, and its zone. */
typedef struct {
    location_place_t place;              /**< Label "" when never set by address. */
    char             tz[LOCATION_TZ_LEN];   /**< IANA name; "" when never chosen (the
                                              settings' POSIX rule is in use). */
} location_t;

/**
 * @brief The clock's place and zone now.
 * @version 1
 * @req REQ-LOC-001
 */
void location_get(location_t *out);

/**
 * @brief Look @p address up (blocks for the HTTPS request: not on the LVGL or HTTP-critical
 *        paths without care). @p why says why not, for a person.
 * @version 1
 * @req REQ-LOC-001
 * @return ESP_OK with @p out filled; ESP_ERR_NOT_FOUND when no match; another error when the
 *         geocoder could not be asked.
 */
esp_err_t location_lookup(const char *address, location_place_t *out, char *why, size_t n);

/**
 * @brief Keep @p loc (coordinates, label, zone) and apply its zone now. No zone ("") keeps the
 *        clock's timezone rule; a zone the table does not know is refused.
 * @version 1
 * @req REQ-LOC-001
 * @return ESP_OK, ESP_ERR_INVALID_ARG, or the storage error.
 */
esp_err_t location_save(const location_t *loc);

#ifdef __cplusplus
}
#endif
