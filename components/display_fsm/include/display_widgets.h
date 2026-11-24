/**
 * @file display_widgets.h
 * v2: adds forecast_icons_rescan (the one off-lock widget call).
 * @version 2
 * @brief Layer 1: Reusable UI widget components (C API).
 *
 * Widgets never call lvgl_port_lock — caller (FSM task) holds the lock.
 */

#ifndef DISPLAY_WIDGETS_H
#define DISPLAY_WIDGETS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"
#include "ui_theme.h"   // ui_slot_t
#include <stdbool.h>
#include <time.h>

// Height of alert banner — used by clock widget and states to offset content
#define ALERT_BANNER_HEIGHT 50

// ---------------------------------------------------------------------------
// ClockWidget — time/date display with full/minimized modes
// ---------------------------------------------------------------------------

typedef enum {
    CLOCK_MODE_FULL,        // 256pt time, centered
    CLOCK_MODE_MINIMIZED,   // 128pt time, top-right corner
    CLOCK_MODE_TOPBAR,      // 48pt time, full-width thin bar at top
} clock_mode_t;

/** Clock morph duration (ms). Screen fade-ins run alongside it with the same timing. */
#define CLOCK_MORPH_MS 500

typedef struct clock_widget_t clock_widget_t;

/**
 * @brief Create the clock widget on the given parent, in the theme's text colour.
 * Caller must hold LVGL lock.
 * @version 1
 * @return The widget, or NULL if allocation failed.
 */
clock_widget_t *clock_widget_create(lv_obj_t *parent);

/**
 * @brief Destroy the clock widget and free resources.
 * Caller must hold LVGL lock.
 * @version 1
 */
void clock_widget_destroy(clock_widget_t *w);

/**
 * @brief Update displayed time from struct tm.
 * Caller must hold LVGL lock.
 * @version 1
 */
void clock_widget_update(clock_widget_t *w, const struct tm *ti);

/**
 * @brief Show @p t until clock_widget_release; live updates are kept but not shown meanwhile
 *        (the radar loop shows each frame's time on the clock). The time rolls to @p t,
 *        starting after @p delay_ms.
 * Caller must hold LVGL lock.
 * @version 1
 */
void clock_widget_hold(clock_widget_t *w, const struct tm *t, uint32_t delay_ms);

/**
 * @brief End a hold: roll back to the latest live time after @p delay_ms. No-op when nothing
 *        is held.
 * Caller must hold LVGL lock.
 * @version 1
 */
void clock_widget_release(clock_widget_t *w, uint32_t delay_ms);

/**
 * @brief Switch between full-screen and minimized mode.
 * Caller must hold LVGL lock.
 * @version 1
 */
void clock_widget_set_mode(clock_widget_t *w, clock_mode_t mode);

/**
 * @brief Run @p cb once the clock is at rest: immediately if no morph is running, else when
 *        the running morph finishes. A later mode change drops it.
 * Caller must hold LVGL lock.
 * @version 1
 */
void clock_widget_when_settled(clock_widget_t *w, void (*cb)(void));

/**
 * @brief Apply a text color to all clock labels.
 * Caller must hold LVGL lock.
 * @version 1
 */
void clock_widget_set_color(clock_widget_t *w, lv_color_t color);

/**
 * @brief Minimized-mode text colour (legible over the state screen behind it).
 * Set from a sample of the direct background; states override per their content.
 * @version 1
 */
void clock_widget_set_minimized_color(clock_widget_t *w, lv_color_t color);

/**
 * @brief Default minimized colour, applied on each minimize (states that show the clock
 *        bg behind the clock use this; radar/photos override after).
 * @version 1
 */
void clock_widget_set_minimized_base_color(clock_widget_t *w, lv_color_t color);

/**
 * @brief Set the clock's base ink: the colour every mode's legible colour is derived from.
 * @version 1
 */
void clock_widget_set_user_color(clock_widget_t *w, lv_color_t color);

/**
 * @brief The clock's base ink.
 * @version 1
 * @return The ink, or the theme's TEXT when @p w is NULL.
 */
lv_color_t clock_widget_user_color(const clock_widget_t *w);

