/**
 * @file state_astronomy.cpp
 * @version 1
 * @brief Astronomy state — moon phase, sunrise/sunset, aurora probability.
 */

#include "display_states.h"
#include "display_scheduler.h"
#include "display_widgets.h"
#include "display_fsm.h"
#include "ui_contrast.h"
#include "info_panel.h"
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
/**
 * @brief Local-minus-UTC offset at @p now, from localtime_r/gmtime_r differences
 *        with day-boundary correction.
 * @version 1
 * @req REQ-ASTRO-001
 * @return Offset in minutes (negative west of UTC).
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

/**
 * @brief Build the moon section: phase name, illumination, days to full/new, and a brightness-scaled disc.
 * @version 1
 * @req REQ-ASTRO-001
 */
static void astro_build_moon(lv_obj_t *parent, const moon_phase_t *moon,
                             lv_color_t primary, lv_color_t secondary)
{
    char buf[128];
    lv_label_set_text(info_panel_label(parent, &nunito_48, primary, 0), moon->phase_name);

    snprintf(buf, sizeof(buf), "%.0f%% illuminated", moon->illumination_pct);
    lv_label_set_text(info_panel_label(parent, &lv_font_montserrat_24, secondary, 60), buf);

    snprintf(buf, sizeof(buf), "Full in %d days  |  New in %d days",
             moon->days_to_full, moon->days_to_new);
    lv_label_set_text(info_panel_label(parent, &lv_font_montserrat_20, secondary, 92), buf);

    // Moon visual — simple circle with illumination fill
    lv_obj_t *moon_circle = lv_obj_create(parent);
    lv_obj_set_size(moon_circle, 120, 120);
    lv_obj_align(moon_circle, LV_ALIGN_TOP_LEFT, 0, 130);
    lv_obj_set_style_radius(moon_circle, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_color(moon_circle, lv_color_hex(0x555555), 0);
    lv_obj_set_style_border_width(moon_circle, 2, 0);
    // Brightness maps to illumination
    uint8_t bright = (uint8_t)(moon->illumination_pct * 2.55f);
    lv_obj_set_style_bg_color(moon_circle, lv_color_make(bright, bright, bright), 0);
    lv_obj_set_style_bg_opa(moon_circle, LV_OPA_COVER, 0);
    lv_obj_clear_flag(moon_circle,
                      (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
}

/**
 * @brief Format "Sunrise h:mm AM   Sunset h:mm PM" from local minute-of-day values.
 * @version 1
 * @req REQ-ASTRO-001
 */
static void astro_fmt_sun(char *buf, size_t n, int sr_local, int ss_local)
{
    int sr_h = sr_local / 60, sr_m = sr_local % 60;
    int ss_h = ss_local / 60, ss_m = ss_local % 60;
    snprintf(buf, n, "Sunrise %d:%02d %s   Sunset %d:%02d %s",
             sr_h % 12 == 0 ? 12 : sr_h % 12, sr_m, sr_h >= 12 ? "PM" : "AM",
             ss_h % 12 == 0 ? 12 : ss_h % 12, ss_m, ss_h >= 12 ? "PM" : "AM");
}

/**
 * @brief Format total daylight plus time until sunrise, daylight remaining, or "After sunset".
 * @version 1
 * @req REQ-ASTRO-001
 */
static void astro_fmt_daylight(char *buf, size_t n, int now_min, int sr_local, int ss_local,
                               int daylight_minutes)
{
    int daylight_h = daylight_minutes / 60;
    int daylight_m = daylight_minutes % 60;
    if (now_min < sr_local) {
        int until_sr = sr_local - now_min;
        snprintf(buf, n, "%dh %dm daylight  |  Sunrise in %dh %dm",
                 daylight_h, daylight_m, until_sr / 60, until_sr % 60);
    } else if (now_min < ss_local) {
        int remaining = ss_local - now_min;
        snprintf(buf, n, "%dh %dm daylight  |  %dh %dm remaining",
                 daylight_h, daylight_m, remaining / 60, remaining % 60);
    } else {
        snprintf(buf, n, "%dh %dm daylight  |  After sunset",
                 daylight_h, daylight_m);
    }
}

/**
 * @brief Format the aurora line from Kp and probability ("Kp data unavailable" when Kp is 0).
 * @version 1
 * @req REQ-ASTRO-001
 */
static void astro_fmt_aurora(char *buf, size_t n, float kp, float aurora_prob)
{
    if (!(kp > 0.0f)) {  // also catches NaN, as the original `kp > 0` test did
        snprintf(buf, n, "Aurora: Kp data unavailable");
        return;
    }
    const char *level = aurora_prob >= 0.7f ? "HIGH" :
                        aurora_prob >= 0.4f ? "MODERATE" :
                        aurora_prob >= 0.1f ? "LOW" : "UNLIKELY";
    snprintf(buf, n, "Aurora: Kp=%.1f  %s (%.0f%%)", kp, level, aurora_prob * 100);
}

/**
 * @brief Enter the astronomy state: minimize the clock and build a panel with
 *        today's moon phase, illumination, days to full/new moon, a brightness-
 *        scaled moon disc, local sunrise/sunset with daylight remaining, and an
 *        aurora line from the cached Kp ("Kp data unavailable" when Kp is 0).
 * v2: panel, moon section and line formatting split into helpers (knots v2
 *     cognitive limit); output unchanged.
 * @version 2
 * @req REQ-ASTRO-001
 * @req REQ-DISP-001
 */
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
    int sr_local = info_panel_wrap_day_minutes(sun.sunrise_hour * 60 + sun.sunrise_min + tz_offset_min);
    int ss_local = info_panel_wrap_day_minutes(sun.sunset_hour * 60 + sun.sunset_min + tz_offset_min);

    // OKLCH text contrast over the panel, from the single NVS clock text colour.
    // (Semantic accents below — sunrise gold, aurora violet — are kept on purpose.)
    lv_color_t panel = lv_color_hex(0x0a0a1e);
    lv_color_t base = clock_widget_user_color(get_clock());
    lv_color_t primary = ui_legible(base, panel);
    lv_color_t secondary = lv_color_mix(panel, primary, 110);

    s_container = info_panel_create(s_screen);
    astro_build_moon(s_container, &moon, primary, secondary);

    char buf[128];
    astro_fmt_sun(buf, sizeof(buf), sr_local, ss_local);
    lv_label_set_text(info_panel_label(s_container, &lv_font_montserrat_24, lv_color_hex(0xE9C46A), 270), buf);

    int now_min = ti.tm_hour * 60 + ti.tm_min;
    astro_fmt_daylight(buf, sizeof(buf), now_min, sr_local, ss_local, sun.daylight_minutes);
    lv_label_set_text(info_panel_label(s_container, &lv_font_montserrat_20, secondary, 300), buf);

    float kp = nws_get_kp_index();
    astro_fmt_aurora(buf, sizeof(buf), kp, astro_calc_aurora_probability(kp, cfg.latitude));
    lv_label_set_text(info_panel_label(s_container, &lv_font_montserrat_20, lv_color_hex(0xa490c2), 340), buf);

    fade_in(s_container);

    ESP_LOGI(TAG, "Astronomy: entry (moon=%s %.0f%%, sr=%d:%02d, ss=%d:%02d)",
             moon.phase_name, moon.illumination_pct,
             sr_local / 60, sr_local % 60, ss_local / 60, ss_local % 60);
}

/**
 * @brief Leave the astronomy state: stop animations and delete the panel.
 * @version 1
 * @req REQ-DISP-001
 */
void Astronomy::exit()
{
    if (s_container) { lv_anim_delete(s_container, NULL); lv_obj_delete(s_container); s_container = NULL; }
    ESP_LOGI(TAG, "Astronomy: exit");
}

/**
 * @brief Display timeout: return to the full clock.
 * @version 1
 * @req REQ-DISP-002
 */
void Astronomy::react(EvDisplayTimeout const &)
{
    transit<ClockFull>();
}
