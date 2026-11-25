/**
 * @file phone_view.c
 * @version 1
 * @brief The Phone screen (see phone_view.h). Every colour is a theme role.
 */
#include "phone_view.h"
#include "settings_frame.h"
#include "ui_theme_bind.h"

#include <stdio.h>
#include <string.h>

#define FRAME_PAD 20
#define TITLE_ROW 64
#define QR_SIZE   300
#define TEXT_X    (QR_SIZE + 32)

static lv_obj_t *s_expiry;   // the labels phone_view_update refreshes (NULL when gone)
static lv_obj_t *s_phones;

/**
 * @brief The frame is deleted: forget its labels.
 * @version 1
 * @callback
 */
static void deleted_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    s_expiry = s_phones = NULL;
}

/**
 * @brief The QR code of @p url: dark modules in the theme's deep tone on its near-white,
 *        with a quiet zone (scanners need light around it).
 * @version 1
 * @utility
 */
static void qr_code(lv_obj_t *frame, const char *url)
{
    lv_color_t light = ui_theme_color(UI_ROLE_LIGHT);
    lv_obj_t *qr = lv_qrcode_create(frame);
    lv_qrcode_set_size(qr, QR_SIZE);
    lv_qrcode_set_dark_color(qr, ui_theme_shadow_for(light));
    lv_qrcode_set_light_color(qr, light);
    lv_qrcode_set_quiet_zone(qr, true);
    lv_qrcode_update(qr, url, (uint32_t)strlen(url));
    lv_obj_set_pos(qr, 0, TITLE_ROW + 8);
}

/**
 * @brief The Phone screen.
 * v2: widths from the frame area (settings_content_w).
 * @version 2
 * @req REQ-UI-005
 * @return The frame.
 */
lv_obj_t *phone_view_create(lv_obj_t *parent, const char *url, lv_event_cb_t back_cb,
                            lv_event_cb_t disconnect_cb)
{
    char addr[128];
    snprintf(addr, sizeof(addr), "%s", url);
    char *hash = strchr(addr, '#');   // the pairing code is scanned, never shown
    if (hash) *hash = '\0';

    lv_obj_t *frame = settings_frame_create(parent, "Phone", back_cb);
    lv_obj_add_event_cb(frame, deleted_cb, LV_EVENT_DELETE, NULL);
    qr_code(frame, url);
    lv_obj_t *col = lv_obj_create(frame);
    lv_obj_remove_style_all(col);
    lv_obj_set_size(col, settings_content_w() - TEXT_X, LV_SIZE_CONTENT);
    lv_obj_set_pos(col, TEXT_X, TITLE_ROW + 8);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(col, 10, 0);
    settings_text(col, "Control from your phone", &lv_font_montserrat_24, UI_ROLE_TEXT);
    settings_text(col, "1   Scan the code with the camera\n2   Open the link: the clock's\n"
                       "     settings appear on the phone",
                  &lv_font_montserrat_20, UI_ROLE_TEXT_MUTED);
    s_expiry = settings_text(col, "", &lv_font_montserrat_20, UI_ROLE_ACCENT);
    s_phones = settings_text(col, "", &lv_font_montserrat_20, UI_ROLE_TEXT);
    settings_text(col, "The code works once. A phone stays connected\nfor 30 minutes.",
                  &lv_font_montserrat_16, UI_ROLE_TEXT_FAINT);
    settings_text(col, addr, &lv_font_montserrat_16, UI_ROLE_TEXT_FAINT);
    lv_obj_t *b = lv_button_create(col);
    lv_obj_add_event_cb(b, disconnect_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_center(settings_text(b, LV_SYMBOL_CLOSE "  Disconnect phones", &lv_font_montserrat_20,
                                UI_ROLE_TEXT));
    phone_view_update(0, 0);
    return frame;
}

/**
 * @brief Refresh the countdown and the phone count.
 * @version 1
 * @req REQ-UI-005
 */
void phone_view_update(int secs_left, int phones)
{
    if (!s_expiry) return;
    if (secs_left > 0) lv_label_set_text_fmt(s_expiry, "Code expires in %d:%02d", secs_left / 60, secs_left % 60);
    else lv_label_set_text(s_expiry, "Code used or expired: open this screen again");
    if (phones > 0) lv_label_set_text_fmt(s_phones, LV_SYMBOL_OK "  %d phone%s connected", phones, phones == 1 ? "" : "s");
    else lv_label_set_text(s_phones, "No phone connected");
}
