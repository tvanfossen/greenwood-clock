/**
 * @file nws_radar_frames.h
 * @version 2
 * @brief Which NOAA radar images make up the loop, and how to ask for each one. Pure logic
 *        (no ESP-IDF): host-tested.
 *
 * The time-enabled ImageServer holds one CONUS image every ~6-8 min, each valid from its
 * start to its end (the newest: end == start, still open). Each frame is fetched by its image
 * id: without a time the server returns a default view that lags behind, and asking for the
 * newest at its start time returns the previous image (whose validity ends there).
 *
 * The loop is shown on a fixed grid (hh:00, hh:10, ...): each mark shows the image that was
 * current at that moment, labelled with the mark; the newest image ends the loop at its own
 * time, so the screen is never staler than the server.
 * v2: grid frames (nws_radar_frame_t) instead of every image at its own time.
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

/** One loop frame: the image, and the time it is shown at (and stored under). */
typedef struct {
    nws_radar_raster_t raster;
    int64_t            show_s;   /* a grid mark, or the newest image's own start */
} nws_radar_frame_t;

/**
 * @brief Pick the loop's frames from @p n rasters (any order): the newest image (started no
 *        more than @p max_age_s before @p now_s) at its own start, then each multiple of
 *        @p grid_s before it, back to @p max_age_s, with the image current at that mark (a
 *        mark no image covers is skipped), at most @p max. Written to @p out oldest first.
 * v2: grid frames; @p out takes frames with their shown time.
 * @version 2
 * @req REQ-RADAR-003
 * @return Number picked.
 */
int nws_radar_pick(const nws_radar_raster_t *r, int n, int64_t now_s, int grid_s,
                   int max_age_s, nws_radar_frame_t *out, int max);

#ifdef __cplusplus
}
#endif

#endif /* NWS_RADAR_FRAMES_H */
