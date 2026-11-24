/**
 * @file display_fsm_base.cpp
 * @version 1
 * @brief DisplayFsm base class: shared state, default event handlers, swipe carousel, dot indicator and fade helpers.
 */
// DisplayFsm base class — shared state management and default event handlers.

#include "display_fsm_base.h"
#include "display_states.h"
#include "display_widgets.h"
#include "display_scheduler.h"
#include "frame_capture.h"
#include "ui_theme.h"
#include "nws.h"
#include "esp_log.h"

static const char *TAG = "display_fsm";

// ---------------------------------------------------------------------------
// Static members — persist across state transitions
// ---------------------------------------------------------------------------

clock_widget_t      *DisplayFsm::s_clock         = nullptr;
alert_banner_t      *DisplayFsm::s_alert_banner  = nullptr;
lv_obj_t            *DisplayFsm::s_screen        = nullptr;
lv_obj_t            *DisplayFsm::s_top_layer     = nullptr;
display_state_id_t   DisplayFsm::s_state_id      = DISPLAY_STATE_CLOCK;
const char          *DisplayFsm::s_state_name     = "clock";
const char          *DisplayFsm::s_surprise_json       = nullptr;
uint32_t             DisplayFsm::s_surprise_duration_s  = 30;
lv_obj_t            *DisplayFsm::s_dot_container  = nullptr;

// ---------------------------------------------------------------------------
// Carousel order — states accessible via left/right swipe
// ---------------------------------------------------------------------------

static const display_state_id_t CAROUSEL_ORDER[] = {
    DISPLAY_STATE_CLOCK,
    DISPLAY_STATE_WEATHER,
    DISPLAY_STATE_RADAR,
    DISPLAY_STATE_ASTRONOMY,
    DISPLAY_STATE_PHOTOS,
    DISPLAY_STATE_AMBIENT,
};
static const int CAROUSEL_COUNT = sizeof(CAROUSEL_ORDER) / sizeof(CAROUSEL_ORDER[0]);

// ---------------------------------------------------------------------------
// Base class default event handlers
// ---------------------------------------------------------------------------

/**
 * @brief Default gesture handler: swipe up opens Settings, swipe left/right moves to the next/previous carousel state.
 * @version 1
 * @req REQ-DISP-003
 */
void DisplayFsm::react(EvGesture const &e)
{
    if (e.dir == LV_DIR_TOP) {
        ESP_LOGI(TAG, "Swipe UP → Settings");
        transit<Settings>();
    } else if (e.dir == LV_DIR_LEFT) {
        carousel_navigate(+1);
    } else if (e.dir == LV_DIR_RIGHT) {
        carousel_navigate(-1);
    }
}

/**
 * @brief Default settings-back handler: return to ClockFull.
 * @version 1
 * @req REQ-DISP-001
 * @req REQ-UI-001
 */
void DisplayFsm::react(EvSettingsBack const &)
{
    ESP_LOGI(TAG, "Settings back → ClockFull");
    transit<ClockFull>();
}

/**
 * @brief Stash the surprise JSON and duration (default 30 s) and transit to SurpriseMessage.
 * @version 1
 * @req REQ-SURP-001
 */
void DisplayFsm::react(EvSurpriseMessage const &e)
{
    // Stash event data before transit — entry() can't access event params
    s_surprise_json      = e.json_str;
    s_surprise_duration_s = e.duration_s > 0 ? e.duration_s : 30;
    ESP_LOGI(TAG, "Surprise message → SurpriseMessage state (duration=%lus)",
             (unsigned long)s_surprise_duration_s);
    transit<SurpriseMessage>();
}

/**
 * @brief Update the persistent clock widget with the event's time.
 * @version 1
 * @req REQ-CLOCK-001
 */
void DisplayFsm::react(EvClockUpdate const &e)
{
    if (s_clock) {
        clock_widget_update(s_clock, &e.time);
    }
}

// Default no-ops for events that only specific states handle
/**
 * @brief Default EvDisplayTimeout handler: ignored unless a concrete state overrides it.
 * @version 1
 * @req REQ-DISP-001
 */
