/**
 * @file http_frame_capture.h
 * @version 1
 * @brief POST/GET /api/debug/capture: arm the frame recorder and download its frames.
 */
#ifndef HTTP_FRAME_CAPTURE_H
#define HTTP_FRAME_CAPTURE_H

#include "esp_http_server.h"

/**
 * @brief Register the frame-capture endpoints (before any wildcard handler).
 * @version 1
 * @req REQ-API-003
 */
void http_frame_capture_register(httpd_handle_t srv);

#endif /* HTTP_FRAME_CAPTURE_H */
