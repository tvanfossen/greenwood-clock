// components/display_fsm/src/state_ambient.cpp
//
// AmbientDashboard state — aggregate info panel with sunrise/sunset,
// daylight progress, aurora status, and upcoming celestial events.

#include "display_states.h"
#include "display_scheduler.h"
#include "display_widgets.h"
#include "nws.h"
#include "settings.h"
#include "esp_log.h"
#include <stdio.h>
#include <time.h>

static const char *TAG = "state_ambient";

LV_FONT_DECLARE(nunito_48);

// Static member definitions
lv_obj_t *AmbientDashboard::s_container = nullptr;

// Static meteor shower calendar (month, day, name)
static const struct {
    int month;
    int day;
    const char *name;
} meteor_showers[] = {
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
static const struct {
    int month;
    int day;
    const char *name;
} celestial_dates[] = {
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
    int sr_local = sun.sunrise_hour * 60 + sun.sunrise_min + tz_offset_min;
    int ss_local = sun.sunset_hour * 60 + sun.sunset_min + tz_offset_min;
    while (sr_local < 0) sr_local += 1440;
    while (sr_local >= 1440) sr_local -= 1440;
    while (ss_local < 0) ss_local += 1440;
    while (ss_local >= 1440) ss_local -= 1440;
    int now_min = ti.tm_hour * 60 + ti.tm_min;

    // Container
    s_container = lv_obj_create(s_screen);
    lv_obj_set_size(s_container, 700, 520);
    lv_obj_align(s_container, LV_ALIGN_TOP_LEFT, 16, 16);
    lv_obj_set_style_bg_opa(s_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_container, 0, 0);
    lv_obj_set_style_pad_all(s_container, 0, 0);
    lv_obj_clear_flag(s_container, LV_OBJ_FLAG_SCROLLABLE);

    int y_pos = 0;
    char buf[128];

    // Title
    lv_obj_t *lbl_title = lv_label_create(s_container);
    lv_obj_set_style_text_font(lbl_title, &nunito_48, 0);
    lv_obj_set_style_text_color(lbl_title, lv_color_white(), 0);
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

    if (now_min >= sr_local && now_min <= ss_local && sun.daylight_minutes > 0) {
        int progress = (now_min - sr_local) * 100 / sun.daylight_minutes;
        lv_bar_set_value(bar, progress, LV_ANIM_OFF);
    } else if (now_min > ss_local) {
        lv_bar_set_value(bar, 100, LV_ANIM_OFF);
    } else {
        lv_bar_set_value(bar, 0, LV_ANIM_OFF);
    }
    y_pos += 30;

    // Golden hour info
    lv_obj_t *lbl_golden = lv_label_create(s_container);
    lv_obj_set_style_text_font(lbl_golden, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(lbl_golden, lv_color_hex(0x8888aa), 0);
    lv_obj_align(lbl_golden, LV_ALIGN_TOP_LEFT, 0, y_pos);
    // Golden hour: ~1 hour after sunrise and ~1 hour before sunset
    int gh_morning_end = sr_local + 60;
    int gh_evening_start = ss_local - 60;
    snprintf(buf, sizeof(buf), "Golden hour: %d:%02d-%d:%02d %s, %d:%02d-%d:%02d %s",
             sr_h % 12 == 0 ? 12 : sr_h % 12, sr_m, sr_h >= 12 ? "PM" : "AM",
             (gh_morning_end / 60) % 12 == 0 ? 12 : (gh_morning_end / 60) % 12,
             gh_morning_end % 60,
             (gh_morning_end / 60) >= 12 ? "PM" : "AM",
             (gh_evening_start / 60) % 12 == 0 ? 12 : (gh_evening_start / 60) % 12,
             gh_evening_start % 60,
             (gh_evening_start / 60) >= 12 ? "PM" : "AM");
    lv_label_set_text(lbl_golden, buf);
    y_pos += 32;

    // Weather conditions (from NWS cached data)
    const nws_conditions_t *cond = nws_get_conditions();
    if (cond && cond->valid) {
        lv_obj_t *lbl_wx = lv_label_create(s_container);
        lv_obj_set_style_text_font(lbl_wx, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(lbl_wx, lv_color_hex(0xc0c0c0), 0);
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
        const char *level = aurora_prob >= 0.7f ? "HIGH" :
                            aurora_prob >= 0.4f ? "MODERATE" :
                            aurora_prob >= 0.1f ? "LOW" : "UNLIKELY";
        snprintf(buf, sizeof(buf), "Aurora: Kp=%.1f  %s (%.0f%%)", kp, level, aurora_prob * 100);
    } else {
        snprintf(buf, sizeof(buf), "Aurora: Kp data unavailable");
    }
    lv_label_set_text(lbl_aurora, buf);
    y_pos += 32;

    // Next celestial event (solstice/equinox)
    int cur_doy = ti.tm_yday;
    const char *next_event = NULL;
    int days_until = 999;
    for (int i = 0; i < (int)CELESTIAL_COUNT; i++) {
        struct tm ev_tm = {};
        ev_tm.tm_year = ti.tm_year;
        ev_tm.tm_mon = celestial_dates[i].month - 1;
        ev_tm.tm_mday = celestial_dates[i].day;
        mktime(&ev_tm);
        int diff = ev_tm.tm_yday - cur_doy;
        if (diff < 0) diff += 365;
        if (diff < days_until) {
            days_until = diff;
            next_event = celestial_dates[i].name;
        }
    }

    lv_obj_t *lbl_event = lv_label_create(s_container);
    lv_obj_set_style_text_font(lbl_event, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(lbl_event, lv_color_hex(0x8888aa), 0);
    lv_obj_align(lbl_event, LV_ALIGN_TOP_LEFT, 0, y_pos);
    if (next_event) {
        snprintf(buf, sizeof(buf), "Next: %s in %d days", next_event, days_until);
    } else {
        snprintf(buf, sizeof(buf), "Next: --");
    }
    lv_label_set_text(lbl_event, buf);
    y_pos += 28;

    // Next meteor shower
    const char *next_shower = NULL;
    int shower_days = 999;
    for (int i = 0; i < (int)SHOWER_COUNT; i++) {
        struct tm sh_tm = {};
        sh_tm.tm_year = ti.tm_year;
        sh_tm.tm_mon = meteor_showers[i].month - 1;
        sh_tm.tm_mday = meteor_showers[i].day;
        mktime(&sh_tm);
        int diff = sh_tm.tm_yday - cur_doy;
        if (diff < 0) diff += 365;
        if (diff < shower_days) {
            shower_days = diff;
            next_shower = meteor_showers[i].name;
        }
    }

    lv_obj_t *lbl_shower = lv_label_create(s_container);
    lv_obj_set_style_text_font(lbl_shower, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(lbl_shower, lv_color_hex(0x8888aa), 0);
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
    if (s_container) { lv_obj_del(s_container); s_container = NULL; }
    ESP_LOGI(TAG, "AmbientDashboard: exit");
}

void AmbientDashboard::react(EvDisplayTimeout const &)
{
    transit<ClockFull>();
}
