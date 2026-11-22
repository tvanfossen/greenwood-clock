/**
 * @file http_bg_schedule.h
 * @version 1
 * @brief GET/POST /api/bg_schedule: read and replace the background schedule as JSON.
 */
#ifndef HTTP_BG_SCHEDULE_H
#define HTTP_BG_SCHEDULE_H

#include "esp_http_server.h"

/**
 * @brief Register the background schedule endpoints (before any wildcard handler).
 * @version 1
 * @req REQ-BG-004
 */
void http_bg_schedule_register(httpd_handle_t srv);

#endif /* HTTP_BG_SCHEDULE_H */
