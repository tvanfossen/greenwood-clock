/**
 * @file sky_panel.c
 * @version 1
 * @brief Sky screen cards (see sky_panel.h): the sun card (day arc, daylight left, golden
 *        hour, aurora, next events) and the moon card (drawn phase, illumination, days to
 *        full and new). Every colour is a theme role.
 */
#include "sky_panel.h"
#include "display_widgets.h"   // ALERT_BANNER_HEIGHT
#include "ui_theme_bind.h"

#include <math.h>
#include <stdio.h>

#define SUN_X       16
#define SUN_Y       (ALERT_BANNER_HEIGHT + 16)
#define SUN_W       556
#define SUN_H       470
#define MOON_X      588
#define MOON_Y      250
#define MOON_W      420
#define MOON_H      (SUN_Y + SUN_H - MOON_Y)
#define PAD         16
#define ARC_D       380                 /* day arc diameter */
#define ARC_W       12                  /* day arc stroke */
#define ROW_Y       262                 /* first detail row, below the arc */
#define ROW_STEP    35
#define VALUE_X     150
#define DISC_D      150                 /* moon disc diameter */
#define GOLDEN_MIN  60                  /* golden hour: ~1 h after sunrise, before sunset */

LV_FONT_DECLARE(nunito_48);

/**
 * @brief Format local minute-of-day @p m as "7:03 AM".
 * @version 1
 * @utility
 */
static void fmt_clock(int m, char *out, size_t n)
{
    int h = m / 60;
    snprintf(out, n, "%d:%02d %s", h % 12 ? h % 12 : 12, m % 60, h < 12 ? "AM" : "PM");
}

/**
 * @brief Format a span of @p m minutes as "2h 13m" (or "13m").
 * @version 1
 * @utility
 */
static void fmt_span(int m, char *out, size_t n)
{
    if (m >= 60) snprintf(out, n, "%dh %dm", m / 60, m % 60);
    else snprintf(out, n, "%dm", m);
}

/**
 * @brief Format a day count as "today", "tomorrow" or "in 5 days".
 * @version 1
 * @utility
 */
static void fmt_days(int days, char *out, size_t n)
{
    if (days <= 0) snprintf(out, n, "today");
    else if (days == 1) snprintf(out, n, "tomorrow");
    else snprintf(out, n, "in %d days", days);
}

/**
 * @brief A card: the card slot's fill at 70 % over the picture, rounded, not scrolling.
 * @version 1
 * @utility
 * @return The card.
 */
static lv_obj_t *card(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h)
{
    lv_obj_t *c = lv_obj_create(parent);
    lv_obj_set_pos(c, x, y);
    lv_obj_set_size(c, w, h);
    ui_bind_fill(c, UI_SLOT_CARD);
    lv_obj_set_style_bg_opa(c, LV_OPA_70, 0);
    lv_obj_set_style_radius(c, 12, 0);
    lv_obj_set_style_border_width(c, 0, 0);
    lv_obj_set_style_pad_all(c, PAD, 0);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    return c;
}

/**
 * @brief A bound label with @p text at (@p x, @p y) in @p parent.
 * @version 1
 * @utility
 * @return The label.
 */
static lv_obj_t *text_at(lv_obj_t *parent, const lv_font_t *font, ui_role_t role, int32_t x,
                         int32_t y, const char *text)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_label_set_text(l, text);
    lv_obj_set_pos(l, x, y);
    ui_bind_text(l, role);
    return l;
}

/**
 * @brief A centred bound label @p w wide whose centre is at @p cx.
 * @version 1
 * @utility
 * @return The label.
 */
static lv_obj_t *text_centered(lv_obj_t *parent, const lv_font_t *font, ui_role_t role,
                               int32_t cx, int32_t y, int32_t w, const char *text)
{
    lv_obj_t *l = text_at(parent, font, role, cx - w / 2, y, text);
    lv_obj_set_width(l, w);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    return l;
}

