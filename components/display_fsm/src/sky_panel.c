/**
 * @file sky_panel.c
 * v2: the moon drawn natively (sky_body) at its real phase, with its maria and craters.
 * v3: only the pieces the forecast's sky dial uses: the card and the moon (the astronomy
 *     screen they came from is gone: the dial shows the sky).
 * @version 3
 * @brief The sky's shared pieces (see sky_panel.h): a sky card and the moon as it looks.
 *        Every colour is a theme role.
 */
#include "sky_panel.h"
#include "ui_theme_bind.h"
#include "sky_body.h"

#define PAD           16                /* inside a sky card */
#define MOON_DARK_OPA 70                /* the dark side's earthshine at night */

/**
 * @brief A sky card (see sky_panel.h).
 * @version 1
 * @req REQ-ASTRO-001
 * @return The card.
 */
lv_obj_t *sky_card_create(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h)
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
 * @brief The moon as it looks (see sky_panel.h).
 * v2: the native moon (sky_body): its real phase, maria and craters; dark side by night only.
 * @version 2
 * @req REQ-ASTRO-001
 * @return The disc, or NULL when out of memory.
 */
lv_obj_t *sky_moon_create(lv_obj_t *parent, const sky_data_t *d, int32_t diameter, int32_t x,
                          int32_t y)
{
    // By day only its lit part shows, as in the sky; at night a dim earthshine on the rest.
    lv_obj_t *m = sky_body_moon_create(parent, diameter, d->moon_frac,
                                       d->part == SKY_DAY ? LV_OPA_TRANSP : MOON_DARK_OPA);
    if (m) lv_obj_set_pos(m, x, y);
    return m;
}
