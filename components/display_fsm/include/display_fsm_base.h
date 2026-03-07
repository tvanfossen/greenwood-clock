// components/display_fsm/include/display_fsm_base.h
//
// DisplayFsm base class declaration.
// All concrete states inherit from this.

#ifndef DISPLAY_FSM_BASE_H
#define DISPLAY_FSM_BASE_H

#include "tinyfsm.hpp"
#include "display_events.h"
#include "display_widgets.h"

// ---------------------------------------------------------------------------
// TinyFSM event wrappers (C++ structs wrapping C event payloads)
// ---------------------------------------------------------------------------

struct EvDisplayTimeout  : tinyfsm::Event { };
struct EvWeatherUpdate   : tinyfsm::Event { };
struct EvForecastUpdate  : tinyfsm::Event { };
struct EvAlertReceived   : tinyfsm::Event { };
struct EvRadarReady      : tinyfsm::Event { };
struct EvAstroTrigger    : tinyfsm::Event { };
struct EvPhotoTrigger    : tinyfsm::Event { };
struct EvAmbientTrigger  : tinyfsm::Event { };
struct EvGesture         : tinyfsm::Event { lv_dir_t dir; };
struct EvSettingsBack    : tinyfsm::Event { };
struct EvSurpriseMessage : tinyfsm::Event { const char *json_str; uint32_t duration_s; };
struct EvForceState      : tinyfsm::Event { display_state_id_t state; };
struct EvScheduleConfig  : tinyfsm::Event { display_evt_schedule_config_t cfg; };
struct EvClockUpdate     : tinyfsm::Event { struct tm time; };

// ---------------------------------------------------------------------------

struct DisplayFsm : tinyfsm::Fsm<DisplayFsm>
{
    // -----------------------------------------------------------------------
    // Shared state — persists across state transitions (static)
    // -----------------------------------------------------------------------
protected:
    static clock_widget_t      *s_clock;
    static alert_banner_t      *s_alert_banner;
    static lv_obj_t            *s_screen;
    static display_state_id_t   s_state_id;
    static const char          *s_state_name;

    // Stashed surprise data — set before transit, read in SurpriseMessage::entry()
    static const char          *s_surprise_json;
    static uint32_t             s_surprise_duration_s;

    // Helpers for concrete states
    void minimize_clock() {
        if (s_clock) clock_widget_set_mode(s_clock, CLOCK_MODE_MINIMIZED);
    }
    void restore_clock() {
        if (s_clock) clock_widget_set_mode(s_clock, CLOCK_MODE_FULL);
    }

    void set_state_info(display_state_id_t id, const char *name);

    /** Animate an LVGL object from transparent to fully opaque (300ms ease-out).
     *  Caller must hold LVGL lock. */
    static void fade_in(lv_obj_t *obj);

    /** Animate an LVGL object from opaque to transparent (200ms ease-in).
     *  Does NOT destroy the object — use for visual effect before manual destroy.
     *  Caller must hold LVGL lock. */
    static void fade_out(lv_obj_t *obj);

public:
    // -----------------------------------------------------------------------
    // Default react() — concrete states override what they handle
    // -----------------------------------------------------------------------

    virtual void react(EvDisplayTimeout const &);
    virtual void react(EvWeatherUpdate const &);
    virtual void react(EvForecastUpdate const &);
    virtual void react(EvAlertReceived const &);
    virtual void react(EvRadarReady const &);
    virtual void react(EvAstroTrigger const &);
    virtual void react(EvPhotoTrigger const &);
    virtual void react(EvAmbientTrigger const &);

    // Base handles these — all states get same behavior
    virtual void react(EvGesture const &);
    virtual void react(EvSettingsBack const &);
    virtual void react(EvSurpriseMessage const &);
    virtual void react(EvClockUpdate const &);
    virtual void react(EvForceState const &);
    virtual void react(EvScheduleConfig const &);

    virtual void entry() { }
    virtual void exit()  { }

    // -----------------------------------------------------------------------
    // Public accessors (called from C API)
    // -----------------------------------------------------------------------
    static const char          *get_state_name() { return s_state_name; }
    static display_state_id_t   get_state_id()   { return s_state_id; }
    static clock_widget_t      *get_clock()      { return s_clock; }
    static lv_obj_t            *get_screen()     { return s_screen; }
    static void                 set_clock(clock_widget_t *c) { s_clock = c; }
    static void                 set_alert_banner(alert_banner_t *b) { s_alert_banner = b; }
    static void                 set_screen(lv_obj_t *s) { s_screen = s; }
};

#endif // DISPLAY_FSM_BASE_H
