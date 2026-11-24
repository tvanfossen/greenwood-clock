/**
 * @file ui_theme_bind.h
 * @version 1
 * @brief Bind an object's colours to theme roles once; the theme keeps them painted.
 *
 * A widget says what a colour is for, never what it is:
 *   ui_bind_fill(card, UI_SLOT_CARD);          // bg follows the slot's role
 *   ui_bind_text(label, UI_ROLE_TEXT_MUTED);   // legible against whatever is behind it
 * Text is made legible against the label's real backdrop: the screen colour, or the mean of
 * the background picture under the enclosing panel, with every filled ancestor blended over
 * it at its bg opacity. ui_bind_refresh_all() repaints every bound object after a theme or
 * background change, so screens recolour live. Bindings end with their object.
 * Caller holds the LVGL lock.
 */
#pragma once

#include "lvgl.h"
#include "ui_theme.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Bind @p obj's background colour to @p slot's role (the theme can move the slot).
 * @version 1
 * @req REQ-UI-003
 */
void ui_bind_fill(lv_obj_t *obj, ui_slot_t slot);

/**
 * @brief Bind @p obj's background colour to @p role directly.
 * @version 1
 * @req REQ-UI-003
 */
void ui_bind_fill_role(lv_obj_t *obj, ui_role_t role);

/**
 * @brief Bind @p obj's text colour to @p role, made legible against its real backdrop.
 * @version 1
 * @req REQ-UI-003
 * @req REQ-BG-002
 */
void ui_bind_text(lv_obj_t *obj, ui_role_t role);

/**
 * @brief Bind @p obj's border colour to @p role.
 * @version 1
 * @req REQ-UI-003
 */
void ui_bind_border(lv_obj_t *obj, ui_role_t role);

/**
 * @brief The picture behind the screens (screen-sized, top-down), or NULL: what text
 *        backdrops sample. Repaints bound text when it changes.
 * @version 1
 * @req REQ-UI-003
 * @req REQ-BG-002
 */
void ui_bind_set_picture(const lv_image_dsc_t *picture);

/**
 * @brief The colour really behind @p obj: the screen colour or the picture under its
 *        enclosing panel, with every filled ancestor (and @p obj) blended at its bg opacity.
 * @version 1
 * @req REQ-UI-003
 * @req REQ-BG-002
 * @return The backdrop colour.
 */
lv_color_t ui_bind_backdrop(lv_obj_t *obj);

/**
 * @brief Repaint every bound object from the current theme: fills and borders first, then
 *        text against the new fills.
 * @version 1
 * @req REQ-UI-003
 */
void ui_bind_refresh_all(void);

#ifdef __cplusplus
}
#endif
