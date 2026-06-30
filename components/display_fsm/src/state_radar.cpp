// components/display_fsm/src/state_radar.cpp
//
// RadarOverlay state — minimized clock + map background + radar PNG overlay.

#include "display_states.h"
#include "display_scheduler.h"
#include "display_widgets.h"
#include "ui_contrast.h"
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

    // Apply pre-decoded radar if available (predecode runs outside LVGL lock)
    if (s_rv) {
        radar_view_apply_radar(s_rv);
        fade_in(radar_view_container(s_rv));
    }

    // Legible minimized clock over the map (map is upright/not flipped). The map
    // covers the clock bg, so override the bg-image default with a map sample.
    clock_widget_t *clk = get_clock();
    const lv_image_dsc_t *map = radar_view_map_dsc();
    if (clk && map) {
        lv_area_t a;
        clock_widget_min_area(&a);
        lv_color_t mean = ui_image_region_mean(map, a.x1, a.y1,
                                               a.x2 - a.x1 + 1, a.y2 - a.y1 + 1);
        clock_widget_set_minimized_color(clk, ui_legible(clock_widget_user_color(clk), mean));
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
    // Staged data was pre-decoded before LVGL lock — just swap it in
    radar_view_apply_radar(s_rv);
    ESP_LOGI(TAG, "RadarOverlay: radar applied");
}

void RadarOverlay::react(EvForceState const &e)
{
    if (e.state == DISPLAY_STATE_RADAR) {
        // Already here — no-op
    } else {
        DisplayFsm::react(e);
    }
}
