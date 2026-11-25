/**
 * @file state_weather.cpp
 * v2: sky dial (sun, moon, sunrise / sunset) under the clock.
 * @version 2
 * @brief WeatherOverlay state — minimized clock + current conditions + forecast strip.
 *
 * Condition Lottie animation fills the weather card background.
 */

#include "display_states.h"
#include "display_scheduler.h"
#include "display_widgets.h"
#include "display_fsm.h"
#include "nws.h"
#include "settings.h"
#include "sky_dial.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <time.h>

static const char *TAG = "state_weather";

// Static member definitions
weather_card_t    *WeatherOverlay::s_card      = nullptr;
forecast_strip_t  *WeatherOverlay::s_strip     = nullptr;
lv_obj_t          *WeatherOverlay::s_dial      = nullptr;

#define DIAL_X 588   // under the minimised clock, beside the card, above the strip
#define DIAL_Y 250
#define DIAL_W 420
#define DIAL_H 166

// Deferred condition-Lottie load: its ThorVG parse (~700ms) holds the LVGL lock,
// so we submit it AFTER the entry fade settles to keep the transition smooth.
static lv_timer_t     *s_lottie_timer = nullptr;
static weather_card_t *s_lottie_card  = nullptr;
static bool            s_lottie_is_day = false;

/**
 * @brief One-shot LVGL timer: load the condition Lottie into the weather card
 *        if conditions are valid, then delete the timer.
 * @version 1
 * @callback
 * @req REQ-WX-005
 */
static void deferred_lottie_cb(lv_timer_t *t)
{
    s_lottie_timer = nullptr;
    const nws_conditions_t *cond = nws_get_conditions();
    if (s_lottie_card && cond && cond->valid) {
        weather_card_load_condition_lottie(s_lottie_card, cond->description,
                                           s_lottie_is_day);
    }
    lv_timer_delete(t);
}

/**
 * @brief The sky now at the configured place, with the cached Kp.
 * @version 1
 * @utility
 */
static void sky_now(sky_data_t *d)
{
    clock_settings_t cfg;
    settings_load(&cfg);
    sky_data_compute(time(NULL), cfg.latitude, cfg.longitude, nws_get_kp_index(), d);
}

/**
 * @brief Enter the weather state: minimize the clock, build and fill the weather
 *        card, forecast strip and sky dial, fade them in, and schedule the condition
 *        Lottie 1.1 s later, after the fade (day = between today's sunrise and sunset).
 * v2: clock uses the user's exact colour; no background sampling before minimizing.
 * v3: the Lottie delay counted from the end of the clock morph.
 * v4: back to a fixed 1.1 s: the fades run alongside the morph again.
 * v5: colours from the theme roles (ui_theme), no literals.
 * v6: no contrast calls: the card and strip colours are theme bindings.
 * v7: sky dial; the Lottie's day / night from the real sunrise and sunset (was 06-20 h).
 * @version 7
 * @req REQ-WX-002
 * @req REQ-WX-003
 * @req REQ-WX-005
 * @req REQ-DISP-001
 */
