/**
 * @file display_fsm_base.h
 * @version 1
 * @brief DisplayFsm base class and TinyFSM event wrappers; all concrete display states inherit from it.
 */
// DisplayFsm base class declaration.
// All concrete states inherit from this.

#ifndef DISPLAY_FSM_BASE_H
#define DISPLAY_FSM_BASE_H

#include "tinyfsm.hpp"
#include "display_fsm.h"
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
struct EvPhotoTrigger    : tinyfsm::Event { };
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
    static lv_obj_t            *s_top_layer;   // lv_layer_top() — persistent UI here
    static display_state_id_t   s_state_id;
    static const char          *s_state_name;

    // Stashed surprise data — set before transit, read in SurpriseMessage::entry()
    static const char          *s_surprise_json;
    static uint32_t             s_surprise_duration_s;

    // Carousel dot indicator — persistent across all states
    static lv_obj_t            *s_dot_container;

    // Helpers for concrete states
    /**
     * @brief Switch the clock widget to minimized mode and hide the clock animation. The
     *        minimized colour is judged against @p behind (the state's own content under the
     *        clock, e.g. the radar map) or the background picture when NULL, before the morph
     *        so it recolours straight to it.
     * v2: samples what is behind the minimized clock (minimal OKLCH adjustment).
     * @version 2
     * @req REQ-CLOCK-002
     * @req REQ-ANIM-001
     * @req REQ-BG-002
     */
    void minimize_clock(const lv_image_dsc_t *behind = nullptr) {
        if (s_clock) {
            clock_widget_set_minimized_base_color(s_clock, display_fsm_min_clock_color(behind));
            clock_widget_set_mode(s_clock, CLOCK_MODE_MINIMIZED);
        }
        display_fsm_hide_clock_lottie();
    }
    /**
     * @brief Restore the clock widget to full mode and show the clock animation.
     * v2: starts the animation once the restore morph has finished. Started together, the
     *     snowfall's ~127 dirty areas per frame each re-drew the scaling morph snapshot.
     * @version 2
     * @req REQ-CLOCK-001
     * @req REQ-ANIM-001
     */
    void restore_clock() {
        if (!s_clock) {
            display_fsm_show_clock_lottie();
            return;
        }
        clock_widget_set_mode(s_clock, CLOCK_MODE_FULL);
        clock_widget_when_settled(s_clock, display_fsm_show_clock_lottie);
    }
    /**
     * @brief Switch the clock widget to topbar mode and hide the clock animation.
     * @version 1
     * @req REQ-CLOCK-002
     * @req REQ-ANIM-001
     */
    void topbar_clock() {
        if (s_clock) clock_widget_set_mode(s_clock, CLOCK_MODE_TOPBAR);
        display_fsm_hide_clock_lottie();
    }

    void set_state_info(display_state_id_t id, const char *name);

    /**
     * @brief Navigate the carousel by @p offset (-1 = prev, +1 = next); wraps around.
     * @version 1
     * @req REQ-DISP-001
     */
    void carousel_navigate(int offset);

    /**
     * @brief Transit to the carousel state @p id (other ids are ignored).
     * v2: no astronomy or ambient screens.
     * @version 2
     * @req REQ-DISP-001
     */
    void transit_to(display_state_id_t id);

    /**
     * @brief Animate @p obj from transparent to opaque (900 ms ease-in-out). Caller holds the
     *        LVGL lock.
     * @version 1
     * @req REQ-DISP-004
     */
    static void fade_in(lv_obj_t *obj);

    /**
     * @brief Animate @p obj from opaque to transparent (600 ms ease-in-out); it is not
     *        destroyed. Caller holds the LVGL lock.
     * @version 1
     * @req REQ-DISP-004
     */
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
    virtual void react(EvPhotoTrigger const &);

    // Base handles these — all states get same behavior
    virtual void react(EvGesture const &);
    virtual void react(EvSettingsBack const &);
    virtual void react(EvSurpriseMessage const &);
    virtual void react(EvClockUpdate const &);
    virtual void react(EvForceState const &);
    virtual void react(EvScheduleConfig const &);

    /**
     * @brief Default state entry hook (no-op).
     * @version 1
     * @req REQ-DISP-001
     */
    virtual void entry() { }
    /**
     * @brief Default state exit hook (no-op).
     * @version 1
     * @req REQ-DISP-001
     */
    virtual void exit()  { }

    // -----------------------------------------------------------------------
    // Public accessors (called from C API)
    // -----------------------------------------------------------------------
    /**
     * @brief Get the current state name.
     * @version 1
     * @req REQ-DISP-001
     * @return Current state name.
     */
    static const char          *get_state_name() { return s_state_name; }
    /**
     * @brief Get the current state id.
     * @version 1
     * @req REQ-DISP-001
     * @return Current display_state_id_t.
     */
    static display_state_id_t   get_state_id()   { return s_state_id; }
    /**
     * @brief Get the persistent clock widget.
     * @version 1
     * @req REQ-DISP-001
     * @return Clock widget, or nullptr before setup.
     */
    static clock_widget_t      *get_clock()      { return s_clock; }
    /**
     * @brief Get the shared screen object.
     * @version 1
     * @req REQ-DISP-001
     * @return Screen object, or nullptr before setup.
     */
    static lv_obj_t            *get_screen()     { return s_screen; }
    /**
     * @brief Set the persistent clock widget shared by all states.
     * @version 1
     * @req REQ-DISP-001
     */
    static void                 set_clock(clock_widget_t *c) { s_clock = c; }
    /**
     * @brief Set the persistent alert banner shared by all states.
     * @version 1
     * @req REQ-DISP-001
     * @req REQ-ALERT-001
     */
    static void                 set_alert_banner(alert_banner_t *b) { s_alert_banner = b; }
    /**
     * @brief Set the shared screen object.
     * @version 1
     * @req REQ-DISP-001
     */
    static void                 set_screen(lv_obj_t *s) { s_screen = s; }
    /**
     * @brief Set the top layer that hosts persistent UI.
     * @version 1
     * @req REQ-DISP-001
     */
    static void                 set_top_layer(lv_obj_t *t) { s_top_layer = t; }

    /**
     * @brief Create the carousel dot indicator on the screen. Call once at init.
     * @version 1
     * @req REQ-DISP-001
     */
    static void                 create_dot_indicator();
    /**
     * @brief Update the dot indicator to the current carousel position.
     * @version 1
     * @req REQ-DISP-001
     */
    static void                 update_dot_indicator();
};

#endif // DISPLAY_FSM_BASE_H
