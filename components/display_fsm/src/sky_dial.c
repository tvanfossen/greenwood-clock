/**
 * @file sky_dial.c
 * v2: the native sun and moon (sky_body): the moon at its real place and phase, by day too.
 * @version 2
 * @brief Sky dial (see sky_dial.h): the horizon window with the wheel's arc, the sun and
 *        the moon placed by angle (clipped below the horizon), the moon's phase name under it,
 *        and the next sunrise / sunset in a column beside it. Every colour is a theme role.
 */
#include "sky_dial.h"
#include "sky_panel.h"   // sky_card_create, sky_moon_create
#include "sky_body.h"
#include "ui_theme_bind.h"

#include <math.h>
#include <stdio.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define WIN_W    236     /* the sky window, above the horizon */
#define WIN_H    100
#define WHEEL_R  78      /* the wheel's radius (centre on the horizon, mid window) */
#define SUN_D    52      /* the sun's square: its disc 60 % of it, rays and glow round it */
#define MOON_D   36      /* the moon's square: its disc 92 % of it */
#define COL_X    (WIN_W + 18)

/**
 * @brief Place @p obj (@p size px) at @p deg on the wheel in @p win.
 * @version 1
 * @utility
 */
static void place(lv_obj_t *obj, float deg, int32_t size)
{
    float r = deg * (float)M_PI / 180.0f;
    int32_t cx = WIN_W / 2 + (int32_t)lroundf(WHEEL_R * sinf(r));
    int32_t cy = WIN_H - (int32_t)lroundf(WHEEL_R * cosf(r));
    lv_obj_set_pos(obj, cx - size / 2, cy - size / 2);
}

/**
 * @brief A bound label with @p text at (@p x, @p y).
 * @version 1
 * @utility
 * @return The label.
 */
static lv_obj_t *dial_text(lv_obj_t *parent, const lv_font_t *font, ui_role_t role, int32_t x,
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
 * @brief The sky window: the wheel's upper arc, the horizon, the sun and the moon (each
 *        hidden by the window's clip once below the horizon).
 * v2: the native animated sun (sky_body); the moon wherever it really is.
 * @version 2
 * @utility
 */
static void sky_window(lv_obj_t *card, const sky_data_t *d)
{
    lv_obj_t *win = lv_obj_create(card);
    lv_obj_remove_style_all(win);
    lv_obj_set_size(win, WIN_W, WIN_H);
    lv_obj_remove_flag(win, (lv_obj_flag_t)(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE));

    lv_obj_t *ring = lv_arc_create(win);   // the path: the themed arc track, no fill or knob
    lv_obj_set_size(ring, 2 * WHEEL_R, 2 * WHEEL_R);
    lv_obj_set_pos(ring, WIN_W / 2 - WHEEL_R, WIN_H - WHEEL_R);
    lv_arc_set_bg_angles(ring, 180, 360);
    lv_arc_set_value(ring, lv_arc_get_min_value(ring));
    lv_obj_set_style_arc_width(ring, 2, LV_PART_MAIN);
    lv_obj_set_style_arc_width(ring, 0, LV_PART_INDICATOR);
    lv_obj_set_style_opa(ring, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_remove_flag(ring, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *moon = sky_moon_create(win, d, MOON_D, 0, 0);
    if (moon) place(moon, sky_moon_angle(d), MOON_D);
    lv_obj_t *sun = d->sunrise_min >= 0 ? sky_body_sun_create(win, SUN_D) : NULL;
    if (sun) place(sun, sky_sun_angle(d), SUN_D);

    lv_obj_t *horizon = lv_obj_create(win);
    lv_obj_remove_style_all(horizon);
    lv_obj_set_size(horizon, WIN_W, 2);
    lv_obj_set_pos(horizon, 0, WIN_H - 2);
    lv_obj_set_style_bg_opa(horizon, LV_OPA_COVER, 0);
    ui_bind_fill_role(horizon, UI_ROLE_BORDER);
}

/**
 * @brief Two times in the column beside the window: @p names[0] / @p mins[0] first (the
 *        next to happen), the other under it; a time below 0 shows as a dash.
 * @version 1
 * @utility
 */
static void two_times(lv_obj_t *card, const char *const names[2], const int mins[2])
{
    char buf[16];
    for (int i = 0; i < 2; i++) {
        int32_t y = 2 + i * 60;
        dial_text(card, &lv_font_montserrat_16, UI_ROLE_TEXT_FAINT, COL_X, y, names[i]);
        if (mins[i] >= 0) sky_fmt_clock(mins[i], buf, sizeof(buf));
        else snprintf(buf, sizeof(buf), "-");
        dial_text(card, &lv_font_montserrat_24, i ? UI_ROLE_TEXT_MUTED : UI_ROLE_TEXT, COL_X,
                  y + 20, buf);
    }
}

/**
 * @brief The column beside the window: by day the sun's next set and rise; at night the moon's
 *        next rise and set (whichever comes first on top).
 * v2: the moon's times at night.
 * @version 2
 * @utility
 */
static void sky_times(lv_obj_t *card, const sky_data_t *d)
{
    if (d->part != SKY_DAY) {
        bool set_next = d->moon_up;
        const char *names[2] = { set_next ? "Moonset" : "Moonrise", set_next ? "Moonrise" : "Moonset" };
        int mins[2] = { set_next ? d->moonset_min : d->moonrise_min,
                        set_next ? d->moonrise_min : d->moonset_min };
        two_times(card, names, mins);
        return;
    }
    const char *names[2] = { "Sunset", "Sunrise" };
    int mins[2] = { d->sunset_min, d->sunrise_min };
    two_times(card, names, mins);
}

/**
 * @brief Build the dial card (see sky_dial.h).
 * v2: the moon's times at night (sky_times).
 * @version 2
 * @req REQ-WX-007
 * @return The card.
 */
lv_obj_t *sky_dial_create(lv_obj_t *parent, const sky_data_t *d, int32_t x, int32_t y,
                          int32_t w, int32_t h)
{
    lv_obj_t *card = sky_card_create(parent, x, y, w, h);
    sky_window(card, d);
    lv_obj_t *phase = dial_text(card, &lv_font_montserrat_16, UI_ROLE_TEXT_FAINT, 0, WIN_H + 8,
                                d->moon.phase_name);
    lv_obj_set_width(phase, WIN_W);
    lv_obj_set_style_text_align(phase, LV_TEXT_ALIGN_CENTER, 0);
    sky_times(card, d);
    return card;
}
