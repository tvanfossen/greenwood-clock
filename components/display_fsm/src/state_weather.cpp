// components/display_fsm/src/state_weather.cpp
//
// WeatherOverlay state — minimized clock + current conditions + forecast strip.
// Optionally creates particle effects for precipitation conditions.

#include "display_states.h"
#include "display_scheduler.h"
#include "display_widgets.h"
#include "nws.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "state_weather";

// Static member definitions
weather_card_t    *WeatherOverlay::s_card      = nullptr;
forecast_strip_t  *WeatherOverlay::s_strip     = nullptr;
particle_system_t *WeatherOverlay::s_particles = nullptr;

// Map NWS description to particle config (NULL = no particles)
static const particle_config_t *condition_to_particles(const char *desc)
{
    if (!desc) return NULL;

    // Check for precipitation keywords (case-insensitive substring)
    // Order matters: more specific matches first
    if (strcasestr(desc, "Thunderstorm") || strcasestr(desc, "Thunder"))
        return &PARTICLE_RAIN;  // rain + could add flash overlay later
    if (strcasestr(desc, "Snow") || strcasestr(desc, "Flurries") || strcasestr(desc, "Blizzard"))
        return &PARTICLE_SNOW;
    if (strcasestr(desc, "Freezing") || strcasestr(desc, "Ice") || strcasestr(desc, "Sleet"))
        return &PARTICLE_ICE;
    if (strcasestr(desc, "Drizzle") || strcasestr(desc, "Light Rain"))
        return &PARTICLE_DRIZZLE;
    if (strcasestr(desc, "Rain") || strcasestr(desc, "Showers"))
        return &PARTICLE_RAIN;

    return NULL;
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
    }

    // Create forecast strip (bottom)
    s_strip = forecast_strip_create(s_screen);
    if (s_strip && fc) {
        forecast_strip_update(s_strip, fc);
    }

    // Create particle effects if precipitation
    if (cond && cond->valid) {
        const particle_config_t *pcfg = condition_to_particles(cond->description);
        if (pcfg) {
            s_particles = particle_system_create(s_screen, pcfg);
            ESP_LOGI(TAG, "Particles active for: %s", cond->description);
        }
    }

    // Fade in weather widgets
    if (s_card) fade_in(weather_card_container(s_card));
    if (s_strip) fade_in(forecast_strip_container(s_strip));

    ESP_LOGI(TAG, "WeatherOverlay: entry (conditions=%s)",
             cond && cond->valid ? "valid" : "none");
}

void WeatherOverlay::exit()
{
    if (s_particles) { particle_system_destroy(s_particles); s_particles = NULL; }
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

    // Update particles if condition changed
    if (cond && cond->valid) {
        const particle_config_t *pcfg = condition_to_particles(cond->description);
        if (pcfg && !s_particles) {
            s_particles = particle_system_create(s_screen, pcfg);
        } else if (!pcfg && s_particles) {
            particle_system_destroy(s_particles);
            s_particles = NULL;
        }
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
