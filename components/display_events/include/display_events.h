// components/display_events/include/display_events.h
//
// C-compatible event types for the display FSM.
// All event payloads are plain structs — safe to pass through FreeRTOS queues.

#ifndef DISPLAY_EVENTS_H
#define DISPLAY_EVENTS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "lvgl.h"

// ---------------------------------------------------------------------------
// Display state identifiers
// ---------------------------------------------------------------------------

typedef enum {
    DISPLAY_STATE_CLOCK = 0,
    DISPLAY_STATE_WEATHER,
    DISPLAY_STATE_RADAR,
    DISPLAY_STATE_ASTRONOMY,
    DISPLAY_STATE_PHOTOS,
    DISPLAY_STATE_AMBIENT,
    DISPLAY_STATE_SURPRISE,
    DISPLAY_STATE_SETTINGS,
    DISPLAY_STATE_MAX
} display_state_id_t;

// ---------------------------------------------------------------------------
// Event types
// ---------------------------------------------------------------------------

typedef enum {
    DISPLAY_EVT_DISPLAY_TIMEOUT,     // return-to-clock timer fired
    DISPLAY_EVT_WEATHER_UPDATE,      // new conditions data
    DISPLAY_EVT_FORECAST_UPDATE,     // new forecast data
    DISPLAY_EVT_ALERT_RECEIVED,      // new/changed alerts
    DISPLAY_EVT_RADAR_READY,         // decoded radar PNG available
    DISPLAY_EVT_ASTRO_TRIGGER,       // dawn/dusk/midnight trigger
    DISPLAY_EVT_PHOTO_TRIGGER,       // periodic photo rotation
    DISPLAY_EVT_AMBIENT_TRIGGER,     // periodic ambient info
    DISPLAY_EVT_GESTURE,             // touch gesture detected
    DISPLAY_EVT_SETTINGS_BACK,       // back from settings → clock
    DISPLAY_EVT_SURPRISE_MESSAGE,    // HTTP push message
    DISPLAY_EVT_FORCE_STATE,         // web control override (bypasses debounce)
    DISPLAY_EVT_SCHEDULE_CONFIG,     // update display durations/cooldowns
    DISPLAY_EVT_CLOCK_UPDATE,        // time tick → update clock widget
    DISPLAY_EVT_REFRESH_BG,          // reload clock background from settings
} display_event_type_t;

// ---------------------------------------------------------------------------
// Event payloads
// ---------------------------------------------------------------------------

typedef struct {
    lv_dir_t dir;
} display_evt_gesture_t;

typedef struct {
    display_state_id_t state;
} display_evt_force_state_t;

typedef struct {
    const char *json_str;       // SPIRAM-allocated JSON string, FSM takes ownership
    uint32_t    duration_s;     // display duration (0 = use default 30s)
} display_evt_surprise_t;

typedef struct {
    display_state_id_t state;
    uint32_t display_duration_ms;
    uint32_t cooldown_ms;
    bool     enabled;
} display_evt_schedule_config_t;

typedef struct {
    int tm_hour;
    int tm_min;
    int tm_sec;
    int tm_mday;
    int tm_mon;     // 0-11
    int tm_year;    // years since 1900
    int tm_wday;    // 0-6 (Sunday = 0)
} display_evt_clock_t;

// ---------------------------------------------------------------------------
// Unified event struct (fits in FreeRTOS queue)
// ---------------------------------------------------------------------------

typedef struct {
    display_event_type_t type;
    union {
        display_evt_gesture_t         gesture;
        display_evt_force_state_t     force_state;
        display_evt_surprise_t        surprise;
        display_evt_schedule_config_t schedule;
        display_evt_clock_t           clock;
    };
} display_event_t;

#ifdef __cplusplus
}
#endif

#endif // DISPLAY_EVENTS_H