/**
 * @brief The day arc: sunrise on the left, sunset on the right, the sun (the knob) where it
 *        is now; at night the arc is empty and has no sun.
 * @version 1
 * @utility
 */
static void day_arc(lv_obj_t *parent, const sky_data_t *d)
{
    lv_obj_t *arc = lv_arc_create(parent);
    lv_obj_set_size(arc, ARC_D, ARC_D);
    lv_obj_align(arc, LV_ALIGN_TOP_MID, 0, 0);
    lv_arc_set_bg_angles(arc, 180, 360);
    lv_arc_set_range(arc, 0, d->daylight_min > 0 ? d->daylight_min : 1);
    lv_arc_set_value(arc, d->part == SKY_DAY ? d->now_min - d->sunrise_min : 0);
    lv_obj_set_style_arc_width(arc, ARC_W, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, ARC_W, LV_PART_INDICATOR);
    lv_obj_set_style_pad_all(arc, 6, LV_PART_KNOB);
    lv_obj_set_flag(arc, LV_OBJ_FLAG_CLICKABLE, false);
    if (d->part != SKY_DAY) lv_obj_set_style_opa(arc, LV_OPA_TRANSP, LV_PART_KNOB);
}

/**
 * @brief The arc's centre (time to the next turn of the day) and its end labels.
 * @version 1
 * @utility
 */
static void arc_labels(lv_obj_t *parent, const sky_data_t *d)
{
    const int32_t mid = (SUN_W - 2 * PAD) / 2;
    const int32_t end = ARC_D / 2 - ARC_W / 2;   // arc end x from the middle
    char big[24], buf[24];
    const char *small = "polar night";
    snprintf(big, sizeof(big), "No sunrise");
    if (d->part == SKY_DAY) {
        fmt_span(d->sunset_min - d->now_min, big, sizeof(big));
        small = "of daylight left";
    } else if (d->sunrise_min >= 0) {
        int to = d->sunrise_min - d->now_min;
        fmt_span(to < 0 ? to + 1440 : to, big, sizeof(big));
        small = "until sunrise";
    }
    text_centered(parent, &nunito_48, UI_ROLE_TEXT, mid, 96, 340, big);
    text_centered(parent, &lv_font_montserrat_20, UI_ROLE_TEXT_MUTED, mid, 152, 340, small);
    if (d->sunrise_min < 0) return;
    fmt_clock(d->sunrise_min, buf, sizeof(buf));
    text_centered(parent, &lv_font_montserrat_20, UI_ROLE_TEXT, mid - end, 200, 140, buf);
    text_centered(parent, &lv_font_montserrat_16, UI_ROLE_TEXT_FAINT, mid - end, 226, 140,
                  "Sunrise");
    fmt_clock(d->sunset_min, buf, sizeof(buf));
    text_centered(parent, &lv_font_montserrat_20, UI_ROLE_TEXT, mid + end, 200, 140, buf);
    text_centered(parent, &lv_font_montserrat_16, UI_ROLE_TEXT_FAINT, mid + end, 226, 140,
                  "Sunset");
}

/**
 * @brief One detail row: a faint key and its value.
 * @version 1
 * @utility
 */
static void row(lv_obj_t *parent, int i, const char *key, const char *value)
{
    int32_t y = ROW_Y + i * ROW_STEP;
    text_at(parent, &lv_font_montserrat_20, UI_ROLE_TEXT_FAINT, 0, y, key);
    text_at(parent, &lv_font_montserrat_20, UI_ROLE_TEXT, VALUE_X, y, value);
}

/**
 * @brief The next golden hour: the morning one until it ends, then the evening one.
 * @version 1
 * @utility
 */
