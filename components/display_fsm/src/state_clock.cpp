// components/display_fsm/src/state_clock.cpp
//
// ClockFull state — home state. Full-screen clock display.
// All other states return here after their display duration.

#include "display_states.h"
#include "display_scheduler.h"
#include "esp_log.h"

static const char *TAG = "state_clock";

void ClockFull::entry()
{
    set_state_info(DISPLAY_STATE_CLOCK, "clock");
    restore_clock();
    display_scheduler_get()->return_to_clock();
    ESP_LOGI(TAG, "ClockFull: entry");
}

void ClockFull::exit()
{
    ESP_LOGI(TAG, "ClockFull: exit");
}

// Clock is home — no timeout handler (we're already home)

// Data events can trigger overlay states (via scheduler debounce)
void ClockFull::react(EvWeatherUpdate const &)
{
    if (display_scheduler_get()->try_show(DISPLAY_STATE_WEATHER)) {
        transit<WeatherOverlay>();
    }
}

void ClockFull::react(EvRadarReady const &)
{
    if (display_scheduler_get()->try_show(DISPLAY_STATE_RADAR)) {
        transit<RadarOverlay>();
    }
}

void ClockFull::react(EvAstroTrigger const &)
{
    if (display_scheduler_get()->try_show(DISPLAY_STATE_ASTRONOMY)) {
        transit<Astronomy>();
    }
}

void ClockFull::react(EvPhotoTrigger const &)
{
    if (display_scheduler_get()->try_show(DISPLAY_STATE_PHOTOS)) {
        transit<PhotoSlideshow>();
    }
}

void ClockFull::react(EvAmbientTrigger const &)
{
    if (display_scheduler_get()->try_show(DISPLAY_STATE_AMBIENT)) {
        transit<AmbientDashboard>();
    }
}

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
