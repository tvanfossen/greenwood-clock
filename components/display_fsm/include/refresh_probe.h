/**
 * @file refresh_probe.h
 * @version 1
 * @brief Display refresh probe: times LVGL refreshes (render vs whole refresh incl. flush), the
 *        gap between refreshes and the dirty areas per refresh while armed. Shared by the
 *        particle views and the clock morph so frame-rate claims come from measurement.
 *        All functions require the LVGL lock.
 */
#ifndef REFRESH_PROBE_H
#define REFRESH_PROBE_H

#include <stdbool.h>
#include <stdint.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Accumulated refresh statistics since the last reset. Only refreshes that rendered count. */
typedef struct {
    uint32_t refrs;          /**< Refreshes that rendered. */
    uint32_t areas;          /**< Dirty areas summed over those refreshes. */
    int64_t  render_us;      /**< Summed render phase time. */
    int64_t  refr_us;        /**< Summed whole-refresh time (render + flush). */
    int64_t  refr_max_us;    /**< Longest single refresh. */
    int64_t  gap_max_us;     /**< Longest interval between consecutive refresh starts. */
    /* Internal timestamps. */
    int64_t  refr_t0_us, render_t0_us, last_start_us;
    bool     armed;
} refresh_probe_t;

/**
 * @brief Register the probe on the default display (disarmed, zeroed).
 * @version 1
 * @req REQ-DISP-004
 */
void refresh_probe_attach(refresh_probe_t *p);

/**
 * @brief Unregister the probe from the default display.
 * @version 1
 * @req REQ-DISP-004
 */
void refresh_probe_detach(refresh_probe_t *p);

/**
 * @brief Zero the statistics and arm (true) or disarm (false) collection.
 * @version 1
 * @req REQ-DISP-004
 */
void refresh_probe_reset(refresh_probe_t *p, bool armed);

#ifdef __cplusplus
}
#endif

#endif /* REFRESH_PROBE_H */