void DisplayFsm::react(EvDisplayTimeout const &)  { }
/**
 * @brief Default EvWeatherUpdate handler: ignored unless a concrete state overrides it.
 * @version 1
 * @req REQ-DISP-001
 */
void DisplayFsm::react(EvWeatherUpdate const &)   { }
/**
 * @brief Default EvForecastUpdate handler: ignored unless a concrete state overrides it.
 * @version 1
 * @req REQ-DISP-001
 */
void DisplayFsm::react(EvForecastUpdate const &)  { }
/**
 * @brief Default EvRadarReady handler: ignored unless a concrete state overrides it.
 * @version 1
 * @req REQ-DISP-001
 */
void DisplayFsm::react(EvRadarReady const &)      { }

// Alert banner — handled in base class, composited on ALL states
/**
 * @brief Show the first active NWS alert in the banner on every state, or hide the banner when none are active.
 * @version 1
 * @req REQ-ALERT-001
 */
void DisplayFsm::react(EvAlertReceived const &)
{
    if (!s_alert_banner) return;

    const nws_alerts_t *alerts = nws_get_alerts();
    if (!alerts || !alerts->valid) return;

    if (alerts->alert_count > 0) {
        alert_banner_show(s_alert_banner, &alerts->alerts[0]);
        ESP_LOGI(TAG, "Alert banner shown: %d active alerts", alerts->alert_count);
    } else {
        alert_banner_hide(s_alert_banner);
        ESP_LOGI(TAG, "Alert banner hidden: no active alerts");
    }
}
/**
 * @brief Default EvAstroTrigger handler: ignored unless a concrete state overrides it.
 * @version 1
 * @req REQ-DISP-001
 */
void DisplayFsm::react(EvAstroTrigger const &)    { }
/**
 * @brief Default EvPhotoTrigger handler: ignored unless a concrete state overrides it.
 * @version 1
 * @req REQ-DISP-001
 */
void DisplayFsm::react(EvPhotoTrigger const &)    { }
/**
 * @brief Default EvAmbientTrigger handler: ignored unless a concrete state overrides it.
 * @version 1
 * @req REQ-DISP-001
 */
void DisplayFsm::react(EvAmbientTrigger const &)  { }
/**
 * @brief Forced state change (API): show the requested state now, bypassing its cooldown
 *        (force_show), or return to the clock. States that are already the target
 *        override this to stay put.
 * v2: implemented here for every state (was the clock's alone; the other states deferred to
 *     this no-op, so a forced change worked only from the clock).
 * @version 2
 * @req REQ-DISP-001
 * @req REQ-API-003
 */
void DisplayFsm::react(EvForceState const &e)
{
    if (e.state == s_state_id) return;   // already showing it
    if (e.state == DISPLAY_STATE_SETTINGS) {
        transit<Settings>();
        return;
    }
    if (e.state == DISPLAY_STATE_CLOCK) display_scheduler_get()->return_to_clock();
    else display_scheduler_get()->force_show(e.state);
    transit_to(e.state);
}
/**
 * @brief Default EvScheduleConfig handler: ignored unless a concrete state overrides it.
 * @version 1
 * @req REQ-DISP-001
 */
void DisplayFsm::react(EvScheduleConfig const &)  { }

// ---------------------------------------------------------------------------
// Carousel navigation
// ---------------------------------------------------------------------------

/**
 * @brief Find a state's position in the carousel order.
 * @version 1
 * @utility
 * @return Carousel index, or 0 (clock) if the state is not in the carousel.
 */
static int carousel_index_of(display_state_id_t id)
{
    for (int i = 0; i < CAROUSEL_COUNT; i++) {
        if (CAROUSEL_ORDER[i] == id) return i;
    }
    return 0;  // fallback to clock
}

/**
 * @brief Move @p offset steps through the carousel (wrapping), updating the scheduler's return timer; from a non-carousel state go to the clock.
 * v2: the state switch moved to transit_to (shared with the forced change).
 * @version 2
 * @req REQ-DISP-003
 * @req REQ-DISP-002
 */
