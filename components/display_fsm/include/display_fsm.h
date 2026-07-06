// components/display_fsm/include/display_fsm.h
//
// Public C API for the display finite state machine.
// Callable from any C or C++ component.

#ifndef DISPLAY_FSM_H
#define DISPLAY_FSM_H

#ifdef __cplusplus
extern "C" {
#endif

#include "display_events.h"

/**
 * @brief Initialize the display FSM.
 *
 * Creates the event queue and FSM task. Must be called after
 * bsp_display_start_with_config() and LVGL FS driver init.
 * The FSM enters ClockFull state on startup.
 */
void display_fsm_init(void);

/**
 * @brief Send an event to the display FSM.
 *
 * Thread-safe — can be called from any FreeRTOS task or ISR context
 * (uses xQueueSend with 100ms timeout from task context).
 *
 * @param evt Pointer to event struct (copied into queue).
 * @return true if event was queued, false if queue full.
 */
bool display_fsm_send_event(const display_event_t *evt);

/**
 * @brief Get the name of the current display state.
 *
 * @return Static string, e.g. "clock", "weather", "settings".
 */
const char *display_fsm_get_state_name(void);

/**
 * @brief Get the current display state ID.
 *
 * @return Current state enum value.
 */
display_state_id_t display_fsm_get_state_id(void);

/**
 * @brief Get the FSM's clock widget for external updates (color, etc.).
 *
 * @return Pointer to the ClockWidget, or NULL if not yet created.
 */
struct clock_widget_t *display_fsm_get_clock(void);

/**
 * @brief Re-derive the minimized clock colour from the bg image directly behind
 * the minimized clock position (strong OKLCH contrast). Call from a state's
 * entry() BEFORE minimize_clock() so the morph recolours to the correct,
 * placement-aware colour. Caller must hold the LVGL lock (state entry does).
 */
void display_fsm_apply_min_bg_contrast(void);

/**
 * @brief Get the FSM's active screen for background/widget operations.
 *
 * @return Pointer to the LVGL screen object.
 */
lv_obj_t *display_fsm_get_screen(void);

/**
 * @brief Reload the background image from current settings.
 *
 * Deletes the old background (if any), loads the new one from NVS path.
 * Safe to call from LVGL callbacks (handles its own locking).
 */
void display_fsm_refresh_background(void);

/**
 * @brief Reload the text color from current settings.
 *
 * Updates the ClockWidget's text color from NVS.
 * Safe to call from LVGL callbacks (handles its own locking).
 */
void display_fsm_refresh_text_color(void);

/**
 * @brief Submit a Lottie file for async loading on the shared loader task.
 *
 * The loader task has a 64KB SPIRAM stack for ThorVG's deep recursive
 * JSON parse.  The widget must already be created and have its buffer set.
 * Loading happens asynchronously — the widget will start animating once
 * the parse completes.
 *
 * @param job  Load job descriptor (copied into queue).
 * @return true if queued, false if queue full or Lottie not enabled.
 */
typedef struct {
    lv_obj_t *widget;       // Lottie widget (already created, buffer set)
    char      path[256];    // POSIX path to .json file
    uint32_t  target_fps;   // 0 = use default animation speed
} lottie_load_job_t;

bool display_fsm_load_lottie(const lottie_load_job_t *job);

/**
 * @brief Show/hide the clock Lottie animation (hummingbird).
 *
 * The Lottie lives on the root screen and must be explicitly hidden
 * when non-ClockFull states are active, otherwise it bleeds through.
 * Caller must hold LVGL lock.
 */
void display_fsm_show_clock_lottie(void);
void display_fsm_hide_clock_lottie(void);

#ifdef __cplusplus
}
#endif

#endif // DISPLAY_FSM_H
