/**
 * @file nws_conditions_merge.c
 * @version 1
 * @brief Filling gaps in an observation from other stations (see nws_conditions_merge.h).
 */
#include "nws_conditions_merge.h"

#include <string.h>

/**
 * @brief Whether a temperature is the missing-value sentinel (anything below -200 C).
 * @version 1
 * @utility
 * @return true if missing.
 */
static bool temp_missing(float t)
{
    return t <= -200.0f;
}

/**
 * @brief Copy temperature @p src into @p dst if @p dst is missing and @p src is not.
 * @version 1
 * @utility
 * @return 1 if filled, else 0.
 */
static int fill_temp(float *dst, float src)
{
    if (!temp_missing(*dst) || temp_missing(src)) return 0;
    *dst = src;
    return 1;
}

/**
 * @brief Whether @p c lacks any value the weather card shows.
 * @version 1
 * @req REQ-WX-002
 * @return true if another station should be asked.
 */
bool nws_conditions_incomplete(const nws_conditions_t *c)
{
    return temp_missing(c->temp_c) || temp_missing(c->feels_like_c)
           || c->humidity == NWS_HUMIDITY_MISSING || c->description[0] == '\0';
}

/**
 * @brief Fill each value @p dst lacks from @p src.
 * @version 1
 * @req REQ-WX-002
 * @return Number of values filled.
 */
int nws_conditions_fill(nws_conditions_t *dst, const nws_conditions_t *src)
{
    int n = fill_temp(&dst->temp_c, src->temp_c) + fill_temp(&dst->feels_like_c, src->feels_like_c)
            + fill_temp(&dst->dew_point_c, src->dew_point_c);
    if (dst->humidity == NWS_HUMIDITY_MISSING && src->humidity != NWS_HUMIDITY_MISSING) {
        dst->humidity = src->humidity;
        n++;
    }
    if (dst->description[0] == '\0' && src->description[0] != '\0') {
        memcpy(dst->description, src->description, sizeof(dst->description));
        n++;
    }
    return n;
}
