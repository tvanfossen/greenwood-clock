/**
 * @file http_json.h
 * @version 1
 * @brief The JSON plumbing the API handlers share: read a request's body, send a cJSON reply,
 *        an {"error": why} object.
 */
#pragma once

#include "esp_http_server.h"
#include "cJSON.h"

#include <stddef.h>

/**
 * @brief The request's whole body (NUL-terminated; free it), or NULL when larger than @p max
 *        or cut short.
 * @version 1
 * @req REQ-UI-005
 * @return The body.
 */
char *http_json_body(httpd_req_t *req, size_t max);

/**
 * @brief Send @p status with JSON @p body (a cJSON item; deleted).
 * @version 1
 * @req REQ-UI-005
 */
void http_json_send(httpd_req_t *req, const char *status, cJSON *body);

/**
 * @brief {"error": @p why}.
 * @version 1
 * @req REQ-UI-005
 * @return The object.
 */
cJSON *http_json_error(const char *why);
