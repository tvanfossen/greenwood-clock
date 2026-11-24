/**
 * @file nws_radar_frames.c
 * @version 1
 * @brief Radar loop frame choice (see nws_radar_frames.h).
 */
#include "nws_radar_frames.h"

#include <stdbool.h>

/**
 * @brief Index of the raster with the latest start in [@p oldest_ms, @p limit_ms].
 * @version 1
 * @dg_internal
 * @return The index, or -1 if none.
 */
static int latest_before(const nws_radar_raster_t *r, int n, int64_t limit_ms, int64_t oldest_ms)
{
    int best = -1;
    for (int i = 0; i < n; i++) {
        int64_t s = r[i].start_ms;
        bool fits = s <= limit_ms && s >= oldest_ms;
        if (fits && (best < 0 || s > r[best].start_ms)) best = i;
    }
    return best;
}

/**
 * @brief Pick the loop's frames (see nws_radar_frames.h).
 * @version 1
 * @req REQ-RADAR-003
 * @return Number picked.
 */
int nws_radar_pick(const nws_radar_raster_t *r, int n, int64_t now_s, int min_gap_s,
                   int max_age_s, nws_radar_raster_t *out, int max)
{
    const int64_t oldest_ms = (now_s - max_age_s) * 1000;
    int64_t limit_ms = INT64_MAX;   // the next pick starts at or before this
    int picked = 0;
    while (picked < max) {
        int best = latest_before(r, n, limit_ms, oldest_ms);
        if (best < 0) break;
        out[picked++] = r[best];
        limit_ms = r[best].start_ms - (int64_t)min_gap_s * 1000;
    }
    for (int i = 0; i < picked / 2; i++) {   // newest first -> oldest first
        nws_radar_raster_t t = out[i];
        out[i] = out[picked - 1 - i];
        out[picked - 1 - i] = t;
    }
    return picked;
}
