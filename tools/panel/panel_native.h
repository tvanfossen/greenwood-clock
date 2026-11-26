/**
 * @file panel_native.h
 * @version 1
 * @brief The phone panel's native helpers: the theme as CSS variables (the page's overlays
 *        draw in it), the phone's own keyboard for text, and the real clock as a guide over a
 *        picture being cropped (panel_ui.js asks for it).
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Publish the current theme's roles to the page as CSS variables (--screen, --surface,
 *        --raised, --inset, --border, --text, --muted, --faint, --accent).
 * @version 1
 * @utility
 */
void panel_native_theme_css(void);

/**
 * @brief A settings_backend_t edit_text: the phone's keyboard in a page overlay.
 * @version 1
 * @utility
 */
void panel_native_edit_text(const char *title, const char *initial, int max,
                            void (*done)(const char *text, void *ctx), void *ctx);

#ifdef __cplusplus
}
#endif