void WeatherOverlay::entry()
{
    // Per-phase timing — entry() runs under the LVGL lock, so every phase here
    // blocks rendering. Find what eats the transition budget.
    int64_t t0 = esp_timer_get_time();
    set_state_info(DISPLAY_STATE_WEATHER, "weather");
    minimize_clock();
    int64_t t_min = esp_timer_get_time();

    const nws_conditions_t *cond = nws_get_conditions();
    const nws_forecast_t   *fc   = nws_get_forecast();
    sky_data_t sky;
    sky_now(&sky);

    // Create weather card (left region)
    s_card = weather_card_create(s_screen);
    int64_t t_card_c = esp_timer_get_time();
    if (s_card && cond) {
        weather_card_update(s_card, cond);
        // Defer the condition Lottie until ~after the fade settles (see above).
        if (cond->valid) {
            s_lottie_is_day = sky.part == SKY_DAY;
            s_lottie_card   = s_card;
            s_lottie_timer  = lv_timer_create(deferred_lottie_cb, 1100, nullptr);
            lv_timer_set_repeat_count(s_lottie_timer, 1);
        }
    }

    int64_t t_card_u = esp_timer_get_time();

    // Create forecast strip (bottom)
    s_strip = forecast_strip_create(s_screen);
    int64_t t_strip_c = esp_timer_get_time();
    if (s_strip && fc) {
        forecast_strip_update(s_strip, fc);
    }
    int64_t t_strip_u = esp_timer_get_time();


    s_dial = sky_dial_create(s_screen, &sky, DIAL_X, DIAL_Y, DIAL_W, DIAL_H);

    // Fade in weather widgets
    if (s_card) fade_in(weather_card_container(s_card));
    if (s_strip) fade_in(forecast_strip_container(s_strip));
    fade_in(s_dial);
    int64_t t_end = esp_timer_get_time();

    ESP_LOGI(TAG, "WeatherOverlay: entry (conditions=%s)",
             cond && cond->valid ? "valid" : "none");
    ESP_LOGI(TAG, "[TIMING] entry us: minimize=%lld card_create=%lld card_upd=%lld "
                  "strip_create=%lld strip_upd=%lld fades=%lld TOTAL=%lld",
             t_min - t0, t_card_c - t_min, t_card_u - t_card_c,
             t_strip_c - t_card_u, t_strip_u - t_strip_c, t_end - t_strip_u,
             t_end - t0);
}

/**
 * @brief Leave the weather state: cancel a pending Lottie load, destroy the
 *        forecast strip, weather card and sky dial.
 * v2: the sky dial.
 * @version 2
 * @req REQ-DISP-001
 */
void WeatherOverlay::exit()
{
    // Cancel a still-pending deferred Lottie load before destroying the card.
    if (s_lottie_timer) { lv_timer_delete(s_lottie_timer); s_lottie_timer = nullptr; }
    s_lottie_card = nullptr;
    if (s_strip)     { forecast_strip_destroy(s_strip);      s_strip = NULL; }
    if (s_card)      { weather_card_destroy(s_card);         s_card = NULL; }
    if (s_dial)      { lv_anim_delete(s_dial, NULL); lv_obj_delete(s_dial); s_dial = NULL; }
    ESP_LOGI(TAG, "WeatherOverlay: exit");
}

/**
 * @brief Display timeout: return to the full clock.
 * @version 1
 * @req REQ-DISP-002
 */
void WeatherOverlay::react(EvDisplayTimeout const &)
{
    transit<ClockFull>();
}

/**
 * @brief New conditions fetched: refresh the weather card.
 * @version 1
 * @req REQ-WX-002
 */
void WeatherOverlay::react(EvWeatherUpdate const &)
{
    const nws_conditions_t *cond = nws_get_conditions();
    if (s_card && cond) {
        weather_card_update(s_card, cond);
    }
}

/**
 * @brief New forecast fetched: refresh the forecast strip.
 * @version 1
 * @req REQ-WX-003
 */
void WeatherOverlay::react(EvForecastUpdate const &)
{
    const nws_forecast_t *fc = nws_get_forecast();
    if (s_strip && fc) {
        forecast_strip_update(s_strip, fc);
    }
}

/**
 * @brief Forced state change: if weather is requested, refresh card and strip in
 *        place; otherwise defer to the base FSM handler.
 * @version 1
 * @req REQ-API-003
 * @req REQ-DISP-001
 */
void WeatherOverlay::react(EvForceState const &e)
{
    if (e.state == DISPLAY_STATE_WEATHER) {
        // Already here — just refresh data
        const nws_conditions_t *cond = nws_get_conditions();
        if (s_card && cond) weather_card_update(s_card, cond);
        const nws_forecast_t *fc = nws_get_forecast();
        if (s_strip && fc) forecast_strip_update(s_strip, fc);
    } else {
        DisplayFsm::react(e);
    }
}
