// components/display_fsm/src/state_weather.cpp
//
// WeatherOverlay state — minimized clock + current conditions + forecast strip.
// Condition Lottie animation fills the weather card background.

#include "display_states.h"
#include "display_scheduler.h"
#include "display_widgets.h"
#include "nws.h"
#include "esp_log.h"
#include <time.h>

static const char *TAG = "state_weather";

// Static member definitions
weather_card_t    *WeatherOverlay::s_card      = nullptr;
forecast_strip_t  *WeatherOverlay::s_strip     = nullptr;

// Deferred condition-Lottie load: its ThorVG parse (~700ms) holds the LVGL lock,
// so we submit it AFTER the entry fade settles to keep the transition smooth.
static lv_timer_t     *s_lottie_timer = nullptr;
static weather_card_t *s_lottie_card  = nullptr;
static bool            s_lottie_is_day = false;

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

void WeatherOverlay::entry()
{
    set_state_info(DISPLAY_STATE_WEATHER, "weather");
    minimize_clock();

    const nws_conditions_t *cond = nws_get_conditions();
    const nws_forecast_t   *fc   = nws_get_forecast();

    // Create weather card (left region)
    s_card = weather_card_create(s_screen);
    if (s_card && cond) {
        weather_card_update(s_card, cond);
        // Defer the condition Lottie until ~after the fade settles (see above).
        if (cond->valid) {
            time_t now;
            time(&now);
            struct tm ti;
            localtime_r(&now, &ti);
            s_lottie_is_day = (ti.tm_hour >= 6 && ti.tm_hour < 20);
            s_lottie_card   = s_card;
            s_lottie_timer  = lv_timer_create(deferred_lottie_cb, 1100, nullptr);
            lv_timer_set_repeat_count(s_lottie_timer, 1);
        }
    }

    // Create forecast strip (bottom)
    s_strip = forecast_strip_create(s_screen);
    if (s_strip && fc) {
        forecast_strip_update(s_strip, fc);
    }

    // Fade in weather widgets
    if (s_card) fade_in(weather_card_container(s_card));
    if (s_strip) fade_in(forecast_strip_container(s_strip));

    ESP_LOGI(TAG, "WeatherOverlay: entry (conditions=%s)",
             cond && cond->valid ? "valid" : "none");
}

void WeatherOverlay::exit()
{
    // Cancel a still-pending deferred Lottie load before destroying the card.
    if (s_lottie_timer) { lv_timer_delete(s_lottie_timer); s_lottie_timer = nullptr; }
    s_lottie_card = nullptr;
    if (s_strip)     { forecast_strip_destroy(s_strip);      s_strip = NULL; }
    if (s_card)      { weather_card_destroy(s_card);         s_card = NULL; }
    ESP_LOGI(TAG, "WeatherOverlay: exit");
}

void WeatherOverlay::react(EvDisplayTimeout const &)
{
    transit<ClockFull>();
}

void WeatherOverlay::react(EvWeatherUpdate const &)
{
    const nws_conditions_t *cond = nws_get_conditions();
    if (s_card && cond) {
        weather_card_update(s_card, cond);
    }
}

void WeatherOverlay::react(EvForecastUpdate const &)
{
    const nws_forecast_t *fc = nws_get_forecast();
    if (s_strip && fc) {
        forecast_strip_update(s_strip, fc);
    }
}

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
