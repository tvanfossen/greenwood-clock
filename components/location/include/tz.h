/**
 * @file tz.h
 * @version 1
 * @brief The IANA timezones the clock can be set to (every canonical zone, plus UTC), each
 *        with the POSIX rule its C library runs on, grouped by region ("America") for the
 *        picker. The table is generated from tzdata (tz_table.c). Pure C.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/** One zone: its IANA name and POSIX rule. */
typedef struct {
    const char *name;    /**< "America/Chicago". */
    const char *posix;   /**< "CST6CDT,M3.2.0,M11.1.0". */
} tz_zone_t;

extern const tz_zone_t TZ_ZONES[];   /**< Sorted by name. */
extern const int       TZ_ZONE_COUNT;

/**
 * @brief The POSIX rule of zone @p name.
 * @version 1
 * @req REQ-LOC-001
 * @return It, or NULL when @p name is not a zone.
 */
const char *tz_posix(const char *name);

#define TZ_REGION_LEN 16

/**
 * @brief The regions (the part before the first '/': "Africa", "America", ..., "UTC"), in
 *        order, into @p out (at most @p max).
 * @version 1
 * @req REQ-LOC-001
 * @return How many.
 */
int tz_regions(char out[][TZ_REGION_LEN], int max);

/**
 * @brief The zones of region @p region, in order, as indices into TZ_ZONES, into @p out (at
 *        most @p max). Their place names start after "@p region/" ("UTC" is its own).
 * @version 1
 * @req REQ-LOC-001
 * @return How many.
 */
int tz_zones_in(const char *region, int *out, int max);

/**
 * @brief The place part of zone @p name: after its region ("America/Argentina/Salta" ->
 *        "Argentina/Salta"; "UTC" -> "UTC").
 * @version 1
 * @req REQ-LOC-001
 * @return A pointer into @p name.
 */
const char *tz_place(const char *name);

#ifdef __cplusplus
}
#endif
