/**
 * @file settings_http.h
 * @version 1
 * @brief The phone panel's settings backend over the clock's HTTP API (see settings_http.c).
 */
#pragma once

#include "settings_backend.h"
#include "sched_json.h"

/**
 * @brief The backend over the state GET /api/panel returned, sending changes with @p token.
 * @version 1
 * @req REQ-UI-005
 * @return It, or NULL when the state is not usable.
 */
const settings_backend_t *settings_http_backend(const char *state_json, const char *token);
