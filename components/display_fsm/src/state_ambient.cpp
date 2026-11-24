/**
 * @file state_ambient.cpp
 * @version 1
 * @brief AmbientDashboard state: panel with sunrise/sunset, daylight progress, golden hour, weather, aurora and upcoming celestial events.
 */
// AmbientDashboard state — aggregate info panel with sunrise/sunset,
// daylight progress, aurora status, and upcoming celestial events.

#include "display_states.h"
#include "display_scheduler.h"
#include "display_widgets.h"
#include "info_panel.h"
#include "display_fsm.h"
#include "nws.h"
#include "settings.h"
#include "esp_log.h"
#include <stdio.h>
#include <time.h>

static const char *TAG = "state_ambient";

LV_FONT_DECLARE(nunito_48);

// Static member definitions
lv_obj_t *AmbientDashboard::s_container = nullptr;

typedef struct {
    int month;
    int day;
    const char *name;
} dated_event_t;

// Static meteor shower calendar (month, day, name)
static const dated_event_t meteor_showers[] = {
    {  1,  3, "Quadrantids" },
    {  4, 22, "Lyrids" },
    {  5,  6, "Eta Aquariids" },
    {  7, 30, "Delta Aquariids" },
    {  8, 12, "Perseids" },
    { 10, 21, "Orionids" },
    { 11, 17, "Leonids" },
    { 12, 14, "Geminids" },
};
#define SHOWER_COUNT (sizeof(meteor_showers) / sizeof(meteor_showers[0]))

// Solstice/equinox approximate dates (for current year — good enough)
static const dated_event_t celestial_dates[] = {
    {  3, 20, "Vernal Equinox" },
    {  6, 20, "Summer Solstice" },
    {  9, 22, "Autumnal Equinox" },
    { 12, 21, "Winter Solstice" },
};
#define CELESTIAL_COUNT (sizeof(celestial_dates) / sizeof(celestial_dates[0]))

// Compute timezone offset in minutes from UTC using localtime/gmtime difference.
// Portable — does not require tm_gmtoff extension.
/**
 * @brief Local timezone offset from UTC in minutes, from the localtime/gmtime difference (handles day and month wrap).
 * @version 1
 * @req REQ-AMB-001
 * @return Offset in minutes (local minus UTC).
 */
static int get_tz_offset_minutes(time_t now)
{
    struct tm local_tm, utc_tm;
    localtime_r(&now, &local_tm);
    gmtime_r(&now, &utc_tm);
    int offset = (local_tm.tm_hour - utc_tm.tm_hour) * 60
               + (local_tm.tm_min  - utc_tm.tm_min);
    // Handle day boundary
    int day_diff = local_tm.tm_mday - utc_tm.tm_mday;
    if (day_diff > 1) day_diff = -1;     // month wrap (local is 1st, utc is 31st)
    else if (day_diff < -1) day_diff = 1; // month wrap (local is 31st, utc is 1st)
    offset += day_diff * 1440;
    return offset;
}

// Daylight progress percentage (0–100) for the current minute of day.
/**
 * @brief Daylight progress for the current minute of day between sunrise and sunset.
 * @version 1
 * @req REQ-AMB-001
 * @return Percentage 0..100 (0 before sunrise or with no daylight, 100 after sunset).
 */
static int daylight_progress(int now_min, int sr, int ss, int daylight_minutes)
{
    if (now_min > ss) return 100;
    if (now_min < sr || daylight_minutes <= 0) return 0;
    return (now_min - sr) * 100 / daylight_minutes;
}

// Qualitative aurora visibility label from a 0–1 probability.
/**
 * @brief Map an aurora probability (0..1) to a visibility label.
 * @version 1
 * @req REQ-AMB-001
 * @return "HIGH" (>=0.7), "MODERATE" (>=0.4), "LOW" (>=0.1) or "UNLIKELY".
 */
static const char *aurora_level(float prob)
{
    if (prob >= 0.7f) return "HIGH";
    if (prob >= 0.4f) return "MODERATE";
    if (prob >= 0.1f) return "LOW";
    return "UNLIKELY";
}

// Soonest upcoming dated event (wrapping past year-end). Returns its name and
// days-until, or NULL. Shared by the solstice/equinox and meteor-shower panels.
/**
 * @brief Find the soonest upcoming dated event from today, wrapping past year-end.
 * @version 1
 * @req REQ-AMB-001
 * @return Event name with *days_out set to days until it, or NULL if @p count is 0.
 */
static const char *next_event_after(const dated_event_t *events, int count,
                                    int year, int cur_doy, int *days_out)
{
    const char *best = NULL;
    int best_days = 999;
    for (int i = 0; i < count; i++) {
        struct tm ev = {};
        ev.tm_year = year;
        ev.tm_mon  = events[i].month - 1;
        ev.tm_mday = events[i].day;
        mktime(&ev);
        int diff = ev.tm_yday - cur_doy;
        if (diff < 0) diff += 365;
        if (diff < best_days) {
            best_days = diff;
            best = events[i].name;
        }
    }
    *days_out = best_days;
    return best;
}

