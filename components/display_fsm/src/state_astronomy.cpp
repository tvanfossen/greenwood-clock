// components/display_fsm/src/state_astronomy.cpp
//
// Astronomy state — moon phase, sunrise/sunset, aurora probability.

#include "display_states.h"
#include "display_scheduler.h"
#include "display_widgets.h"
#include "display_fsm.h"
#include "ui_contrast.h"
#include "nws.h"
#include "settings.h"
#include "esp_log.h"
#include <stdio.h>
#include <time.h>

static const char *TAG = "state_astro";

LV_FONT_DECLARE(nunito_48);

// Static member definitions
lv_obj_t *Astronomy::s_container = nullptr;

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

void Astronomy::entry()
{
    set_state_info(DISPLAY_STATE_ASTRONOMY, "astronomy");
    display_fsm_apply_min_bg_contrast();   // sample bg behind clock BEFORE minimizing
    minimize_clock();

    clock_settings_t cfg;
    settings_load(&cfg);

    // Get current date
    time_t now;
    time(&now);
    struct tm ti;
    localtime_r(&now, &ti);

    // Compute moon phase
    moon_phase_t moon;
    astro_calc_moon_phase(ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday, &moon);

    // Compute sunrise/sunset (returns UTC — adjust with local time offset)
    sun_times_t sun;
    astro_calc_sun_times(ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday,
                         cfg.latitude, cfg.longitude, &sun);

    // Adjust sunrise/sunset from UTC to local time
    int tz_offset_min = get_tz_offset_minutes(now);
    int sr_local = sun.sunrise_hour * 60 + sun.sunrise_min + tz_offset_min;
    int ss_local = sun.sunset_hour * 60 + sun.sunset_min + tz_offset_min;
    while (sr_local < 0) sr_local += 1440;
    while (sr_local >= 1440) sr_local -= 1440;
    while (ss_local < 0) ss_local += 1440;
    while (ss_local >= 1440) ss_local -= 1440;

    // OKLCH text contrast over the panel, from the single NVS clock text colour.
    // (Semantic accents below — sunrise gold, aurora violet — are kept on purpose.)
    lv_color_t panel = lv_color_hex(0x0a0a1e);
    lv_color_t base = clock_widget_user_color(get_clock());
    lv_color_t primary = ui_legible(base, panel);
    lv_color_t secondary = lv_color_mix(panel, primary, 110);

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

    // Moon phase title
    lv_obj_t *lbl_title = lv_label_create(s_container);
    lv_obj_set_style_text_font(lbl_title, &nunito_48, 0);
    lv_obj_set_style_text_color(lbl_title, primary, 0);
    lv_obj_align(lbl_title, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_label_set_text(lbl_title, moon.phase_name);

    // Moon illumination
    char buf[128];
    lv_obj_t *lbl_illum = lv_label_create(s_container);
    lv_obj_set_style_text_font(lbl_illum, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(lbl_illum, secondary, 0);
    lv_obj_align(lbl_illum, LV_ALIGN_TOP_LEFT, 0, 60);
    snprintf(buf, sizeof(buf), "%.0f%% illuminated", moon.illumination_pct);
    lv_label_set_text(lbl_illum, buf);

    // Next full/new moon
    lv_obj_t *lbl_next = lv_label_create(s_container);
    lv_obj_set_style_text_font(lbl_next, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(lbl_next, secondary, 0);
    lv_obj_align(lbl_next, LV_ALIGN_TOP_LEFT, 0, 92);
    snprintf(buf, sizeof(buf), "Full in %d days  |  New in %d days",
             moon.days_to_full, moon.days_to_new);
    lv_label_set_text(lbl_next, buf);

    // Moon visual — simple circle with illumination fill
    lv_obj_t *moon_circle = lv_obj_create(s_container);
    lv_obj_set_size(moon_circle, 120, 120);
    lv_obj_align(moon_circle, LV_ALIGN_TOP_LEFT, 0, 130);
    lv_obj_set_style_radius(moon_circle, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_color(moon_circle, lv_color_hex(0x555555), 0);
    lv_obj_set_style_border_width(moon_circle, 2, 0);
    // Brightness maps to illumination
    uint8_t bright = (uint8_t)(moon.illumination_pct * 2.55f);
    lv_obj_set_style_bg_color(moon_circle, lv_color_make(bright, bright, bright), 0);
    lv_obj_set_style_bg_opa(moon_circle, LV_OPA_COVER, 0);
    lv_obj_clear_flag(moon_circle,
                      (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));

    // Sunrise / Sunset section
    lv_obj_t *lbl_sun_title = lv_label_create(s_container);
    lv_obj_set_style_text_font(lbl_sun_title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(lbl_sun_title, lv_color_hex(0xE9C46A), 0);
    lv_obj_align(lbl_sun_title, LV_ALIGN_TOP_LEFT, 0, 270);

    int sr_h = sr_local / 60;
    int sr_m = sr_local % 60;
    int ss_h = ss_local / 60;
    int ss_m = ss_local % 60;
    snprintf(buf, sizeof(buf), "Sunrise %d:%02d %s   Sunset %d:%02d %s",
             sr_h % 12 == 0 ? 12 : sr_h % 12, sr_m, sr_h >= 12 ? "PM" : "AM",
             ss_h % 12 == 0 ? 12 : ss_h % 12, ss_m, ss_h >= 12 ? "PM" : "AM");
    lv_label_set_text(lbl_sun_title, buf);

    // Daylight remaining
    int now_min = ti.tm_hour * 60 + ti.tm_min;
    lv_obj_t *lbl_daylight = lv_label_create(s_container);
    lv_obj_set_style_text_font(lbl_daylight, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(lbl_daylight, secondary, 0);
    lv_obj_align(lbl_daylight, LV_ALIGN_TOP_LEFT, 0, 300);

    int daylight_h = sun.daylight_minutes / 60;
    int daylight_m = sun.daylight_minutes % 60;
    if (now_min < sr_local) {
        int until_sr = sr_local - now_min;
        snprintf(buf, sizeof(buf), "%dh %dm daylight  |  Sunrise in %dh %dm",
                 daylight_h, daylight_m, until_sr / 60, until_sr % 60);
    } else if (now_min < ss_local) {
        int remaining = ss_local - now_min;
        snprintf(buf, sizeof(buf), "%dh %dm daylight  |  %dh %dm remaining",
                 daylight_h, daylight_m, remaining / 60, remaining % 60);
    } else {
        snprintf(buf, sizeof(buf), "%dh %dm daylight  |  After sunset",
                 daylight_h, daylight_m);
    }
    lv_label_set_text(lbl_daylight, buf);

    // Aurora section
    float kp = nws_get_kp_index();
    float aurora_prob = astro_calc_aurora_probability(kp, cfg.latitude);

    lv_obj_t *lbl_aurora = lv_label_create(s_container);
    lv_obj_set_style_text_font(lbl_aurora, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(lbl_aurora, lv_color_hex(0xa490c2), 0);
    lv_obj_align(lbl_aurora, LV_ALIGN_TOP_LEFT, 0, 340);
    if (kp > 0.0f) {
        const char *level = aurora_prob >= 0.7f ? "HIGH" :
                            aurora_prob >= 0.4f ? "MODERATE" :
                            aurora_prob >= 0.1f ? "LOW" : "UNLIKELY";
        snprintf(buf, sizeof(buf), "Aurora: Kp=%.1f  %s (%.0f%%)", kp, level, aurora_prob * 100);
    } else {
        snprintf(buf, sizeof(buf), "Aurora: Kp data unavailable");
    }
    lv_label_set_text(lbl_aurora, buf);

    fade_in(s_container);

    ESP_LOGI(TAG, "Astronomy: entry (moon=%s %.0f%%, sr=%d:%02d, ss=%d:%02d)",
             moon.phase_name, moon.illumination_pct,
             sr_h, sr_m, ss_h, ss_m);
}

void Astronomy::exit()
{
    if (s_container) { lv_anim_delete(s_container, NULL); lv_obj_delete(s_container); s_container = NULL; }
    ESP_LOGI(TAG, "Astronomy: exit");
}

void Astronomy::react(EvDisplayTimeout const &)
{
    transit<ClockFull>();
}
