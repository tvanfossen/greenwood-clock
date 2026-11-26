/**
 * @file settings_http.h
 * v2: settings_http_request, the Library.
 * @version 2
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

/** A request's status (0: not sent) and raw answer. */
typedef void (*settings_http_raw_cb_t)(int status, const char *data, size_t len, void *ctx);

/**
 * @brief Send @p method @p path (with JSON @p body, or none) and the session token.
 * @version 1
 * @req REQ-UI-005
 */
void settings_http_request(const char *method, const char *path, const char *body,
                           settings_http_raw_cb_t cb, void *ctx);

/**
 * @brief Why a request failed, for a person, into @p out.
 * @version 1
 * @req REQ-UI-005
 */
void settings_http_failure(int status, const char *data, size_t len, char *out, size_t n);

/**
 * @brief The cached schedule (the Library's rename follows into it).
 * @version 1
 * @req REQ-LIB-001
 * @return It; its length in @p n.
 */
sched_rule_t *settings_http_rules(int *n);

/**
 * @brief Fill @p be's Library calls with the phone's (settings_http_library.c).
 * @version 1
 * @req REQ-LIB-001
 */
void settings_http_library_fill(settings_backend_t *be);
