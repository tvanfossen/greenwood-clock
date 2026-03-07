// components/display_fsm/src/display_scheduler.cpp
//
// Event-driven display scheduling with debounce and return-to-clock timers.

#include "display_scheduler.h"
#include "display_fsm.h"
#include "display_events.h"
#include "settings.h"
#include "nws.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"

static const char *TAG = "scheduler";

// ---------------------------------------------------------------------------
// Singleton instance
// ---------------------------------------------------------------------------

static DisplayScheduler s_instance;

DisplayScheduler *display_scheduler_get(void)
{
    return &s_instance;
}

// ---------------------------------------------------------------------------
// Return timer callback — fires after display_duration_ms
// ---------------------------------------------------------------------------

static void return_timer_cb(TimerHandle_t timer)
{
    (void)timer;
    display_event_t evt = {};
    evt.type = DISPLAY_EVT_DISPLAY_TIMEOUT;
    display_fsm_send_event(&evt);
}

static void photo_trigger_cb(TimerHandle_t timer)
{
    (void)timer;
    display_event_t evt = {};
    evt.type = DISPLAY_EVT_PHOTO_TRIGGER;
    display_fsm_send_event(&evt);
}

static void ambient_trigger_cb(TimerHandle_t timer)
{
    (void)timer;
    display_event_t evt = {};
    evt.type = DISPLAY_EVT_AMBIENT_TRIGGER;
    display_fsm_send_event(&evt);
}

// ---------------------------------------------------------------------------
// Implementation
// ---------------------------------------------------------------------------

void DisplayScheduler::init(void)
{
    // Load timing settings from NVS
    clock_settings_t cfg;
    settings_load(&cfg);

    // Build state configs from NVS settings
    struct {
        display_state_id_t id;
        uint32_t duration_ms;
        uint32_t cooldown_ms;
    } state_cfg[] = {
        { DISPLAY_STATE_WEATHER,   (uint32_t)cfg.weather_show_s * 1000,
                                   (uint32_t)cfg.weather_cooldown_s * 1000 },
        { DISPLAY_STATE_RADAR,     (uint32_t)cfg.radar_show_s * 1000,
                                   (uint32_t)cfg.radar_cooldown_s * 1000 },
        { DISPLAY_STATE_ASTRONOMY, (uint32_t)cfg.astro_show_s * 1000,
                                   (uint32_t)cfg.astro_cooldown_s * 1000 },
        { DISPLAY_STATE_PHOTOS,    (uint32_t)cfg.photos_show_s * 1000,
                                   (uint32_t)cfg.photos_interval_s * 1000 },
        { DISPLAY_STATE_AMBIENT,   (uint32_t)cfg.ambient_show_s * 1000,
                                   (uint32_t)cfg.ambient_interval_s * 1000 },
        { DISPLAY_STATE_SURPRISE,  30000, 0 },  // no cooldown
    };

    m_config_count = sizeof(state_cfg) / sizeof(state_cfg[0]);
    for (int i = 0; i < m_config_count; i++) {
        m_configs[i].state_id            = state_cfg[i].id;
        m_configs[i].display_duration_ms = state_cfg[i].duration_ms;
        m_configs[i].cooldown_ms         = state_cfg[i].cooldown_ms;
        m_configs[i].last_shown_tick     = 0;
        m_configs[i].enabled             = true;
        m_configs[i].condition_fn        = nullptr;
    }

    // Radar only shows when precipitation is active
    StateConfig *radar_cfg = find_config(DISPLAY_STATE_RADAR);
    if (radar_cfg) {
        radar_cfg->condition_fn = nws_radar_has_precipitation;
    }

    m_return_timer = xTimerCreate("fsm_ret", pdMS_TO_TICKS(30000),
                                   pdFALSE, nullptr, return_timer_cb);
    configASSERT(m_return_timer);

    // Periodic triggers — auto-repeating timers from NVS settings
    uint32_t photo_ms   = (uint32_t)cfg.photos_interval_s * 1000;
    uint32_t ambient_ms = (uint32_t)cfg.ambient_interval_s * 1000;

    m_photo_timer = xTimerCreate("fsm_photo", pdMS_TO_TICKS(photo_ms),
                                  pdTRUE, nullptr, photo_trigger_cb);
    configASSERT(m_photo_timer);
    xTimerStart(m_photo_timer, 0);

    m_ambient_timer = xTimerCreate("fsm_ambient", pdMS_TO_TICKS(ambient_ms),
                                    pdTRUE, nullptr, ambient_trigger_cb);
    configASSERT(m_ambient_timer);
    xTimerStart(m_ambient_timer, 0);

    m_active_state = DISPLAY_STATE_CLOCK;
    m_paused = false;

    ESP_LOGI(TAG, "DisplayScheduler initialized (%d configs, photos=%lus, ambient=%lus)",
             m_config_count, (unsigned long)cfg.photos_interval_s,
             (unsigned long)cfg.ambient_interval_s);
}

