/**
 * @file http_auth.h
 * v2: /api/assets/upload is admin-only.
 * v3: HTTP_AUTH_HANDLERS_MAX: the server's handler slots, sized from the gate table.
 * @version 3
 * @brief Every URI is registered through http_auth_register, which puts a gate in front of
 *        its handler: the level a request needs comes from the path (auth.h) and a request
 *        without the right bearer token gets 401. Also POST /api/pair, where a phone trades
 *        the QR code's pairing code for a session token.
 *
 *   PUBLIC   /api/pair, static files (/www/...: the panel itself carries no data)
 *   ADMIN    /ota..., /debug/..., /files/..., /api/coredump..., /api/sdcard/...,
 *            /api/debug/..., /api/logs/..., /api/boot_history, /api/disk, /api/screenshot,
 *            /api/assets/upload (any SD path)
 *   SESSION  every other /api/... (the panel's settings, schedule, themes, uploads, ...)
 */
#ifndef HTTP_AUTH_H
#define HTTP_AUTH_H

#include "esp_http_server.h"

/** How many URIs can be gated. */
#define HTTP_AUTH_GATES_MAX 96

/** The handler slots the HTTP server needs: every gate, plus the ungated POST /api/pair.
 *  The server is configured from this, so the two limits cannot drift apart. */
#define HTTP_AUTH_HANDLERS_MAX (HTTP_AUTH_GATES_MAX + 1)

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