/**
 * @brief Full-clock screen rectangle (where the digits sit), screen-space; used to sample
 *        the background underneath for contrast.
 * @version 1
 */
void clock_widget_full_area(lv_area_t *out);

/**
 * @brief Top-bar clock screen rectangle, screen-space.
 * @version 1
 */
void clock_widget_bar_area(lv_area_t *out);

/**
 * @brief Top-bar text colour (legible over the strip of picture behind the bar).
 * @version 1
 */
void clock_widget_set_bar_color(clock_widget_t *w, lv_color_t color);

/**
 * @brief Minimized-clock screen rectangle, screen-space.
 * @version 1
 */
void clock_widget_min_area(lv_area_t *out);

/**
 * @brief Get the current mode.
 * @version 1
 * @return The mode.
 */
clock_mode_t clock_widget_get_mode(const clock_widget_t *w);

/**
 * @brief Apply a small render-time translation to the clock container for
 * burn-in mitigation. Offsets are clamped to ±4 px; values outside that
 * range are saturated. The translation is purely visual (LVGL translate_x/y
 * style) and does not affect the container's logical position used by
 * mode/animation logic.
 * Caller must hold LVGL lock.
 * @version 1
 */
void clock_widget_set_jitter(clock_widget_t *w, int8_t dx, int8_t dy);

// ---------------------------------------------------------------------------
// WeatherCard — current conditions display
// ---------------------------------------------------------------------------

// Forward declaration — actual struct defined in nws.h
struct nws_conditions_t;

typedef struct weather_card_t weather_card_t;

weather_card_t *weather_card_create(lv_obj_t *parent);
void weather_card_destroy(weather_card_t *w);
void weather_card_update(weather_card_t *w, const struct nws_conditions_t *cond);

lv_obj_t *weather_card_container(const weather_card_t *w);

/**
 * @brief Load a Lottie condition animation into the weather card.
 * Matches condition_desc to a Lottie file in A:/lottie/weather/{day,night}/.
 * Spawns a background load task. No-op if Lottie not enabled or file missing.
 * Caller must hold LVGL lock.
 * @version 2
 */
void weather_card_load_condition_lottie(weather_card_t *w, const char *condition_desc,
                                         bool is_daytime);

// ---------------------------------------------------------------------------
// ForecastStrip — 7-day forecast bar
// ---------------------------------------------------------------------------

struct nws_forecast_t;

typedef struct forecast_strip_t forecast_strip_t;

forecast_strip_t *forecast_strip_create(lv_obj_t *parent);
void forecast_strip_destroy(forecast_strip_t *s);
void forecast_strip_update(forecast_strip_t *s, const struct nws_forecast_t *fc);

lv_obj_t *forecast_strip_container(const forecast_strip_t *s);

/**
 * @brief Re-resolve the forecast icon for every condition (SD stat calls). Must be called
 *        WITHOUT the LVGL lock, on the FSM task; forecast_strip_update reads the result.
 * @version 1
 * @req REQ-WX-006
 */
void forecast_icons_rescan(void);

// ---------------------------------------------------------------------------
// AlertBanner — colored banner with scrolling headline
// ---------------------------------------------------------------------------

struct nws_alert_t;

typedef struct alert_banner_t alert_banner_t;

/**
 * @brief The theme slot for NWS severity @p severity: ALERT_EXTREME / MODERATE / MINOR, and
 *        ALERT_SEVERE for Severe and anything unknown.
 * @version 4
 * @req REQ-ALERT-001
 * @req REQ-UI-003
 * @return The slot.
 */
ui_slot_t alert_severity_slot(const char *severity);

alert_banner_t *alert_banner_create(lv_obj_t *parent);
void alert_banner_destroy(alert_banner_t *b);
void alert_banner_show(alert_banner_t *b, const struct nws_alert_t *alert);
void alert_banner_hide(alert_banner_t *b);
bool alert_banner_is_visible(const alert_banner_t *b);

// ---------------------------------------------------------------------------
// RadarView — map background + radar overlay + home marker
// ---------------------------------------------------------------------------

