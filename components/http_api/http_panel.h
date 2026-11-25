/**
 * @file http_panel.h
 * @version 1
 * @brief The phone panel's API (a paired session or the admin token):
 *
 *   GET  /api/panel         everything the panel shows at start: the settings, the lists to
 *                           choose from, the schedule, the clock's own "now" and a year of
 *                           sunrise/sunset (so the phone checks overlaps the way the clock
 *                           does without knowing where it is)
 *   POST /api/theme/custom  a theme from the editor: kept as custom.json, made the global theme
 *
 * Changes otherwise go to POST /api/settings and POST /api/schedule.
 */
#ifndef HTTP_PANEL_H
#define HTTP_PANEL_H

#include "esp_http_server.h"

/**
 * @brief Register the panel endpoints (through the gate).
 * @version 1
 * @req REQ-UI-005
 */
void http_panel_register(httpd_handle_t srv);

#endif /* HTTP_PANEL_H */
