/**
 * @file tz.c
 * @version 1
 * @brief The timezone table's lookups (see tz.h). Pure C.
 */
#include "tz.h"

#include <stdbool.h>
#include <string.h>

/**
 * @brief The length of @p name's region (up to its first '/', or all of it).
 * @version 1
 * @utility
 * @return It.
 */
static size_t region_len(const char *name)
{
    const char *slash = strchr(name, '/');
    return slash ? (size_t)(slash - name) : strlen(name);
}

/**
 * @brief The POSIX rule of a zone.
 * @version 1
 * @req REQ-LOC-001
 * @return It, or NULL.
 */
const char *tz_posix(const char *name)
{
    const char *rule = NULL;
    for (int i = 0; name && !rule && i < TZ_ZONE_COUNT; i++) {
        if (strcmp(TZ_ZONES[i].name, name) == 0) rule = TZ_ZONES[i].posix;
    }
    return rule;
}

/**
 * @brief The regions, in order.
 * @version 1
 * @req REQ-LOC-001
 * @return How many.
 */
int tz_regions(char out[][TZ_REGION_LEN], int max)
{
    int n = 0;
    for (int i = 0; i < TZ_ZONE_COUNT && n < max; i++) {
        size_t len = region_len(TZ_ZONES[i].name);
        bool fresh = n == 0 || strlen(out[n - 1]) != len ||
                     strncmp(out[n - 1], TZ_ZONES[i].name, len) != 0;
        if (fresh && len < TZ_REGION_LEN) {
            memcpy(out[n], TZ_ZONES[i].name, len);   // the region: this name's first part
            out[n++][len] = '\0';
        }
    }
    return n;
}

/**
 * @brief The zones of a region.
 * @version 1
 * @req REQ-LOC-001
 * @return How many.
 */
int tz_zones_in(const char *region, int *out, int max)
{
    size_t len = region_len(region);
    int n = 0;
    for (int i = 0; i < TZ_ZONE_COUNT && n < max; i++) {
        const char *z = TZ_ZONES[i].name;
        if (region_len(z) == len && strncmp(z, region, len) == 0) out[n++] = i;
    }
    return n;
}

/**
 * @brief The place part of a zone's name.
 * @version 1
 * @req REQ-LOC-001
 * @return A pointer into @p name.
 */
const char *tz_place(const char *name)
{
    const char *slash = strchr(name, '/');
    return slash ? slash + 1 : name;
}
