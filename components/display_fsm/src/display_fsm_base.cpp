// components/display_fsm/src/display_fsm_base.cpp
//
// DisplayFsm base class — shared state management and default event handlers.

#include "display_fsm_base.h"
#include "display_states.h"
#include "display_widgets.h"
#include "nws.h"
#include "esp_log.h"

static const char *TAG = "display_fsm";

// ---------------------------------------------------------------------------
// Static members — persist across state transitions
// ---------------------------------------------------------------------------

clock_widget_t      *DisplayFsm::s_clock         = nullptr;
alert_banner_t      *DisplayFsm::s_alert_banner  = nullptr;
lv_obj_t            *DisplayFsm::s_screen        = nullptr;
display_state_id_t   DisplayFsm::s_state_id      = DISPLAY_STATE_CLOCK;
const char          *DisplayFsm::s_state_name     = "clock";
const char          *DisplayFsm::s_surprise_json       = nullptr;
uint32_t             DisplayFsm::s_surprise_duration_s  = 30;

// ---------------------------------------------------------------------------
// Base class default event handlers
// ---------------------------------------------------------------------------

void DisplayFsm::react(EvGesture const &e)
{
    if (e.dir == LV_DIR_TOP) {
        ESP_LOGI(TAG, "Swipe UP → Settings");
        transit<Settings>();
    }
}

void DisplayFsm::react(EvSettingsBack const &)
{
    ESP_LOGI(TAG, "Settings back → ClockFull");
    transit<ClockFull>();
}

void DisplayFsm::react(EvSurpriseMessage const &e)
{
    // Stash event data before transit — entry() can't access event params
    s_surprise_json      = e.json_str;
    s_surprise_duration_s = e.duration_s > 0 ? e.duration_s : 30;
    ESP_LOGI(TAG, "Surprise message → SurpriseMessage state (duration=%lus)",
             (unsigned long)s_surprise_duration_s);
    transit<SurpriseMessage>();
}

void DisplayFsm::react(EvClockUpdate const &e)
{
    if (s_clock) {
        clock_widget_update(s_clock, &e.time);
    }
}

// Default no-ops for events that only specific states handle
void DisplayFsm::react(EvDisplayTimeout const &)  { }
void DisplayFsm::react(EvWeatherUpdate const &)   { }
void DisplayFsm::react(EvForecastUpdate const &)  { }
void DisplayFsm::react(EvRadarReady const &)      { }

// Alert banner — handled in base class, composited on ALL states
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
void DisplayFsm::react(EvAstroTrigger const &)    { }
void DisplayFsm::react(EvPhotoTrigger const &)    { }
void DisplayFsm::react(EvAmbientTrigger const &)  { }
void DisplayFsm::react(EvForceState const &)      { }
void DisplayFsm::react(EvScheduleConfig const &)  { }

// ---------------------------------------------------------------------------
// Helpers for concrete states
// ---------------------------------------------------------------------------

void DisplayFsm::set_state_info(display_state_id_t id, const char *name)
{
    s_state_id   = id;
    s_state_name = name;
}

// ---------------------------------------------------------------------------
// Fade animation helpers
// ---------------------------------------------------------------------------

static void fade_anim_opa_cb(void *obj, int32_t v)
{
    lv_obj_set_style_opa((lv_obj_t *)obj, (lv_opa_t)v, 0);
}

void DisplayFsm::fade_in(lv_obj_t *obj)
{
    if (!obj) return;
    lv_obj_set_style_opa(obj, LV_OPA_TRANSP, 0);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, obj);
    lv_anim_set_values(&a, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_set_duration(&a, 300);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&a, fade_anim_opa_cb);
    lv_anim_start(&a);
}

void DisplayFsm::fade_out(lv_obj_t *obj)
{
    if (!obj) return;
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, obj);
    lv_anim_set_values(&a, LV_OPA_COVER, LV_OPA_TRANSP);
    lv_anim_set_duration(&a, 200);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in);
    lv_anim_set_exec_cb(&a, fade_anim_opa_cb);
    lv_anim_start(&a);
}