/**
 * @brief Format "Sunrise h:mm AM  |  Sunset h:mm PM" from local minute-of-day values.
 * @version 1
 * @req REQ-AMB-001
 */
static void amb_fmt_sun(char *buf, size_t n, int sr_local, int ss_local)
{
    int sr_h = sr_local / 60, sr_m = sr_local % 60;
    int ss_h = ss_local / 60, ss_m = ss_local % 60;
    snprintf(buf, n, "Sunrise %d:%02d %s  |  Sunset %d:%02d %s",
             sr_h % 12 == 0 ? 12 : sr_h % 12, sr_m, sr_h >= 12 ? "PM" : "AM",
             ss_h % 12 == 0 ? 12 : ss_h % 12, ss_m, ss_h >= 12 ? "PM" : "AM");
}

/**
 * @brief Format the golden-hour line: ~1 h after sunrise and ~1 h before sunset.
 * @version 1
 * @req REQ-AMB-001
 */
static void amb_fmt_golden(char *buf, size_t n, int sr_local, int ss_local)
{
    int sr_h = sr_local / 60, sr_m = sr_local % 60;
    int ss_h = ss_local / 60, ss_m = ss_local % 60;
    // Golden hour: ~1 hour after sunrise and ~1 hour before sunset
    int gh_morning_end = sr_local + 60;
    int gh_evening_start = ss_local - 60;
    int gh_me_h = gh_morning_end / 60;
    int gh_me_m = gh_morning_end % 60;
    int gh_es_h = gh_evening_start / 60;
    int gh_es_m = gh_evening_start % 60;
    snprintf(buf, n, "Golden hour: %d:%02d-%d:%02d %s, %d:%02d-%d:%02d %s",
             sr_h % 12 == 0 ? 12 : sr_h % 12, sr_m,
             gh_me_h % 12 == 0 ? 12 : gh_me_h % 12, gh_me_m,
             gh_me_h >= 12 ? "PM" : "AM",
             gh_es_h % 12 == 0 ? 12 : gh_es_h % 12, gh_es_m,
             ss_h % 12 == 0 ? 12 : ss_h % 12, ss_m,
             ss_h >= 12 ? "PM" : "AM");
}

/**
 * @brief Add the daylight progress bar (sunrise to sunset) at @p y.
 * v2: colours from the theme roles (ui_theme), no literals.
 * @version 2
 * @req REQ-AMB-001
 */
static void amb_add_daylight_bar(lv_obj_t *parent, int y, int now_min, int sr_local,
                                 int ss_local, int daylight_minutes)
{
    lv_obj_t *bar = lv_bar_create(parent);
    lv_obj_set_size(bar, 500, 16);
    lv_obj_align(bar, LV_ALIGN_TOP_LEFT, 0, y);
    lv_obj_set_style_bg_color(bar, ui_theme_color(UI_ROLE_SURFACE_INSET), LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, ui_theme_color(UI_ROLE_ACCENT), LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar, 8, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, 8, LV_PART_INDICATOR);
    lv_bar_set_range(bar, 0, 100);
    lv_bar_set_value(bar, daylight_progress(now_min, sr_local, ss_local, daylight_minutes),
                     LV_ANIM_OFF);
}

/**
 * @brief Add the NWS conditions line (description, humidity, visibility) if conditions are valid.
 * @version 1
 * @req REQ-AMB-001
 * @return Vertical space used: 28 px when the line was added, else 0.
 */
static int amb_add_conditions(lv_obj_t *parent, int y, lv_color_t color)
{
    const nws_conditions_t *cond = nws_get_conditions();
    if (!cond || !cond->valid) {
        return 0;
    }
    char buf[128];
    snprintf(buf, sizeof(buf), "%s  |  Humidity %d%%  |  Visibility %.0f mi",
             cond->description, cond->humidity, cond->visibility_km * 0.621371f);
    lv_label_set_text(info_panel_label(parent, &lv_font_montserrat_20, color, y), buf);
    return 28;
}

/**
 * @brief Format the aurora line from Kp and probability ("Kp data unavailable" when Kp is 0).
 * @version 1
 * @req REQ-AMB-001
 */
static void amb_fmt_aurora(char *buf, size_t n, float kp, float aurora_prob)
{
    if (kp > 0.0f) {
        snprintf(buf, n, "Aurora: Kp=%.1f  %s (%.0f%%)",
                 kp, aurora_level(aurora_prob), aurora_prob * 100);
    } else {
        snprintf(buf, n, "Aurora: Kp data unavailable");
    }
}

