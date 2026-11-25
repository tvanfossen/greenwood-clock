/**
 * @file display_states.h
 * @version 1
 * @brief Concrete display state class declarations (implementations in state_*.cpp); complete types enable transit<S>().
 */
// Complete state class definitions for the display FSM.
// Method declarations only — implementations are in individual state_*.cpp files.
// Including this header gives complete types for all states, enabling transit<S>().

#ifndef DISPLAY_STATES_H
#define DISPLAY_STATES_H

#include "display_fsm_base.h"

// ---------------------------------------------------------------------------
// ClockFull — home state, full-screen clock
// ---------------------------------------------------------------------------

/**
 * @brief ClockFull state: the home screen, full-size clock.
 * @version 1
 * @req REQ-CLOCK-001
 * @return Not applicable: type declaration (clew-guard parses this struct header as a function).
 */
struct ClockFull : DisplayFsm
{
    /**
     * @brief Enter the home clock state.
     * v2: (declaration block) ClockFull no longer overrides EvForceState; the base handles it.
     * @version 2
     * @req REQ-CLOCK-001
     */
    void entry() override;
    void exit() override;

    void react(EvWeatherUpdate const &) override;
    void react(EvRadarReady const &) override;
    void react(EvAstroTrigger const &) override;
    void react(EvPhotoTrigger const &) override;
    void react(EvAmbientTrigger const &) override;
};

// ---------------------------------------------------------------------------
// WeatherOverlay — minimized clock + conditions + forecast
// ---------------------------------------------------------------------------

/**
 * @brief WeatherOverlay state: conditions card, forecast strip and sky dial.
 * v2: the sky dial (s_dial).
 * @version 2
 * @req REQ-WX-002
 * @req REQ-WX-007
 * @return Not applicable: type declaration (clew-guard parses this struct header as a function).
 */
struct WeatherOverlay : DisplayFsm
{
    static weather_card_t    *s_card;
    static forecast_strip_t  *s_strip;
    static lv_obj_t          *s_dial;    // sun / moon dial under the clock

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

/**
 * @brief RadarOverlay state: minimized clock over the basemap and radar image.
 * v2: plays the past hour of radar from the SD history (loop_step).
 * @version 2
 * @req REQ-RADAR-001
 * @return Not applicable: type declaration (clew-guard parses this struct header as a function).
 */
struct RadarOverlay : DisplayFsm
{
    static radar_view_t *s_rv;

    /** Off-lock: decode one radar-loop frame, publish it under the lock, re-post if more. */
    static void loop_step();

    void entry() override;
    void exit() override;

    void react(EvDisplayTimeout const &) override;
    void react(EvRadarReady const &) override;
    void react(EvForceState const &) override;
};

// ---------------------------------------------------------------------------
// Astronomy — moon phase, sunrise/sunset, aurora
// ---------------------------------------------------------------------------

/**
 * @brief Astronomy state: moon phase, sunrise/sunset and aurora.
 * @version 1
 * @req REQ-ASTRO-001
 * @return Not applicable: type declaration (clew-guard parses this struct header as a function).
 */
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

    /**
     * @brief Enter the photo slideshow state.
     * v2: class no longer declares apply_clock_contrast (clock uses the user's colour).
     * @version 2
     * @req REQ-PHOTO-001
     */
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

/**
 * @brief SurpriseMessage state: renders a JSON layout pushed via the HTTP API.
 * @version 1
 * @req REQ-SURP-001
 * @return Not applicable: type declaration (clew-guard parses this struct header as a function).
 */
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
