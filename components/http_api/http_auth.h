/**
 * @file http_auth.h
 * @version 1
 * @brief Every URI is registered through http_auth_register, which puts a gate in front of
 *        its handler: the level a request needs comes from the path (auth.h) and a request
 *        without the right bearer token gets 401. Also POST /api/pair, where a phone trades
 *        the QR code's pairing code for a session token.
 *
 *   PUBLIC   /api/pair, static files (/www/...: the panel itself carries no data)
 *   ADMIN    /ota..., /debug/..., /files/..., /api/coredump..., /api/sdcard/...,
 *            /api/debug/..., /api/logs/..., /api/boot_history, /api/disk, /api/screenshot
 *   SESSION  every other /api/... (the panel's settings, schedule, themes, uploads, ...)
 */
#ifndef HTTP_AUTH_H
#define HTTP_AUTH_H

#include "esp_http_server.h"

/**
 * @brief Register @p uri behind the gate for its path's level.
 * @version 1
 * @req REQ-UI-005
 */
void http_auth_register(httpd_handle_t srv, const httpd_uri_t *uri);

/**
 * @brief Register POST /api/pair.
 * @version 1
 * @req REQ-UI-005
 */
void http_auth_register_pairing(httpd_handle_t srv);

#endif /* HTTP_AUTH_H */
