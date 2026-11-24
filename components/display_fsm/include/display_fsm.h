/**
 * @file display_fsm.h
 * @version 1
 * @brief Public C API for the display finite state machine, callable from any C or C++ component.
 */
// Public C API for the display finite state machine.
// Callable from any C or C++ component.

#ifndef DISPLAY_FSM_H
#define DISPLAY_FSM_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "display_events.h"
#include "lvgl.h"

/**
 * @brief Initialize the display FSM.
 *
 * Creates the event queue and FSM task. Must be called after
 * bsp_display_start_with_config() and LVGL FS driver init.
 * The FSM enters ClockFull state on startup.
 * @version 1
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
 * @version 1
 */
bool display_fsm_send_event(const display_event_t *evt);

/**
 * @brief Get the name of the current display state.
 *
 * @return Static string, e.g. "clock", "weather", "settings".
 * @version 1
 */
const char *display_fsm_get_state_name(void);

/**
 * @brief Get the current display state ID.
 *
 * @return Current state enum value.
 * @version 1
 */
display_state_id_t display_fsm_get_state_id(void);

/**
 * @brief Get the FSM's clock widget for external updates (color, etc.).
 *
 * @return Pointer to the ClockWidget, or NULL if not yet created.
 * @version 1
 */
struct clock_widget_t *display_fsm_get_clock(void);

/**
 * @brief The minimized clock's colour over @p behind (a screen's own content under the
 *        minimized clock: radar map, photo), or over the background picture when NULL: the
 *        user's colour, its OKLCH lightness minimally shifted when it would not be legible.
 *        Caller holds the LVGL lock.
 * @version 1
 * @req REQ-BG-002
 * @return The colour.
 */
lv_color_t display_fsm_min_clock_color(const lv_image_dsc_t *behind);

/**
 * @brief The colour text on a translucent @p panel actually sits on: its bg colour at its bg
 *        opacity over the background picture under it. Caller holds the LVGL lock.
 * @version 1
 * @req REQ-BG-002
 * @return The composite backdrop colour.
 */
lv_color_t display_fsm_panel_backdrop(lv_obj_t *panel);

/**
 * @brief Show or hide the clock background image, for screens whose own content covers the
 *        whole display (LVGL draws covered objects too). Caller holds the LVGL lock.
 * @version 1
 * @req REQ-DISP-004
 */
void display_fsm_set_bg_visible(bool visible);

/**
 * @brief Get the FSM's active screen for background/widget operations.
 *
 * @return Pointer to the LVGL screen object.
 * @version 1
 */
lv_obj_t *display_fsm_get_screen(void);

/**
 * @brief Reload the background image from current settings.
 *
 * Deletes the old background (if any), loads the new one from NVS path.
 * Safe to call from LVGL callbacks (handles its own locking).
 * @version 1
 */
void display_fsm_refresh_background(void);

/**
 * @brief Re-apply the theme (global preference or active profile's) and re-list the theme
 *        files, in fsm_task. Safe to call from LVGL callbacks and HTTP handlers.
 * @version 1
 * @req REQ-UI-003
 */
void display_fsm_refresh_theme(void);

/**
 * @brief Apply a backlight brightness (0-100 %) live, once the display is up.
 *
 * No-op before display init (the backlight PWM is not configured during the boot
 * launch window). No LVGL lock needed.
 * @version 1
 */
void display_fsm_refresh_brightness(uint8_t percent);

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
 * @version 1
 */
void display_fsm_show_clock_lottie(void);
void display_fsm_hide_clock_lottie(void);

#ifdef __cplusplus
}
#endif

#endif // DISPLAY_FSM_H
