/**
 * @file upload_view.c
 * @version 1
 * @brief Upload screen (prototype): the pairing QR code, or the files a paired phone sent
 *        (see upload_view.h). Every colour is a theme role.
 */
#include "upload_view.h"
#include "settings_frame.h"
#include "ui_theme_bind.h"

#include <stdio.h>
#include <string.h>

#define FRAME_PAD 20
#define TITLE_ROW 64
#define CONTENT_W (SETTINGS_FRAME_W - 2 * FRAME_PAD - 2)
#define QR_SIZE   300
#define TEXT_X    (QR_SIZE + 32)
#define ITEM_H    64

/**
 * @brief Format @p secs as "9:42".
 * @version 1
 * @utility
 */
static void fmt_mmss(int secs, char *out, size_t n)
{
    snprintf(out, n, "%d:%02d", secs / 60, secs % 60);
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
 * @brief A column of lines to the right of the QR code, from @p y down.
 * @version 1
 * @utility
 * @return The column (flex, top to bottom).
 */
static lv_obj_t *side_column(lv_obj_t *frame, int32_t x, int32_t y)
{
    lv_obj_t *col = lv_obj_create(frame);
    lv_obj_remove_style_all(col);
    lv_obj_set_size(col, CONTENT_W - x, LV_SIZE_CONTENT);
    lv_obj_set_pos(col, x, y);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(col, 10, 0);
    return col;
}

/**
 * @brief Waiting for a phone: the QR code, three steps, the address and the code's expiry.
 * @version 1
 * @utility
 */
static void waiting(lv_obj_t *frame, const char *url, int secs_left)
{
    char addr[96], line[64], t[16];
    qr_code(frame, url);
    snprintf(addr, sizeof(addr), "%s", url);
    char *hash = strchr(addr, '#');   // the pairing code is not shown, only scanned
    if (hash) *hash = '\0';
    fmt_mmss(secs_left, t, sizeof(t));
    snprintf(line, sizeof(line), "Code expires in %s", t);

    lv_obj_t *col = side_column(frame, TEXT_X, TITLE_ROW + 8);
    settings_text(col, "Scan with your phone", &lv_font_montserrat_32, UI_ROLE_TEXT);
    settings_text(col, "1   Scan the code with the camera", &lv_font_montserrat_20,
                  UI_ROLE_TEXT_MUTED);
    settings_text(col, "2   Pick a background, an album\n     photo or a clock animation",
                  &lv_font_montserrat_20, UI_ROLE_TEXT_MUTED);
    settings_text(col, "3   Send it: it shows up here", &lv_font_montserrat_20,
                  UI_ROLE_TEXT_MUTED);
    settings_text(col, line, &lv_font_montserrat_20, UI_ROLE_ACCENT);
    settings_text(col, "One phone, uploads only, for 30 minutes.\nClosing this screen ends it.",
                  &lv_font_montserrat_16, UI_ROLE_TEXT_FAINT);
    settings_text(col, addr, &lv_font_montserrat_16, UI_ROLE_TEXT_FAINT);
}

/**
 * @brief One received file: its name, kind and detail; a Place button for an animation.
 * @version 1
 * @utility
 */
static void item_row(lv_obj_t *col, const upload_item_t *it)
{
    char sub[96];
    snprintf(sub, sizeof(sub), "%s, %s", it->kind, it->detail);
    lv_obj_t *row = lv_obj_create(col);
    lv_obj_set_size(row, CONTENT_W - 12, ITEM_H);
    lv_obj_set_style_radius(row, 10, 0);
    lv_obj_set_style_pad_all(row, 8, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    ui_bind_fill(row, UI_SLOT_CONTROL);
    settings_text(row, it->name, &lv_font_montserrat_20, UI_ROLE_TEXT);
    lv_obj_t *s = settings_text(row, sub, &lv_font_montserrat_16, UI_ROLE_TEXT_MUTED);
    lv_obj_set_pos(s, 0, 26);
    if (strcmp(it->kind, "Clock animation") == 0) {
        lv_obj_t *b = lv_button_create(row);
        lv_obj_set_size(b, 140, ITEM_H - 16);
        lv_obj_align(b, LV_ALIGN_RIGHT_MID, 0, 0);
        ui_bind_fill_role(b, UI_ROLE_ACCENT);
        lv_obj_center(settings_text(b, "Place  " LV_SYMBOL_RIGHT, &lv_font_montserrat_20,
                                    UI_ROLE_TEXT));
    }
}

/**
 * @brief A phone is paired: the session's time left and the files received so far.
 * @version 1
 * @utility
 */
static void connected(lv_obj_t *frame, int secs_left, const upload_item_t *items, int n)
{
    char line[64], t[16];
    fmt_mmss(secs_left, t, sizeof(t));
    snprintf(line, sizeof(line), LV_SYMBOL_OK "  Phone connected: session ends in %s", t);
    lv_obj_t *col = side_column(frame, 0, TITLE_ROW);
    settings_text(col, line, &lv_font_montserrat_24, UI_ROLE_ACCENT);
    settings_text(col, n ? "Received" : "Nothing received yet", &lv_font_montserrat_20,
                  UI_ROLE_TEXT_MUTED);
    for (int i = 0; i < n; i++) item_row(col, &items[i]);
}

/**
 * @brief The upload screen.
 * @version 1
 * @req REQ-UI-005
 * @return The frame.
 */
lv_obj_t *upload_view_create(lv_obj_t *parent, const char *url, int secs_left,
                             const upload_item_t *items, int n, lv_event_cb_t back_cb)
{
    lv_obj_t *frame = settings_frame_create(parent, "Upload", back_cb);
    if (items) connected(frame, secs_left, items, n);
    else waiting(frame, url, secs_left);
    return frame;
}