void DisplayFsm::carousel_navigate(int offset)
{
    // If in a non-carousel state (Surprise), go to clock
    bool in_carousel = false;
    for (int i = 0; i < CAROUSEL_COUNT; i++) {
        if (CAROUSEL_ORDER[i] == s_state_id) { in_carousel = true; break; }
    }
    if (!in_carousel) {
        transit<ClockFull>();
        return;
    }

    int cur = carousel_index_of(s_state_id);
    int next = (cur + offset + CAROUSEL_COUNT) % CAROUSEL_COUNT;
    display_state_id_t target = CAROUSEL_ORDER[next];

    if (target == s_state_id) return;

    ESP_LOGI(TAG, "Carousel: swipe %s → state %d",
             offset > 0 ? "LEFT(next)" : "RIGHT(prev)", target);

    // Update scheduler: stop old return timer, start new one for target
    if (target == DISPLAY_STATE_CLOCK) {
        display_scheduler_get()->return_to_clock();
    } else {
        display_scheduler_get()->force_show(target);
    }

    transit_to(target);
}

/**
 * @brief Transit to the carousel state @p id (other ids are ignored).
 * @version 1
 * @req REQ-DISP-001
 */
void DisplayFsm::transit_to(display_state_id_t id)
{
    switch (id) {
        case DISPLAY_STATE_CLOCK:      transit<ClockFull>();        break;
        case DISPLAY_STATE_WEATHER:    transit<WeatherOverlay>();   break;
        case DISPLAY_STATE_RADAR:      transit<RadarOverlay>();     break;
        case DISPLAY_STATE_ASTRONOMY:  transit<Astronomy>();        break;
        case DISPLAY_STATE_PHOTOS:     transit<PhotoSlideshow>();   break;
        case DISPLAY_STATE_AMBIENT:    transit<AmbientDashboard>(); break;
        default: break;
    }
}

// ---------------------------------------------------------------------------
// Dot indicator — shows carousel position
// ---------------------------------------------------------------------------

#define DOT_SIZE_ACTIVE   12
#define DOT_SIZE_INACTIVE  6
#define DOT_SPACING       20
#define DOT_BOTTOM_MARGIN 12

/**
 * @brief Create the carousel dot indicator (one dot per carousel state) at the bottom of the top layer; once only.
 * v2: colours from the theme roles (ui_theme), no literals.
 * @version 2
 * @req REQ-DISP-003
 */
