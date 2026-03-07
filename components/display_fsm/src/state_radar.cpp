// components/display_fsm/src/state_radar.cpp
//
// RadarOverlay state — minimized clock + map background + radar PNG overlay.

#include "display_states.h"
#include "display_scheduler.h"
#include "display_widgets.h"
#include "nws.h"
#include "settings.h"
#include "esp_log.h"

static const char *TAG = "state_radar";

// Static member definitions
radar_view_t *RadarOverlay::s_rv = nullptr;

void RadarOverlay::entry()
{
    set_state_info(DISPLAY_STATE_RADAR, "radar");
    minimize_clock();

    // Get device location from settings
    clock_settings_t cfg;
    settings_load(&cfg);

    s_rv = radar_view_create(s_screen, cfg.latitude, cfg.longitude);

    // Load cached radar if available
    if (s_rv) {
        size_t len = 0;
        const uint8_t *png = nws_get_radar_png(&len);
        if (png && len > 0) {
            radar_view_set_radar(s_rv, png, len);
        }
        fade_in(radar_view_container(s_rv));
    }
    ESP_LOGI(TAG, "RadarOverlay: entry");
}

void RadarOverlay::exit()
{
    if (s_rv) { radar_view_destroy(s_rv); s_rv = NULL; }
    ESP_LOGI(TAG, "RadarOverlay: exit");
}

void RadarOverlay::react(EvDisplayTimeout const &)
{
    transit<ClockFull>();
}

void RadarOverlay::react(EvRadarReady const &)
{
    if (!s_rv) return;
    size_t len = 0;
    const uint8_t *png = nws_get_radar_png(&len);
    if (png && len > 0) {
        radar_view_set_radar(s_rv, png, len);
        ESP_LOGI(TAG, "RadarOverlay: radar updated (%zu bytes)", len);
    }
}

void RadarOverlay::react(EvForceState const &e)
{
    if (e.state == DISPLAY_STATE_RADAR) {
        // Already here — no-op
    } else {
        DisplayFsm::react(e);
    }
}
