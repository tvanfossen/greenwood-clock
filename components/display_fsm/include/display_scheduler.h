// components/display_fsm/include/display_scheduler.h
//
// Event-driven display scheduling with debounce and return-to-clock timers.

#ifndef DISPLAY_SCHEDULER_H
#define DISPLAY_SCHEDULER_H

#include "display_events.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include <stdbool.h>

#define SCHEDULER_MAX_STATES 8

class DisplayScheduler {
public:
    struct StateConfig {
        display_state_id_t state_id;
        uint32_t display_duration_ms;
        uint32_t cooldown_ms;
        TickType_t last_shown_tick;
        bool enabled;
        bool (*condition_fn)();
    };

    void init(void);

    /** @brief Try to show a state (respects debounce). Returns false if debounced. */
    bool try_show(display_state_id_t state);

    /** @brief Force-show a state (bypasses debounce, for web control). */
    void force_show(display_state_id_t state);

    /** @brief Called when return timer fires — stops timer, resets active state. */
    void return_to_clock(void);

    /** @brief Pause return timer (settings entered). */
    void pause(void);

    /** @brief Resume return timer (settings exited). */
    void resume(void);

    void set_duration(display_state_id_t state, uint32_t ms);
    void set_cooldown(display_state_id_t state, uint32_t ms);
    void set_enabled(display_state_id_t state, bool enabled);

    display_state_id_t active(void) const;

private:
    StateConfig find_config_result;
    StateConfig m_configs[SCHEDULER_MAX_STATES];
    int m_config_count = 0;
    TimerHandle_t m_return_timer = nullptr;
    TimerHandle_t m_photo_timer  = nullptr;
    TimerHandle_t m_ambient_timer = nullptr;
    display_state_id_t m_active_state = DISPLAY_STATE_CLOCK;
    bool m_paused = false;

    StateConfig *find_config(display_state_id_t state);
    void start_return_timer(uint32_t duration_ms);
};

/** @brief Get the singleton scheduler instance. */
DisplayScheduler *display_scheduler_get(void);

#endif // DISPLAY_SCHEDULER_H
