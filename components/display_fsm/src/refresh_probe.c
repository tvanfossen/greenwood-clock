/**
 * @file refresh_probe.c
 * @version 1
 * @brief Display refresh probe: per-refresh render/refresh timing, refresh gaps and dirty-area
 *        counts from LVGL display events (extracted from particle_view's perf stats).
 */
#include "refresh_probe.h"
#include "src/display/lv_display_private.h"  /* inv_p: dirty-area count per refresh */

#include <string.h>
#include "esp_timer.h"

/**
 * @brief Close one refresh: count it if it rendered, and track the longest refresh.
 * @version 1
 * @utility
 */
static void probe_refr_ready(refresh_probe_t *p, int64_t now)
{
    if (!p->refr_t0_us || p->render_t0_us < p->refr_t0_us) return;   // refresh drew nothing
    int64_t d = now - p->refr_t0_us;
    p->refr_us += d;
    if (d > p->refr_max_us) p->refr_max_us = d;
    p->refrs++;
}

/**
 * @brief Display event hook (LVGL task): timestamps refresh/render phases while armed.
 * @version 1
 * @callback
 */
static void probe_event_cb(lv_event_t *e)
{
    refresh_probe_t *p = (refresh_probe_t *)lv_event_get_user_data(e);
    if (!p->armed) return;
    lv_display_t *disp = (lv_display_t *)lv_event_get_target(e);
    int64_t now = esp_timer_get_time();
    switch (lv_event_get_code(e)) {
        case LV_EVENT_REFR_START:
            p->refr_t0_us = now;
            break;
        case LV_EVENT_RENDER_START:
            if (p->last_start_us && now - p->last_start_us > p->gap_max_us) {
                p->gap_max_us = now - p->last_start_us;
            }
            p->last_start_us = now;
            p->render_t0_us = now;
            p->areas += disp->inv_p;
            break;
        case LV_EVENT_RENDER_READY:
            if (p->render_t0_us) p->render_us += now - p->render_t0_us;
            break;
        case LV_EVENT_REFR_READY:
            probe_refr_ready(p, now);
            break;
        default:
            break;
    }
}

/**
 * @brief Register the probe on the default display (disarmed, zeroed).
 * @version 1
 * @req REQ-DISP-004
 */
void refresh_probe_attach(refresh_probe_t *p)
{
    refresh_probe_reset(p, false);
    lv_display_t *disp = lv_display_get_default();
    if (!disp) return;
    lv_display_add_event_cb(disp, probe_event_cb, LV_EVENT_REFR_START, p);
    lv_display_add_event_cb(disp, probe_event_cb, LV_EVENT_REFR_READY, p);
    lv_display_add_event_cb(disp, probe_event_cb, LV_EVENT_RENDER_START, p);
    lv_display_add_event_cb(disp, probe_event_cb, LV_EVENT_RENDER_READY, p);
}

/**
 * @brief Unregister the probe from the default display.
 * @version 1
 * @req REQ-DISP-004
 */
void refresh_probe_detach(refresh_probe_t *p)
{
    lv_display_t *disp = lv_display_get_default();
    if (disp) lv_display_remove_event_cb_with_user_data(disp, probe_event_cb, p);
}

/**
 * @brief Zero the statistics and arm (true) or disarm (false) collection.
 * @version 1
 * @req REQ-DISP-004
 */
void refresh_probe_reset(refresh_probe_t *p, bool armed)
{
    memset(p, 0, sizeof(*p));
    p->armed = armed;
}
