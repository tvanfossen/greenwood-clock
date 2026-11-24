/**
 * @file nws_radar_frames.h
 * @version 1
 * @brief Which NOAA radar images make up the loop, and how to ask for each one. Pure logic
 *        (no ESP-IDF): host-tested.
 *
 * The time-enabled ImageServer holds one CONUS image every ~6-8 min, each valid from its
 * start to its end (the newest: end == start, still open). Each frame is fetched by its image
 * id: without a time the server returns a default view that lags behind, and asking for the
 * newest at its start time returns the previous image (whose validity ends there).
 */
#ifndef NWS_RADAR_FRAMES_H
#define NWS_RADAR_FRAMES_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** One image on the server: its id and validity (epoch milliseconds). */
typedef struct {
    int64_t id;
    int64_t start_ms;
    int64_t end_ms;   /* == start_ms for the newest (still open) */
} nws_radar_raster_t;

/**
 * @brief Pick the loop's frames from @p n rasters (any order): the newest, then going back,
 *        each at least @p min_gap_s before the last picked, none older than @p max_age_s
 *        before @p now_s, at most @p max. Written to @p out oldest first.
 * @version 1
 * @req REQ-RADAR-003
 * @return Number picked.
 */
int nws_radar_pick(const nws_radar_raster_t *r, int n, int64_t now_s, int min_gap_s,
                   int max_age_s, nws_radar_raster_t *out, int max);

#ifdef __cplusplus
}
#endif

#endif /* NWS_RADAR_FRAMES_H */
