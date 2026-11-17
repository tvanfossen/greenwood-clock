/**
 * @file state_clock.cpp
 * @version 1
 * @brief ClockFull home state: full-screen clock; data and trigger events open other states through the scheduler.
 */
// ClockFull state — home state. Full-screen clock display.
// All other states return here after their display duration.

#include "display_states.h"
#include "display_scheduler.h"
#include "esp_log.h"

static const char *TAG = "state_clock";

/**
 * @brief Enter the home state: record state info, restore the full clock and animation, and stop the scheduler's return timer.
 * @version 1
 * @req REQ-CLOCK-001
 * @req REQ-DISP-002
 */
void ClockFull::entry()
{
    set_state_info(DISPLAY_STATE_CLOCK, "clock");
    restore_clock();
    display_scheduler_get()->return_to_clock();
    ESP_LOGI(TAG, "ClockFull: entry");
}

/**
 * @brief Leave the home state (logs only).
 * @version 1
 * @req REQ-CLOCK-001
 */
void ClockFull::exit()
{
    ESP_LOGI(TAG, "ClockFull: exit");
}

// Clock is home — no timeout handler (we're already home)

// Data events can trigger overlay states (via scheduler debounce)
/**
 * @brief Open WeatherOverlay if the scheduler allows weather (enabled, condition, cooldown).
 * @version 1
 * @req REQ-DISP-002
 * @req REQ-WX-002
 */
void ClockFull::react(EvWeatherUpdate const &)
{
    if (display_scheduler_get()->try_show(DISPLAY_STATE_WEATHER)) {
        transit<WeatherOverlay>();
    }
}

/**
 * @brief Open RadarOverlay if the scheduler allows radar (enabled, condition, cooldown).
 * @version 1
 * @req REQ-DISP-002
 * @req REQ-RADAR-002
 */
void ClockFull::react(EvRadarReady const &)
{
    if (display_scheduler_get()->try_show(DISPLAY_STATE_RADAR)) {
        transit<RadarOverlay>();
    }
}

/**
 * @brief Open Astronomy if the scheduler allows astronomy (enabled, condition, cooldown).
 * @version 1
 * @req REQ-DISP-002
 * @req REQ-ASTRO-001
 */
void ClockFull::react(EvAstroTrigger const &)
{
    if (display_scheduler_get()->try_show(DISPLAY_STATE_ASTRONOMY)) {
        transit<Astronomy>();
    }
}

/**
 * @brief Open PhotoSlideshow if the scheduler allows photos (enabled, condition, cooldown).
 * @version 1
 * @req REQ-DISP-002
 * @req REQ-PHOTO-001
 */
void ClockFull::react(EvPhotoTrigger const &)
{
    if (display_scheduler_get()->try_show(DISPLAY_STATE_PHOTOS)) {
        transit<PhotoSlideshow>();
    }
}

/**
 * @brief Open AmbientDashboard if the scheduler allows ambient (enabled, condition, cooldown).
 * @version 1
 * @req REQ-DISP-002
 * @req REQ-AMB-001
 */
void ClockFull::react(EvAmbientTrigger const &)
{
    if (display_scheduler_get()->try_show(DISPLAY_STATE_AMBIENT)) {
        transit<AmbientDashboard>();
    }
}

/**
 * @brief Force a state from the clock (bypassing cooldown via force_show) or open Settings; other ids are ignored.
 * @version 1
 * @req REQ-DISP-002
 * @req REQ-API-003
 */
void ClockFull::react(EvForceState const &e)
{
    switch (e.state) {
        case DISPLAY_STATE_WEATHER:
            display_scheduler_get()->force_show(DISPLAY_STATE_WEATHER);
            transit<WeatherOverlay>();
            break;
        case DISPLAY_STATE_RADAR:
            display_scheduler_get()->force_show(DISPLAY_STATE_RADAR);
            transit<RadarOverlay>();
            break;
        case DISPLAY_STATE_ASTRONOMY:
            display_scheduler_get()->force_show(DISPLAY_STATE_ASTRONOMY);
            transit<Astronomy>();
            break;
        case DISPLAY_STATE_PHOTOS:
            display_scheduler_get()->force_show(DISPLAY_STATE_PHOTOS);
            transit<PhotoSlideshow>();
            break;
        case DISPLAY_STATE_AMBIENT:
            display_scheduler_get()->force_show(DISPLAY_STATE_AMBIENT);
            transit<AmbientDashboard>();
            break;
        case DISPLAY_STATE_SETTINGS:
            transit<Settings>();
            break;
        default:
            break;
    }
}

// FSM_INITIAL_STATE must be in the same TU as the full ClockFull definition
FSM_INITIAL_STATE(DisplayFsm, ClockFull)