DisplayScheduler::StateConfig *DisplayScheduler::find_config(display_state_id_t state)
{
    for (int i = 0; i < m_config_count; i++) {
        if (m_configs[i].state_id == state) return &m_configs[i];
    }
    return nullptr;
}

bool DisplayScheduler::try_show(display_state_id_t state)
{
    StateConfig *cfg = find_config(state);
    if (!cfg || !cfg->enabled) return false;
    if (cfg->condition_fn && !cfg->condition_fn()) return false;

    TickType_t now = xTaskGetTickCount();
    if (cfg->cooldown_ms > 0 && cfg->last_shown_tick > 0) {
        TickType_t elapsed = now - cfg->last_shown_tick;
        if (elapsed < pdMS_TO_TICKS(cfg->cooldown_ms)) {
            ESP_LOGD(TAG, "Debounced state %d (cooldown not elapsed)", state);
            return false;
        }
    }

    cfg->last_shown_tick = now;
    start_return_timer(cfg->display_duration_ms);
    m_active_state = state;
    ESP_LOGI(TAG, "Showing state %d for %lu ms", state,
             (unsigned long)cfg->display_duration_ms);
    return true;
}

void DisplayScheduler::force_show(display_state_id_t state)
{
    StateConfig *cfg = find_config(state);
    uint32_t duration = cfg ? cfg->display_duration_ms : 30000;

    start_return_timer(duration);
    m_active_state = state;
    ESP_LOGI(TAG, "Force-showing state %d for %lu ms", state, (unsigned long)duration);
}

void DisplayScheduler::return_to_clock(void)
{
    xTimerStop(m_return_timer, 0);
    m_active_state = DISPLAY_STATE_CLOCK;
    ESP_LOGI(TAG, "Returning to clock");
}

void DisplayScheduler::pause(void)
{
    if (!m_paused) {
        xTimerStop(m_return_timer, 0);
        m_paused = true;
        ESP_LOGI(TAG, "Scheduler paused (settings)");
    }
}

void DisplayScheduler::resume(void)
{
    if (m_paused) {
        m_paused = false;
        ESP_LOGI(TAG, "Scheduler resumed");
    }
}

void DisplayScheduler::set_duration(display_state_id_t state, uint32_t ms)
{
    StateConfig *cfg = find_config(state);
    if (cfg) cfg->display_duration_ms = ms;
}

void DisplayScheduler::set_cooldown(display_state_id_t state, uint32_t ms)
{
    StateConfig *cfg = find_config(state);
    if (cfg) cfg->cooldown_ms = ms;
}

void DisplayScheduler::set_enabled(display_state_id_t state, bool enabled)
{
    StateConfig *cfg = find_config(state);
    if (cfg) cfg->enabled = enabled;
}

display_state_id_t DisplayScheduler::active(void) const
{
    return m_active_state;
}

void DisplayScheduler::start_return_timer(uint32_t duration_ms)
{
    xTimerStop(m_return_timer, 0);
    xTimerChangePeriod(m_return_timer, pdMS_TO_TICKS(duration_ms), 0);
    xTimerStart(m_return_timer, 0);
}
