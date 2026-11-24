/**
 * @file http_theme.h
 * @version 1
 * @brief GET /api/themes (installed theme files) and GET /api/theme (the current theme's role
 *        colours, for the web panel's CSS variables).
 */
#ifndef HTTP_THEME_H
#define HTTP_THEME_H

#include "esp_http_server.h"

/**
 * @brief Register the theme endpoints (before any wildcard handler).
 * @version 1
 * @req REQ-UI-003
 */
void http_theme_register(httpd_handle_t srv);

#endif /* HTTP_THEME_H */