static void fmt_golden(const sky_data_t *d, char *out, size_t n)
{
    int from = d->sunset_min - GOLDEN_MIN, to = d->sunset_min;
    if (d->now_min < d->sunrise_min + GOLDEN_MIN || d->part == SKY_NIGHT_AFTER) {
        from = d->sunrise_min;
        to = d->sunrise_min + GOLDEN_MIN;
    }
    char a[16], b[16];
    fmt_clock(from, a, sizeof(a));
    fmt_clock(to, b, sizeof(b));
    snprintf(out, n, "%s - %s%s", a, b, d->part == SKY_NIGHT_AFTER ? " tomorrow" : "");
}

/**
 * @brief The detail rows: golden hour, daylight, aurora, next season change and shower.
 * @version 1
 * @utility
 */
static void detail_rows(lv_obj_t *parent, const sky_data_t *d)
{
    static const char *const LEVEL[] = { "unlikely", "low", "moderate", "high" };
    char buf[64], span[16];
    int i = 0;
    if (d->sunrise_min >= 0) {
        fmt_golden(d, buf, sizeof(buf));
        row(parent, i++, "Golden hour", buf);
    }
    fmt_span(d->daylight_min, span, sizeof(span));
    snprintf(buf, sizeof(buf), "%s today", span);
    row(parent, i++, "Daylight", buf);
    int lv = (d->aurora_prob >= 0.1f) + (d->aurora_prob >= 0.4f) + (d->aurora_prob >= 0.7f);
    if (d->kp > 0.0f) snprintf(buf, sizeof(buf), "Kp %.1f, %s", (double)d->kp, LEVEL[lv]);
    else snprintf(buf, sizeof(buf), "no Kp data");
    row(parent, i++, "Aurora", buf);
    fmt_days(d->season_days, span, sizeof(span));
    snprintf(buf, sizeof(buf), "%s %s", d->season, span);
    row(parent, i++, "Season", buf);
    fmt_days(d->shower_days, span, sizeof(span));
    snprintf(buf, sizeof(buf), "%s %s", d->shower, span);
    row(parent, i, "Meteors", buf);
}

/**
 * @brief Free the moon canvas's buffer with it.
 * @version 1
 * @callback
 */
static void free_buf_cb(lv_event_t *e)
{
    lv_draw_buf_destroy((lv_draw_buf_t *)lv_event_get_user_data(e));
}

/**
 * @brief The colour of moon-disc pixel (@p dx, @p dy) from the centre: the lit side or the
 *        shadow across the terminator (soft over a pixel), and its coverage at the rim.
 * @version 1
 * @utility
 * @return The coverage (0 outside the disc); @p out is set when it is above 0.
 */
static float disc_px(float dx, float dy, float r, float term, float side, lv_color_t lit,
                     lv_color_t dark, lv_color_t *out)
{
    float edge = r - sqrtf(dx * dx + dy * dy) + 0.5f;
    if (edge <= 0.0f) return 0.0f;
    // The terminator's half-width, a pixel wider than the disc: at full and new it lies just
    // outside the rim, so the rim never takes the soft terminator's half-tone.
    float w = sqrtf(fmaxf(r * r - dy * dy, 0.0f)) + 1.0f;
    float t = fminf(fmaxf(side * dx - term * w + 0.5f, 0.0f), 1.0f);   // 1 = lit
    *out = lv_color_mix(lit, dark, (uint8_t)(t * 255.0f));
    return fminf(edge, 1.0f);
}

/**
 * @brief The moon drawn as it looks tonight: lit by @p illum_pct, from the right while
 *        @p waxing (northern hemisphere), the shadowed part in a deep tone.
 * @version 1
 * @utility
 */
