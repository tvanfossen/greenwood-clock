/**
 * @file nws_conditions_merge.h
 * @version 1
 * @brief Filling gaps in an observation from other stations: a station's latest observation
 *        can lack temperature, humidity or the description (NWS sends nulls). Pure logic
 *        (host-tested).
 */
#ifndef NWS_CONDITIONS_MERGE_H
#define NWS_CONDITIONS_MERGE_H

#include <stdbool.h>
#include "nws.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Sentinels parse_conditions stores for a missing value. */
#define NWS_TEMP_MISSING     (-999.0f)
#define NWS_HUMIDITY_MISSING (-1)

/**
 * @brief Whether @p c lacks any value the weather card shows (temperature, feels-like,
 *        humidity, description).
 * @version 1
 * @req REQ-WX-002
 * @return true if another station should be asked.
 */
bool nws_conditions_incomplete(const nws_conditions_t *c);

/**
 * @brief Fill each value @p dst lacks from @p src (when @p src has it); values @p dst has
 *        are kept.
 * @version 1
 * @req REQ-WX-002
 * @return Number of values filled.
 */
int nws_conditions_fill(nws_conditions_t *dst, const nws_conditions_t *src);

#ifdef __cplusplus
}
#endif

#endif /* NWS_CONDITIONS_MERGE_H */
