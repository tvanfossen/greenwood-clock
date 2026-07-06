// components/display_fsm/src/state_ambient.cpp
//
// AmbientDashboard state — aggregate info panel with sunrise/sunset,
// daylight progress, aurora status, and upcoming celestial events.

#include "display_states.h"
#include "display_scheduler.h"
#include "display_widgets.h"
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

// Normalize a minutes-of-day value into [0, 1440).
static int wrap_day_minutes(int m)
{
    m %= 1440;
    if (m < 0) m += 1440;
    return m;
}

// Daylight progress percentage (0–100) for the current minute of day.
static int daylight_progress(int now_min, int sr, int ss, int daylight_minutes)
{
    if (now_min > ss) return 100;
    if (now_min < sr || daylight_minutes <= 0) return 0;
    return (now_min - sr) * 100 / daylight_minutes;
}

// Qualitative aurora visibility label from a 0–1 probability.
static const char *aurora_level(float prob)
{
    if (prob >= 0.7f) return "HIGH";
    if (prob >= 0.4f) return "MODERATE";
    if (prob >= 0.1f) return "LOW";
    return "UNLIKELY";
}

// Soonest upcoming dated event (wrapping past year-end). Returns its name and
// days-until, or NULL. Shared by the solstice/equinox and meteor-shower panels.
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

