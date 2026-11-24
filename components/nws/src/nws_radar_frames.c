/**
 * @file nws_radar_frames.c
 * @version 2
 * @brief Radar loop frame choice (see nws_radar_frames.h).
 * v2: past frames on the grid (the image current at each mark), the newest at its own time.
 */
#include "nws_radar_frames.h"

#include <stdbool.h>
#include <stddef.h>

/**
 * @brief Index of the raster with the latest start in [@p oldest_ms, @p limit_ms].
 * @version 1
 * @utility
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
 * @brief Index of the image current at @p t_ms: the latest start at or before it, whose
 *        validity has not ended by then (none across a server gap).
 * @version 1
 * @utility
 * @return The index, or -1 if none.
 */
static int current_at(const nws_radar_raster_t *r, int n, int64_t t_ms)
{
    int i = latest_before(r, n, t_ms, INT64_MIN);
    bool open = i >= 0 && r[i].end_ms == r[i].start_ms;
    return (i >= 0 && (open || r[i].end_ms > t_ms)) ? i : -1;
}

/**
 * @brief Reverse @p out[0..n) in place (newest first -> oldest first).
 * @version 1
 * @utility
 */
static void reverse(nws_radar_frame_t *out, int n)
{
    for (int i = 0; i < n / 2; i++) {
        nws_radar_frame_t t = out[i];
        out[i] = out[n - 1 - i];
        out[n - 1 - i] = t;
    }
}

/**
 * @brief Pick the loop's frames (see nws_radar_frames.h).
 * v2: grid frames: each mark before the newest shows the image current at it.
 * @version 2
 * @req REQ-RADAR-003
 * @return Number picked.
 */
int nws_radar_pick(const nws_radar_raster_t *r, int n, int64_t now_s, int grid_s,
                   int max_age_s, nws_radar_frame_t *out, int max)
{
    const int64_t oldest_s = now_s - max_age_s;
    int newest = latest_before(r, n, INT64_MAX, oldest_s * 1000);
    if (newest < 0 || max <= 0) return 0;
    int64_t newest_s = r[newest].start_ms / 1000;
    out[0] = (nws_radar_frame_t){ r[newest], newest_s };
    int picked = 1;
    // Marks strictly before the newest: one on its start would show the newest twice.
    for (int64_t m = ((newest_s - 1) / grid_s) * grid_s; m >= oldest_s && picked < max;
         m -= grid_s) {
        int i = current_at(r, n, m * 1000);
        if (i >= 0) out[picked++] = (nws_radar_frame_t){ r[i], m };
    }
    reverse(out, picked);
    return picked;
}