void DisplayFsm::create_dot_indicator()
{
    lv_obj_t *parent = s_top_layer ? s_top_layer : s_screen;
    if (!parent || s_dot_container) return;

    int total_w = (CAROUSEL_COUNT - 1) * DOT_SPACING + DOT_SIZE_ACTIVE;
    s_dot_container = lv_obj_create(parent);
    lv_obj_remove_style_all(s_dot_container);
    lv_obj_set_size(s_dot_container, total_w, DOT_SIZE_ACTIVE + 4);
    lv_obj_align(s_dot_container, LV_ALIGN_BOTTOM_MID, 0, -DOT_BOTTOM_MARGIN);
    lv_obj_clear_flag(s_dot_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_dot_container, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_style_pad_all(s_dot_container, 0, 0);

    // Create dot objects (children of container)
    for (int i = 0; i < CAROUSEL_COUNT; i++) {
        lv_obj_t *dot = lv_obj_create(s_dot_container);
        lv_obj_remove_style_all(dot);
        lv_obj_set_size(dot, DOT_SIZE_INACTIVE, DOT_SIZE_INACTIVE);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(dot, ui_theme_color(UI_ROLE_TEXT), 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_40, 0);
        lv_obj_clear_flag(dot, LV_OBJ_FLAG_SCROLLABLE);

        // Position centered vertically within container
        int x = i * DOT_SPACING;
        int cy = (DOT_SIZE_ACTIVE + 4) / 2;
        lv_obj_set_pos(dot, x, cy - DOT_SIZE_INACTIVE / 2);
    }

    // Move to front so it's always visible
    lv_obj_move_foreground(s_dot_container);
    update_dot_indicator();
}

/**
 * @brief Highlight the dot for the current carousel state, or hide the dots in non-carousel states.
 * @version 1
 * @req REQ-DISP-003
 */
void DisplayFsm::update_dot_indicator()
{
    if (!s_dot_container) return;

    int active_idx = carousel_index_of(s_state_id);
    bool in_carousel = false;
    for (int i = 0; i < CAROUSEL_COUNT; i++) {
        if (CAROUSEL_ORDER[i] == s_state_id) { in_carousel = true; break; }
    }

    // Hide dots when in non-carousel states (Settings, Surprise)
    if (!in_carousel) {
        lv_obj_add_flag(s_dot_container, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_obj_clear_flag(s_dot_container, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_dot_container);

    uint32_t child_count = lv_obj_get_child_count(s_dot_container);
    for (uint32_t i = 0; i < child_count && (int)i < CAROUSEL_COUNT; i++) {
        lv_obj_t *dot = lv_obj_get_child(s_dot_container, (int32_t)i);
        bool is_active = ((int)i == active_idx);
        int size = is_active ? DOT_SIZE_ACTIVE : DOT_SIZE_INACTIVE;

        lv_obj_set_size(dot, size, size);
        lv_obj_set_style_bg_opa(dot, is_active ? LV_OPA_COVER : LV_OPA_40, 0);

        int x = (int)i * DOT_SPACING + (DOT_SIZE_ACTIVE - size) / 2;
        int cy = (DOT_SIZE_ACTIVE + 4) / 2;
        lv_obj_set_pos(dot, x, cy - size / 2);
    }
}

// ---------------------------------------------------------------------------
// Helpers for concrete states
// ---------------------------------------------------------------------------

/**
 * @brief Record the current state id and name and refresh the dot indicator.
 * v2: starts an armed frame-capture recording (every state entry is a transition).
 * @version 2
 * @req REQ-DISP-001
 * @req REQ-API-003
 */
void DisplayFsm::set_state_info(display_state_id_t id, const char *name)
{
    frame_capture_on_transition();
    s_state_id   = id;
    s_state_name = name;
    update_dot_indicator();
}

// ---------------------------------------------------------------------------
// Fade animation helpers
// ---------------------------------------------------------------------------

/**
 * @brief lv_anim exec callback: set the object's opacity to @p v.
 * @version 1
 * @callback
 */
static void fade_anim_opa_cb(void *obj, int32_t v)
{
    lv_obj_set_style_opa((lv_obj_t *)obj, (lv_opa_t)v, 0);
}

/**
 * @brief Fade @p obj in from transparent alongside the clock morph (CLOCK_MORPH_MS,
 *        ease-in-out); caller holds the LVGL lock.
 * v2: delayed until the clock morph ended.
 * v3: runs with the morph again, same duration and easing, so the two stay in sync (delayed,
 *     the fade felt detached from the morph). A snapshot-image fade was measured slower
 *     (minimise 8.3-8.8 Hz vs ~10.5) and added ~70 ms to the transition stall, so the live
 *     object is faded.
 * @version 3
 * @req REQ-DISP-004
 */
void DisplayFsm::fade_in(lv_obj_t *obj)
{
    if (!obj) return;
    lv_obj_set_style_opa(obj, LV_OPA_TRANSP, 0);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, obj);
    lv_anim_set_values(&a, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_set_duration(&a, CLOCK_MORPH_MS);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_set_exec_cb(&a, fade_anim_opa_cb);
    lv_anim_start(&a);
}

/**
 * @brief Fade @p obj from opaque to transparent over 600 ms (ease-in-out) without deleting it; caller holds the LVGL lock.
 * @version 1
 * @req REQ-DISP-004
 */
void DisplayFsm::fade_out(lv_obj_t *obj)
{
    if (!obj) return;
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, obj);
    lv_anim_set_values(&a, LV_OPA_COVER, LV_OPA_TRANSP);
    lv_anim_set_duration(&a, 600);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_set_exec_cb(&a, fade_anim_opa_cb);
    lv_anim_start(&a);
}
