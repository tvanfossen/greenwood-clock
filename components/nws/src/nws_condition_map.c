/**
 * @file nws_condition_map.c
 * @version 1
 * @brief Maps NWS short_forecast strings to Lottie animation file paths.
 *
 * NWS uses text descriptions ("Mostly Cloudy", "Light Rain"), not numeric codes.
 * Substring matching against known patterns.
 *
 * DEAD CODE: compiled (components/nws/CMakeLists.txt) and declared in nws.h, but
 * nothing calls nws_condition_to_lottie_path(). The live condition table is the
 * one in components/display_fsm/src/weather_widgets.c, which duplicates this one.
 */

#include "nws.h"
#include <string.h>
#include <strings.h>  // strcasestr
#include <stdio.h>

// Match table — order matters: more specific patterns first
static const struct {
    const char *pattern;
    const char *day_file;
    const char *night_file;
} condition_map[] = {
    { "Thunderstorm",   "day/thunderstorm.json", "night/thunderstorm.json" },
    { "Thunder",        "day/thunderstorm.json", "night/thunderstorm.json" },
    { "Freezing Rain",  "day/ice.json",          "night/ice.json" },
    { "Freezing",       "day/ice.json",          "night/ice.json" },
    { "Ice Pellets",    "day/ice.json",          "night/ice.json" },
    { "Sleet",          "day/ice.json",          "night/ice.json" },
    { "Blizzard",       "day/snow.json",         "night/snow.json" },
    { "Snow",           "day/snow.json",         "night/snow.json" },
    { "Flurries",       "day/snow.json",         "night/snow.json" },
    { "Drizzle",        "day/drizzle.json",      "night/drizzle.json" },
    { "Light Rain",     "day/drizzle.json",      "night/drizzle.json" },
    { "Rain",           "day/rain.json",         "night/rain.json" },
    { "Showers",        "day/rain.json",         "night/rain.json" },
    { "Fog",            "day/fog.json",          "night/fog.json" },
    { "Mist",           "day/fog.json",          "night/fog.json" },
    { "Haze",           "day/haze.json",         "night/fog.json" },
    { "Smoke",          "day/haze.json",         "night/fog.json" },
    { "Windy",          "day/windy.json",        "night/clear.json" },
    { "Breezy",         "day/windy.json",        "night/clear.json" },
    { "Overcast",       "day/overcast.json",     "night/overcast.json" },
    { "Mostly Cloudy",  "day/mostly_cloudy.json","night/mostly_cloudy.json" },
    { "Partly Cloudy",  "day/partly_cloudy.json","night/partly_cloudy.json" },
    { "Partly Sunny",   "day/partly_cloudy.json","night/partly_cloudy.json" },
    { "Mostly Sunny",   "day/clear.json",        "night/clear.json" },
    { "Mostly Clear",   "day/clear.json",        "night/clear.json" },
    { "Sunny",          "day/clear.json",        "night/clear.json" },
    { "Clear",          "day/clear.json",        "night/clear.json" },
    { "Fair",           "day/clear.json",        "night/clear.json" },
    { "Cloudy",         "day/mostly_cloudy.json","night/mostly_cloudy.json" },
};

#define MAP_COUNT (sizeof(condition_map) / sizeof(condition_map[0]))

/**
 * @brief Write "A:/lottie/weather/<day|night>/<file>.json" for the first table
 *        pattern found (case-insensitive substring) in @p short_forecast.
 *
 * Falls back to mostly_cloudy when nothing matches or @p short_forecast is NULL;
 * no-op if @p path_out is NULL or @p max_len is 0. Unreferenced (see file note).
 * @version 1
 * @dg_internal
 */
void nws_condition_to_lottie_path(const char *short_forecast, bool is_daytime,
                                   char *path_out, size_t max_len)
{
    if (!path_out || max_len == 0) return;

    const char *file = is_daytime ? "day/mostly_cloudy.json" : "night/mostly_cloudy.json";

    if (short_forecast) {
        for (size_t i = 0; i < MAP_COUNT; i++) {
            if (strcasestr(short_forecast, condition_map[i].pattern)) {
                file = is_daytime ? condition_map[i].day_file : condition_map[i].night_file;
                break;
            }
        }
    }

    snprintf(path_out, max_len, "A:/lottie/weather/%s", file);
}