static void moon_disc(lv_obj_t *parent, float illum_pct, bool waxing, int32_t x, int32_t y)
{
    lv_draw_buf_t *buf = lv_draw_buf_create(DISC_D, DISC_D, LV_COLOR_FORMAT_ARGB8888, 0);
    if (!buf) return;
    lv_obj_t *cv = lv_canvas_create(parent);
    lv_canvas_set_draw_buf(cv, buf);
    lv_obj_add_event_cb(cv, free_buf_cb, LV_EVENT_DELETE, buf);
    lv_canvas_fill_bg(cv, ui_theme_color(UI_ROLE_SCREEN), LV_OPA_TRANSP);
    lv_obj_set_pos(cv, x, y);
    lv_obj_set_style_radius(cv, LV_RADIUS_CIRCLE, 0);   // a rim, so a lit disc shows on a
    lv_obj_set_style_border_width(cv, 2, 0);             // light card
    ui_bind_border(cv, UI_ROLE_BORDER);
    // Terminator at x = term * w(y): +1 = none lit (new), -1 = all lit (full).
    const float r = DISC_D / 2.0f - 1.0f, term = 1.0f - 2.0f * illum_pct / 100.0f;
    const lv_color_t lit = ui_theme_color(UI_ROLE_LIGHT), dark = ui_theme_tone(0.3f);
    for (int py = 0; py < DISC_D; py++) {
        for (int px = 0; px < DISC_D; px++) {
            lv_color_t c;
            float cover = disc_px(px + 0.5f - DISC_D / 2.0f, py + 0.5f - DISC_D / 2.0f, r, term,
                                  waxing ? 1.0f : -1.0f, lit, dark, &c);
            if (cover > 0.0f) lv_canvas_set_px(cv, px, py, c, (lv_opa_t)(cover * 255.0f));
        }
    }
}

/**
 * @brief The moon card: the disc and its phase, illumination and days to full and new.
 * @version 1
 * @utility
 */
static void moon_card(lv_obj_t *root, const sky_data_t *d)
{
    lv_obj_t *c = card(root, MOON_X, MOON_Y, MOON_W, MOON_H);
    const int32_t inner_h = MOON_H - 2 * PAD;
    moon_disc(c, d->moon.illumination_pct, d->waxing, 0, (inner_h - DISC_D) / 2);
    const int32_t x = DISC_D + 20, y = (inner_h - 150) / 2;
    char buf[48];
    text_at(c, &lv_font_montserrat_24, UI_ROLE_TEXT, x, y, d->moon.phase_name);
    snprintf(buf, sizeof(buf), "%.0f%% lit", (double)d->moon.illumination_pct);
    text_at(c, &lv_font_montserrat_20, UI_ROLE_TEXT_MUTED, x, y + 36, buf);
    char when[16];
    // The phase is the date's (00:00 UTC, mean month): within a day of the named phase, it is
    // that phase today, not "tomorrow".
    bool full_now = d->moon.phase_index == 4 && d->moon.days_to_full <= 1;
    bool new_now = d->moon.phase_index == 0 && d->moon.days_to_new <= 1;
    fmt_days(full_now ? 0 : d->moon.days_to_full, when, sizeof(when));
    snprintf(buf, sizeof(buf), "Full moon %s", when);
    text_at(c, &lv_font_montserrat_20, UI_ROLE_TEXT, x, y + 88, buf);
    fmt_days(new_now ? 0 : d->moon.days_to_new, when, sizeof(when));
    snprintf(buf, sizeof(buf), "New moon %s", when);
    text_at(c, &lv_font_montserrat_20, UI_ROLE_TEXT, x, y + 120, buf);
}

/**
 * @brief Build the sky screen's cards on @p screen from @p d.
 * @version 1
 * @req REQ-ASTRO-001
 * @req REQ-AMB-001
 * @return The holder of both cards.
 */
lv_obj_t *sky_panel_create(lv_obj_t *screen, const sky_data_t *d)
{
    lv_obj_t *root = lv_obj_create(screen);
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, lv_pct(100), lv_pct(100));
    lv_obj_remove_flag(root, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));
    lv_obj_t *sun = card(root, SUN_X, SUN_Y, SUN_W, SUN_H);
    day_arc(sun, d);
    arc_labels(sun, d);
    detail_rows(sun, d);
    moon_card(root, d);
    return root;
}
