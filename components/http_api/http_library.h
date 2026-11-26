/**
 * @file http_library.h
 * @version 1
 * @brief The Library endpoints for the phone panel (see http_library.c).
 */
#pragma once

#include "esp_http_server.h"

/**
 * @brief Register the Library endpoints (behind the session gate).
 * @version 1
 * @req REQ-LIB-001
 */
void http_library_register(httpd_handle_t srv);
