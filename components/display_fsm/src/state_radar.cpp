/**
 * @file state_radar.cpp
 * @version 1
 * @brief RadarOverlay state — minimized clock + map background + radar PNG overlay.
 */

#include "display_states.h"
#include "display_scheduler.h"
#include "display_widgets.h"
#include "display_fsm.h"
#include "radar_loop.h"
#include "esp_lvgl_port.h"
#include "nws.h"
#include "settings.h"
#include "esp_log.h"

static const char *TAG = "state_radar";

// Static member definitions
radar_view_t *RadarOverlay::s_rv = nullptr;

/**
 * @brief Enter the radar state: tint the minimized clock for legibility over the
 *        basemap, minimize it, create the radar view at the configured location,
 *        apply the staged overlay and fade it in, then start loading the radar loop.
 * v2: clock uses the user's exact colour; no background sampling before minimizing.
 * v3: arms the radar loop and posts its first off-lock load.
 * @version 3
 * @req REQ-RADAR-001
 * @req REQ-DISP-001
 */
void RadarOverlay::entry()
{
    set_state_info(DISPLAY_STATE_RADAR, "radar");

    minimize_clock();

    // Get device location from settings
    clock_settings_t cfg;
    settings_load(&cfg);

    s_rv = radar_view_create(s_screen, cfg.latitude, cfg.longitude);

    // Apply pre-decoded radar if available (predecode runs outside LVGL lock)
    if (s_rv) {
        radar_view_apply_radar(s_rv);
        fade_in(radar_view_container(s_rv));
        radar_loop_arm();
        display_event_t evt = {};
        evt.type = DISPLAY_EVT_RADAR_LOOP_LOAD;
        display_fsm_send_event(&evt);
    }
    ESP_LOGI(TAG, "RadarOverlay: entry");
}

/**
 * @brief Leave the radar state: destroy the radar view, then stop the loop and free its frames.
 * v2: stops the radar loop.
 * @version 2
 * @req REQ-RADAR-001
 * @req REQ-DISP-001
 */
void RadarOverlay::exit()
{
    if (s_rv) { radar_view_destroy(s_rv); s_rv = NULL; }
    radar_loop_stop();
    ESP_LOGI(TAG, "RadarOverlay: exit");
}

/**
 * @brief FSM task, off the LVGL lock: decode one radar-loop frame, publish it to playback
 *        under the lock, and post the next load while frames remain. A no-op once the radar
 *        state has exited (radar_loop_stop disarms the loop).
 * @version 1
 * @req REQ-RADAR-003
 */
void RadarOverlay::loop_step()
{
    bool more = radar_loop_load_next();
    lvgl_port_lock(0);
    radar_loop_attach(s_rv);
    lvgl_port_unlock();
    if (more) {
        display_event_t evt = {};
        evt.type = DISPLAY_EVT_RADAR_LOOP_LOAD;
        display_fsm_send_event(&evt);
    }
}

/**
 * @brief Display timeout: return to the full clock.
 * @version 1
 * @req REQ-DISP-002
 */
void RadarOverlay::react(EvDisplayTimeout const &)
{
    transit<ClockFull>();
}

/**
 * @brief New radar staged: swap the pre-decoded overlay into the open view.
 * @version 1
 * @req REQ-RADAR-001
 */
void RadarOverlay::react(EvRadarReady const &)
{
    if (!s_rv) return;
    // Staged data was pre-decoded before LVGL lock — just swap it in
    radar_view_apply_radar(s_rv);
    ESP_LOGI(TAG, "RadarOverlay: radar applied");
}

/**
 * @brief Forced state change: no-op if radar is requested, otherwise defer to
 *        the base FSM handler.
 * @version 1
 * @req REQ-API-003
 * @req REQ-DISP-001
 */
void RadarOverlay::react(EvForceState const &e)
{
    if (e.state == DISPLAY_STATE_RADAR) {
        // Already here — no-op
    } else {
        DisplayFsm::react(e);
    }
}
