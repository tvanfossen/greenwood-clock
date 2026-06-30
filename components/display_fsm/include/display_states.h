// components/display_fsm/include/display_states.h
//
// Complete state class definitions for the display FSM.
// Method declarations only — implementations are in individual state_*.cpp files.
// Including this header gives complete types for all states, enabling transit<S>().

#ifndef DISPLAY_STATES_H
#define DISPLAY_STATES_H

#include "display_fsm_base.h"

// ---------------------------------------------------------------------------
// ClockFull — home state, full-screen clock
// ---------------------------------------------------------------------------

struct ClockFull : DisplayFsm
{
    void entry() override;
    void exit() override;

    void react(EvWeatherUpdate const &) override;
    void react(EvRadarReady const &) override;
    void react(EvAstroTrigger const &) override;
    void react(EvPhotoTrigger const &) override;
    void react(EvAmbientTrigger const &) override;
    void react(EvForceState const &) override;
};

// ---------------------------------------------------------------------------
// WeatherOverlay — minimized clock + conditions + forecast
// ---------------------------------------------------------------------------

struct WeatherOverlay : DisplayFsm
{
    static weather_card_t    *s_card;
    static forecast_strip_t  *s_strip;

    void entry() override;
    void exit() override;

    void react(EvDisplayTimeout const &) override;
    void react(EvWeatherUpdate const &) override;
    void react(EvForecastUpdate const &) override;
    void react(EvForceState const &) override;
};

// ---------------------------------------------------------------------------
// RadarOverlay — minimized clock + map + radar PNG
// ---------------------------------------------------------------------------

struct RadarOverlay : DisplayFsm
{
    static radar_view_t *s_rv;

    void entry() override;
    void exit() override;

    void react(EvDisplayTimeout const &) override;
    void react(EvRadarReady const &) override;
    void react(EvForceState const &) override;
};

// ---------------------------------------------------------------------------
// Astronomy — moon phase, sunrise/sunset, aurora
// ---------------------------------------------------------------------------

struct Astronomy : DisplayFsm
{
    static lv_obj_t *s_container;

    void entry() override;
    void exit() override;

    void react(EvDisplayTimeout const &) override;
};

// ---------------------------------------------------------------------------
// PhotoSlideshow — SD card image rotation
// ---------------------------------------------------------------------------

struct PhotoSlideshow : DisplayFsm
{
    static image_rotator_t *s_rotator;
    static lv_obj_t        *s_empty_label;
    static void            *s_advance_timer;   // FreeRTOS TimerHandle_t
    static bool             s_first_shown;

    void entry() override;
    void exit() override;

    void react(EvDisplayTimeout const &) override;

    // Slideshow tick (DISPLAY_EVT_PHOTO_ADVANCE), driven by the FreeRTOS timer.
    // decode_next() runs OFF the LVGL lock (the slow PNG decode); present() runs
    // UNDER the lock (fast descriptor swap). Both no-op if not in the photo state.
    static void decode_next();
    static void present();
    static void prefetch();
    // Sample the shown photo behind the minimized clock and set a legible
    // (OKLCH) clock colour. Call under the LVGL lock after present().
    static void apply_clock_contrast();
};

// ---------------------------------------------------------------------------
// AmbientDashboard — aggregate info panel
// ---------------------------------------------------------------------------

struct AmbientDashboard : DisplayFsm
{
    static lv_obj_t *s_container;

    void entry() override;
    void exit() override;

    void react(EvDisplayTimeout const &) override;
};

// ---------------------------------------------------------------------------
// SurpriseMessage — JSON layout DSL rendering
// ---------------------------------------------------------------------------

struct SurpriseMessage : DisplayFsm
{
    static json_layout_t *s_layout;

    void entry() override;
    void exit() override;

    void react(EvDisplayTimeout const &) override;
    void react(EvSurpriseMessage const &) override;
    void react(EvForceState const &) override;
};

// ---------------------------------------------------------------------------
// Settings — sub-states for each settings screen
// ---------------------------------------------------------------------------

struct Settings : DisplayFsm
{
    void entry() override;
    void exit() override;

    void react(EvGesture const &) override;
    void react(EvSettingsBack const &) override;
    void react(EvClockUpdate const &) override;
    void react(EvSurpriseMessage const &) override;
    void react(EvForceState const &) override;
};

#endif // DISPLAY_STATES_H
