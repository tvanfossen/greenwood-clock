/**
 * @file display_scheduler.cpp
 * @version 1
 * @brief Event-driven display scheduler: per-state durations and cooldowns from NVS, return-to-clock and periodic trigger timers.
 */
// Event-driven display scheduling with debounce and return-to-clock timers.

#include "display_scheduler.h"
#include "display_fsm.h"
#include "display_events.h"
#include "settings.h"
#include "nws.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include <dirent.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

static const char *TAG = "scheduler";

// ---------------------------------------------------------------------------
// Singleton instance
// ---------------------------------------------------------------------------

static DisplayScheduler s_instance;

/**
 * @brief Get the process-wide scheduler singleton.
 * @version 1
 * @req REQ-DISP-002
 * @return Pointer to the singleton DisplayScheduler.
 */
DisplayScheduler *display_scheduler_get(void)
{
    return &s_instance;
}

// ---------------------------------------------------------------------------
// Return timer callback — fires after display_duration_ms
// ---------------------------------------------------------------------------

/**
 * @brief FreeRTOS timer callback: send DISPLAY_EVT_DISPLAY_TIMEOUT so the FSM returns to the clock.
 * @version 1
 * @callback
 */
static void return_timer_cb(TimerHandle_t timer)
{
    (void)timer;
    display_event_t evt = {};
    evt.type = DISPLAY_EVT_DISPLAY_TIMEOUT;
    display_fsm_send_event(&evt);
}

/**
 * @brief FreeRTOS timer callback: send DISPLAY_EVT_PHOTO_TRIGGER.
 * @version 1
 * @callback
 */
static void photo_trigger_cb(TimerHandle_t timer)
{
    (void)timer;
    display_event_t evt = {};
    evt.type = DISPLAY_EVT_PHOTO_TRIGGER;
    display_fsm_send_event(&evt);
}

/**
 * @brief FreeRTOS timer callback: send DISPLAY_EVT_AMBIENT_TRIGGER.
 * @version 1
 * @callback
 */
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

// True if /sdcard/photos contains at least one .png file.
/**
 * @brief Check whether /sdcard/photos holds at least one regular .png file.
 * @version 1
 * @req REQ-PHOTO-001
 * @return true if a PNG was found, false otherwise or if the directory cannot be opened.
 */
static bool photos_dir_has_png(void)
{
    DIR *d = opendir("/sdcard/photos");
    if (!d) return false;

    bool found = false;
    struct dirent *ent;
    while (!found && (ent = readdir(d)) != NULL) {
        if (ent->d_type != DT_REG) continue;
        size_t len = strlen(ent->d_name);
        if (len >= 4 && strcasecmp(ent->d_name + len - 4, ".png") == 0) {
            found = true;
        }
    }
    closedir(d);
    return found;
}

/**
 * @brief Build per-state durations and cooldowns from NVS, gate radar on precipitation, create the return timer and start the photo (only if PNGs exist) and ambient periodic timers.
 * @version 1
 * @req REQ-DISP-002
 * @req REQ-RADAR-002
 * @req REQ-PHOTO-001
 * @req REQ-AMB-001
 */
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

    // Check if photos directory has any PNG files — disable state if empty
    bool photos_available = photos_dir_has_png();

    m_photo_timer = xTimerCreate("fsm_photo", pdMS_TO_TICKS(photo_ms),
                                  pdTRUE, nullptr, photo_trigger_cb);
    configASSERT(m_photo_timer);
    if (photos_available) {
        xTimerStart(m_photo_timer, 0);
    } else {
        StateConfig *photo_cfg = find_config(DISPLAY_STATE_PHOTOS);
        if (photo_cfg) photo_cfg->enabled = false;
        ESP_LOGI(TAG, "Photos disabled — no PNG files in /sdcard/photos");
    }

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

/**
 * @brief Find the config entry for @p state.
 * @version 1
 * @utility
 * @return Matching StateConfig, or nullptr.
 */
DisplayScheduler::StateConfig *DisplayScheduler::find_config(display_state_id_t state)
{
    for (int i = 0; i < m_config_count; i++) {
        if (m_configs[i].state_id == state) return &m_configs[i];
    }
    return nullptr;
}