typedef struct radar_view_t radar_view_t;

radar_view_t *radar_view_create(lv_obj_t *parent, float lat, float lon);
void radar_view_destroy(radar_view_t *rv);

// Preload the static map background (~2.4 MB SD read) into the module cache.
// MUST be called OUTSIDE the LVGL lock, once, at startup. Without it the first
// radar_view_create() does the SD read under the LVGL lock and stalls the
// transition into the radar screen.
void radar_view_preload_map(void);

// The cached radar basemap descriptor (XRGB8888, upright/not flipped), or NULL.
// For sampling the map behind the minimized clock.
const lv_image_dsc_t *radar_view_map_dsc(void);

// Two-phase radar overlay: predecode runs without LVGL lock (~500ms),
// apply_radar runs with LVGL lock (instant pointer swap).
void radar_view_predecode(const uint8_t *png_data, size_t len, time_t valid_ts);
void radar_view_apply_radar(radar_view_t *rv);
// Crossfade the overlay to one radar-loop frame (taken at ts) over fade_ms; `newest` is the
// most recent frame; caller holds the LVGL lock.
void radar_view_crossfade_frame(radar_view_t *rv, const lv_image_dsc_t *dsc, time_t ts,
                                uint32_t fade_ms, bool newest);

lv_obj_t *radar_view_container(const radar_view_t *rv);

// ---------------------------------------------------------------------------
// ImageRotator — cycles through images from a directory
// ---------------------------------------------------------------------------

typedef struct image_rotator_t image_rotator_t;

// start_index: first image to show (clamped into range); lets the slideshow
// resume where a previous visit left off instead of always restarting at 0.
image_rotator_t *image_rotator_create(lv_obj_t *parent, const char *dir_path,
                                      int start_index);
void image_rotator_destroy(image_rotator_t *r);

// Two-phase advance for zero-lag transitions. The 1024x600 PNG decode is the
// expensive part and MUST run off the LVGL lock (otherwise it stalls rendering
// when LVGL lazily decodes on first draw). Call decode_current() off-lock, then
// present() under the LVGL lock (a fast descriptor swap + opaque RGB565 blit).
void image_rotator_step(image_rotator_t *r);            // advance index (no I/O)
void image_rotator_decode_current(image_rotator_t *r);  // OFF-lock: decode to pending
void image_rotator_prefetch_next(image_rotator_t *r);   // OFF-lock: decode next ahead
void image_rotator_present(image_rotator_t *r);         // UNDER lock: swap to pending

int  image_rotator_count(const image_rotator_t *r);
int  image_rotator_index(const image_rotator_t *r);

// Descriptor of the photo currently displayed (RGB565, vertically flipped like
// the clock background). NULL if none. For sampling the bg behind the clock.
const lv_image_dsc_t *image_rotator_shown_dsc(const image_rotator_t *r);

// ---------------------------------------------------------------------------
// JsonLayout — creates LVGL widgets from JSON DSL
// ---------------------------------------------------------------------------

typedef struct json_layout_t json_layout_t;

json_layout_t *json_layout_create(lv_obj_t *parent, const char *json_str);
void json_layout_destroy(json_layout_t *jl);

// ---------------------------------------------------------------------------
// AstroCalc — pure computation, no UI
// ---------------------------------------------------------------------------

typedef struct {
    float illumination_pct;        // 0.0 - 100.0
    char  phase_name[20];          // "Waxing Crescent", "Full Moon", etc.
    int   phase_index;             // 0-7
    int   days_to_full;
    int   days_to_new;
} moon_phase_t;

typedef struct {
    int   sunrise_hour, sunrise_min;
    int   sunset_hour, sunset_min;
    int   daylight_minutes;
    float solar_noon_elevation;    // degrees
} sun_times_t;

void  astro_calc_moon_phase(int year, int month, int day, moon_phase_t *out);
void  astro_calc_sun_times(int year, int month, int day, float lat, float lon, sun_times_t *out);
float astro_calc_aurora_probability(float kp_index, float latitude);

#ifdef __cplusplus
}
#endif

#endif // DISPLAY_WIDGETS_H
