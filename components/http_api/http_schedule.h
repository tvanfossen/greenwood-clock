/**
 * @file http_schedule.h
 * @version 1
 * @brief GET/POST /api/schedule: read and replace the daily/events schedule (sched_json.h
 *        format). A POST that would make two profiles of a kind overlap is refused with 409
 *        and the reason.
 */
#ifndef HTTP_SCHEDULE_H
#define HTTP_SCHEDULE_H

#include "esp_http_server.h"

/**
 * @brief Register the schedule endpoints (before any wildcard handler).
 * @version 1
 * @req REQ-BG-004
 */
void http_schedule_register(httpd_handle_t srv);

#endif /* HTTP_SCHEDULE_H */