/**
 * @brief Show @p state if it is enabled, its condition holds and its cooldown has elapsed; starts its return timer.
 * @version 1
 * @req REQ-DISP-002
 * @return true if the state may be shown, false if disabled, condition false or debounced.
 */
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

/**
 * @brief Show @p state regardless of cooldown, starting the return timer for its duration (30 s if unconfigured).
 * @version 1
 * @req REQ-DISP-002
 */
void DisplayScheduler::force_show(display_state_id_t state)
{
    StateConfig *cfg = find_config(state);
    uint32_t duration = cfg ? cfg->display_duration_ms : 30000;

    start_return_timer(duration);
    m_active_state = state;
    ESP_LOGI(TAG, "Force-showing state %d for %lu ms", state, (unsigned long)duration);
}

/**
 * @brief Stop the return timer and mark the clock as the active state.
 * @version 1
 * @req REQ-DISP-002
 */
void DisplayScheduler::return_to_clock(void)
{
    xTimerStop(m_return_timer, 0);
    m_active_state = DISPLAY_STATE_CLOCK;
    ESP_LOGI(TAG, "Returning to clock");
}

/**
 * @brief Pause scheduling (settings open): stop the return timer.
 * @version 1
 * @req REQ-DISP-002
 */
void DisplayScheduler::pause(void)
{
    if (!m_paused) {
        xTimerStop(m_return_timer, 0);
        m_paused = true;
        ESP_LOGI(TAG, "Scheduler paused (settings)");
    }
}

/**
 * @brief Resume scheduling after a pause.
 * @version 1
 * @req REQ-DISP-002
 */
void DisplayScheduler::resume(void)
{
    if (m_paused) {
        m_paused = false;
        ESP_LOGI(TAG, "Scheduler resumed");
    }
}

/**
 * @brief Set the display duration for @p state.
 * @version 1
 * @req REQ-DISP-002
 * @req REQ-API-003
 */
void DisplayScheduler::set_duration(display_state_id_t state, uint32_t ms)
{
    StateConfig *cfg = find_config(state);
    if (cfg) cfg->display_duration_ms = ms;
}

/**
 * @brief Set the cooldown for @p state.
 * @version 1
 * @req REQ-DISP-002
 * @req REQ-API-003
 */
void DisplayScheduler::set_cooldown(display_state_id_t state, uint32_t ms)
{
    StateConfig *cfg = find_config(state);
    if (cfg) cfg->cooldown_ms = ms;
}

/**
 * @brief Enable or disable @p state.
 * @version 1
 * @req REQ-DISP-002
 * @req REQ-API-003
 */
void DisplayScheduler::set_enabled(display_state_id_t state, bool enabled)
{
    StateConfig *cfg = find_config(state);
    if (cfg) cfg->enabled = enabled;
}

/**
 * @brief The album changed: enable the photo state and start its timer when /sdcard/photos
 *        holds a PNG, disable it when it holds none. SD access: called off the LVGL lock.
 * @version 1
 * @req REQ-PHOTO-001
 */
void DisplayScheduler::photos_changed(void)
{
    bool has = photos_dir_has_png();
    set_enabled(DISPLAY_STATE_PHOTOS, has);
    if (has && xTimerIsTimerActive(m_photo_timer) == pdFALSE) xTimerStart(m_photo_timer, 0);
    if (!has) xTimerStop(m_photo_timer, 0);
    ESP_LOGI(TAG, "Photos rescanned: %s", has ? "enabled" : "disabled (no PNG files)");
}

/**
 * @brief Get the state the scheduler considers active.
 * @version 1
 * @req REQ-DISP-002
 * @return Active display_state_id_t.
 */
display_state_id_t DisplayScheduler::active(void) const
{
    return m_active_state;
}

/**
 * @brief Restart the one-shot return timer with period @p duration_ms.
 * @version 1
 * @req REQ-DISP-002
 */
void DisplayScheduler::start_return_timer(uint32_t duration_ms)
{
    xTimerStop(m_return_timer, 0);
    xTimerChangePeriod(m_return_timer, pdMS_TO_TICKS(duration_ms), 0);
    xTimerStart(m_return_timer, 0);
}
