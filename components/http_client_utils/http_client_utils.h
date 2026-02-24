// components/http_client_utils/http_client_utils.h
//
// Reusable HTTP client utilities: buffer management, status evaluation,
// and a full-lifecycle GET helper.  Shared by weather, ota, and any future
// components that need to make outbound HTTP requests.

#ifndef HTTP_CLIENT_UTILS_H
#define HTTP_CLIENT_UTILS_H

#include "esp_http_client.h"
#include "esp_err.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Outcome of a single HTTP connection attempt.
 *
 * Returned (or written to output parameter) by every public function.
 * Callers use this to drive retry loops without inspecting raw status codes.
 */
typedef enum {
    HTTP_ATTEMPT_OK,          /**< Request succeeded; response body available. */
    HTTP_ATTEMPT_RETRY,       /**< Transient failure; retry with standard delay. */
    HTTP_ATTEMPT_RATE_LIMIT,  /**< HTTP 429; retry after extended delay. */
    HTTP_ATTEMPT_ABORT,       /**< Client error or OOM; do not retry. */
} http_attempt_result_t;

/**
 * @brief Execute one complete HTTP GET request: init, connect, read, cleanup.
 *
 * Encapsulates the full esp_http_client lifecycle.  On @c HTTP_ATTEMPT_OK the
 * returned buffer must be @c free()'d by the caller.  All other results return
 * NULL and the caller should inspect @p result to decide whether to retry.
 *
 * @param cfg      ESP HTTP client configuration for this request.
 * @param result   Set to the attempt outcome on return.
 * @param out_len  Set to the number of body bytes when @c HTTP_ATTEMPT_OK.
 * @return Null-terminated heap buffer, or NULL.
 */
char *http_request_execute(const esp_http_client_config_t *cfg,
                            http_attempt_result_t *result,
                            size_t *out_len);

#ifdef __cplusplus
}
#endif

#endif // HTTP_CLIENT_UTILS_H