/**
 * @brief Format "<prefix>: <event> in N days", or "<prefix>: --" when there is no event.
 * @version 1
 * @req REQ-AMB-001
 */
static void amb_fmt_next(char *buf, size_t n, const char *prefix, const char *event, int days)
{
    if (event) {
        snprintf(buf, n, "%s: %s in %d days", prefix, event, days);
    } else {
        snprintf(buf, n, "%s: --", prefix);
    }
}

/**
 * @brief Enter: sample bg contrast and minimize the clock, then build the dashboard panel (sun times, daylight bar, golden hour, NWS conditions, aurora from Kp, next solstice/equinox and meteor shower) and fade it in.
 * v2: sections split into helpers and the panel shared via info_panel.h (knots v2
 *     cognitive limit); output unchanged.
 * v3: clock uses the user's exact colour; no background sampling before minimizing.
 * v4: colours from the theme roles (ui_theme), no literals.
 * @version 4
 * @req REQ-AMB-001
 * @req REQ-ASTRO-001
 */
void AmbientDashboard::entry()
{
    set_state_info(DISPLAY_STATE_AMBIENT, "ambient");
    minimize_clock();

    clock_settings_t cfg;
    settings_load(&cfg);

    time_t now;
    time(&now);
    struct tm ti;
    localtime_r(&now, &ti);

    // Compute sun times
    sun_times_t sun;
    astro_calc_sun_times(ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday,
                         cfg.latitude, cfg.longitude, &sun);

    int tz_offset_min = get_tz_offset_minutes(now);
    int sr_local = info_panel_wrap_day_minutes(sun.sunrise_hour * 60 + sun.sunrise_min + tz_offset_min);
    int ss_local = info_panel_wrap_day_minutes(sun.sunset_hour * 60 + sun.sunset_min + tz_offset_min);
    int now_min = ti.tm_hour * 60 + ti.tm_min;

    s_container = info_panel_create(s_screen);
    info_panel_palette_t pal = info_panel_palette(s_container);
    int y_pos = 0;
    char buf[128];

    lv_label_set_text(info_panel_label(s_container, &nunito_48, pal.text, y_pos), "Dashboard");
    y_pos += 56;

    amb_fmt_sun(buf, sizeof(buf), sr_local, ss_local);
    lv_label_set_text(info_panel_label(s_container, &lv_font_montserrat_24, pal.accent, y_pos), buf);
    y_pos += 32;

    amb_add_daylight_bar(s_container, y_pos, now_min, sr_local, ss_local, sun.daylight_minutes);
    y_pos += 30;

    amb_fmt_golden(buf, sizeof(buf), sr_local, ss_local);
    lv_label_set_text(info_panel_label(s_container, &lv_font_montserrat_20, pal.muted, y_pos), buf);
    y_pos += 32;

    y_pos += amb_add_conditions(s_container, y_pos, pal.muted);

    float kp = nws_get_kp_index();
    amb_fmt_aurora(buf, sizeof(buf), kp, astro_calc_aurora_probability(kp, cfg.latitude));
    lv_label_set_text(info_panel_label(s_container, &lv_font_montserrat_20, pal.accent_alt, y_pos), buf);
    y_pos += 32;

    // Next celestial event (solstice/equinox) and meteor shower
    int days_until = 999;
    const char *next_event = next_event_after(celestial_dates, (int)CELESTIAL_COUNT,
                                              ti.tm_year, ti.tm_yday, &days_until);
    amb_fmt_next(buf, sizeof(buf), "Next", next_event, days_until);
    lv_label_set_text(info_panel_label(s_container, &lv_font_montserrat_20, pal.muted, y_pos), buf);
    y_pos += 28;

    int shower_days = 999;
    const char *next_shower = next_event_after(meteor_showers, (int)SHOWER_COUNT,
                                               ti.tm_year, ti.tm_yday, &shower_days);
    amb_fmt_next(buf, sizeof(buf), "Meteor shower", next_shower, shower_days);
    lv_label_set_text(info_panel_label(s_container, &lv_font_montserrat_20, pal.muted, y_pos), buf);

    fade_in(s_container);

    ESP_LOGI(TAG, "AmbientDashboard: entry");
}

/**
 * @brief Leave: delete the dashboard panel.
 * @version 1
 * @req REQ-AMB-001
 */
void AmbientDashboard::exit()
{
    if (s_container) { lv_anim_delete(s_container, NULL); lv_obj_delete(s_container); s_container = NULL; }
    ESP_LOGI(TAG, "AmbientDashboard: exit");
}

/**
 * @brief Display timeout: return to ClockFull.
 * @version 1
 * @req REQ-AMB-001
 * @req REQ-DISP-002
 */
void AmbientDashboard::react(EvDisplayTimeout const &)
{
    transit<ClockFull>();
}
