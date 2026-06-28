// components/display_fsm/src/display_fsm_base.cpp
//
// DisplayFsm base class — shared state management and default event handlers.

#include "display_fsm_base.h"
#include "display_states.h"
#include "display_widgets.h"
#include "display_scheduler.h"
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
// Carousel navigation
// ---------------------------------------------------------------------------

static int carousel_index_of(display_state_id_t id)
{
    for (int i = 0; i < CAROUSEL_COUNT; i++) {
        if (CAROUSEL_ORDER[i] == id) return i;
    }
    return 0;  // fallback to clock
}

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

    switch (target) {
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
        lv_obj_set_style_bg_color(dot, lv_color_white(), 0);
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

void DisplayFsm::set_state_info(display_state_id_t id, const char *name)
{
    s_state_id   = id;
    s_state_name = name;
    update_dot_indicator();
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
    lv_anim_set_duration(&a, 900);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
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
    lv_anim_set_duration(&a, 600);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_set_exec_cb(&a, fade_anim_opa_cb);
    lv_anim_start(&a);
}