void AmbientDashboard::entry()
{
    set_state_info(DISPLAY_STATE_AMBIENT, "ambient");
    display_fsm_apply_min_bg_contrast();   // sample bg behind clock BEFORE minimizing
    minimize_clock();

    clock_settings_t cfg;
    settings_load(&cfg);

    // Fixed high-contrast palette for secondary screens (NVS text_color is clock-only)
    lv_color_t primary = lv_color_white();
    lv_color_t secondary = lv_color_hex(0xb0b0c0);

    time_t now;
    time(&now);
    struct tm ti;
    localtime_r(&now, &ti);

    // Compute sun times
    sun_times_t sun;
    astro_calc_sun_times(ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday,
                         cfg.latitude, cfg.longitude, &sun);

    int tz_offset_min = get_tz_offset_minutes(now);
    int sr_local = wrap_day_minutes(sun.sunrise_hour * 60 + sun.sunrise_min + tz_offset_min);
    int ss_local = wrap_day_minutes(sun.sunset_hour * 60 + sun.sunset_min + tz_offset_min);
    int now_min = ti.tm_hour * 60 + ti.tm_min;

    // Container with dark semi-opaque backdrop for contrast.
    // Y=66: reserves 50px alert banner zone + 16px margin.
    s_container = lv_obj_create(s_screen);
    lv_obj_set_size(s_container, 556, 470);
    lv_obj_align(s_container, LV_ALIGN_TOP_LEFT, 16, ALERT_BANNER_HEIGHT + 16);
    lv_obj_set_style_bg_color(s_container, lv_color_hex(0x0a0a1e), 0);
    lv_obj_set_style_bg_opa(s_container, LV_OPA_70, 0);
    lv_obj_set_style_radius(s_container, 12, 0);
    lv_obj_set_style_border_width(s_container, 0, 0);
    lv_obj_set_style_pad_all(s_container, 16, 0);
    lv_obj_clear_flag(s_container, LV_OBJ_FLAG_SCROLLABLE);

    int y_pos = 0;
    char buf[128];

    // Title
    lv_obj_t *lbl_title = lv_label_create(s_container);
    lv_obj_set_style_text_font(lbl_title, &nunito_48, 0);
    lv_obj_set_style_text_color(lbl_title, primary, 0);
    lv_obj_align(lbl_title, LV_ALIGN_TOP_LEFT, 0, y_pos);
    lv_label_set_text(lbl_title, "Dashboard");
    y_pos += 56;

    // Sunrise / Sunset
    lv_obj_t *lbl_sun = lv_label_create(s_container);
    lv_obj_set_style_text_font(lbl_sun, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(lbl_sun, lv_color_hex(0xE9C46A), 0);
    lv_obj_align(lbl_sun, LV_ALIGN_TOP_LEFT, 0, y_pos);
    int sr_h = sr_local / 60, sr_m = sr_local % 60;
    int ss_h = ss_local / 60, ss_m = ss_local % 60;
    snprintf(buf, sizeof(buf), "Sunrise %d:%02d %s  |  Sunset %d:%02d %s",
             sr_h % 12 == 0 ? 12 : sr_h % 12, sr_m, sr_h >= 12 ? "PM" : "AM",
             ss_h % 12 == 0 ? 12 : ss_h % 12, ss_m, ss_h >= 12 ? "PM" : "AM");
    lv_label_set_text(lbl_sun, buf);
    y_pos += 32;

    // Daylight progress bar
    lv_obj_t *bar = lv_bar_create(s_container);
    lv_obj_set_size(bar, 500, 16);
    lv_obj_align(bar, LV_ALIGN_TOP_LEFT, 0, y_pos);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0x333344), LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, lv_color_hex(0xE9C46A), LV_PART_INDICATOR);
    lv_obj_set_style_radius(bar, 8, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, 8, LV_PART_INDICATOR);
    lv_bar_set_range(bar, 0, 100);

    lv_bar_set_value(bar, daylight_progress(now_min, sr_local, ss_local,
                                            sun.daylight_minutes), LV_ANIM_OFF);
    y_pos += 30;

    // Golden hour info
    lv_obj_t *lbl_golden = lv_label_create(s_container);
    lv_obj_set_style_text_font(lbl_golden, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(lbl_golden, secondary, 0);
    lv_obj_align(lbl_golden, LV_ALIGN_TOP_LEFT, 0, y_pos);
    // Golden hour: ~1 hour after sunrise and ~1 hour before sunset
    int gh_morning_end = sr_local + 60;
    int gh_evening_start = ss_local - 60;
    int gh_me_h = gh_morning_end / 60;
    int gh_me_m = gh_morning_end % 60;
    int gh_es_h = gh_evening_start / 60;
    int gh_es_m = gh_evening_start % 60;
    snprintf(buf, sizeof(buf), "Golden hour: %d:%02d-%d:%02d %s, %d:%02d-%d:%02d %s",
             sr_h % 12 == 0 ? 12 : sr_h % 12, sr_m,
             gh_me_h % 12 == 0 ? 12 : gh_me_h % 12, gh_me_m,
             gh_me_h >= 12 ? "PM" : "AM",
             gh_es_h % 12 == 0 ? 12 : gh_es_h % 12, gh_es_m,
             ss_h % 12 == 0 ? 12 : ss_h % 12, ss_m,
             ss_h >= 12 ? "PM" : "AM");
    lv_label_set_text(lbl_golden, buf);
    y_pos += 32;

    // Weather conditions (from NWS cached data)
    const nws_conditions_t *cond = nws_get_conditions();
    if (cond && cond->valid) {
        lv_obj_t *lbl_wx = lv_label_create(s_container);
        lv_obj_set_style_text_font(lbl_wx, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(lbl_wx, secondary, 0);
        lv_obj_align(lbl_wx, LV_ALIGN_TOP_LEFT, 0, y_pos);
        snprintf(buf, sizeof(buf), "%s  |  Humidity %d%%  |  Visibility %.0f mi",
                 cond->description, cond->humidity, cond->visibility_km * 0.621371f);
        lv_label_set_text(lbl_wx, buf);
        y_pos += 28;
    }

    // Aurora status
    float kp = nws_get_kp_index();
    float aurora_prob = astro_calc_aurora_probability(kp, cfg.latitude);

    lv_obj_t *lbl_aurora = lv_label_create(s_container);
    lv_obj_set_style_text_font(lbl_aurora, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(lbl_aurora, lv_color_hex(0xa490c2), 0);
    lv_obj_align(lbl_aurora, LV_ALIGN_TOP_LEFT, 0, y_pos);
    if (kp > 0.0f) {
        snprintf(buf, sizeof(buf), "Aurora: Kp=%.1f  %s (%.0f%%)",
                 kp, aurora_level(aurora_prob), aurora_prob * 100);
    } else {
        snprintf(buf, sizeof(buf), "Aurora: Kp data unavailable");
    }
    lv_label_set_text(lbl_aurora, buf);
    y_pos += 32;

    // Next celestial event (solstice/equinox)
    int cur_doy = ti.tm_yday;
    int days_until = 999;
    const char *next_event = next_event_after(celestial_dates, (int)CELESTIAL_COUNT,
                                              ti.tm_year, cur_doy, &days_until);

    lv_obj_t *lbl_event = lv_label_create(s_container);
    lv_obj_set_style_text_font(lbl_event, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(lbl_event, secondary, 0);
    lv_obj_align(lbl_event, LV_ALIGN_TOP_LEFT, 0, y_pos);
    if (next_event) {
        snprintf(buf, sizeof(buf), "Next: %s in %d days", next_event, days_until);
    } else {
        snprintf(buf, sizeof(buf), "Next: --");
    }
    lv_label_set_text(lbl_event, buf);
    y_pos += 28;

    // Next meteor shower
    int shower_days = 999;
    const char *next_shower = next_event_after(meteor_showers, (int)SHOWER_COUNT,
                                               ti.tm_year, cur_doy, &shower_days);

    lv_obj_t *lbl_shower = lv_label_create(s_container);
    lv_obj_set_style_text_font(lbl_shower, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(lbl_shower, secondary, 0);
    lv_obj_align(lbl_shower, LV_ALIGN_TOP_LEFT, 0, y_pos);
    if (next_shower) {
        snprintf(buf, sizeof(buf), "Meteor shower: %s in %d days", next_shower, shower_days);
    } else {
        snprintf(buf, sizeof(buf), "Meteor shower: --");
    }
    lv_label_set_text(lbl_shower, buf);

    fade_in(s_container);

    ESP_LOGI(TAG, "AmbientDashboard: entry");
}

void AmbientDashboard::exit()
{
    if (s_container) { lv_anim_delete(s_container, NULL); lv_obj_delete(s_container); s_container = NULL; }
    ESP_LOGI(TAG, "AmbientDashboard: exit");
}

void AmbientDashboard::react(EvDisplayTimeout const &)
{
    transit<ClockFull>();
}
