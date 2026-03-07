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
 * @brief Get the FSM's active screen for background/widget operations.
 *
 * @return Pointer to the LVGL screen object.
 */
lv_obj_t *display_fsm_get_screen(void);

#ifdef __cplusplus
}
#endif

#endif // DISPLAY_FSM_H
