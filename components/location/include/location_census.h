/**
 * @file location_census.h
 * @version 1
 * @brief Finding a place from an address with the US Census Bureau's geocoder: the request
 *        URL for a one-line address, and what its JSON answer says (the place's coordinates,
 *        rounded to about 100 m, and a short "City, ST" label; the street address is not
 *        kept). Pure C.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LOCATION_LABEL_LEN 48

/** A place: a short label and its coordinates. */
typedef struct {
    char  label[LOCATION_LABEL_LEN];   /**< "Chicago, IL". */
    float lat, lon;                    /**< Degrees, rounded to 3 decimals. */
} location_place_t;

/**
 * @brief The geocoder's URL for @p address (percent-encoded) into @p out.
 * @version 1
 * @req REQ-LOC-001
 * @return Its length, or 0 when the address is empty or does not fit.
 */
size_t location_census_url(const char *address, char *out, size_t n);

/**
 * @brief Read the geocoder's answer @p json into @p out (its first match).
 * @version 1
 * @req REQ-LOC-001
 * @return Whether it matched the address.
 */
bool location_census_parse(const char *json, location_place_t *out);

#ifdef __cplusplus
}
#endif
