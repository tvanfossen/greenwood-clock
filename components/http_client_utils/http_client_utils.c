// components/http_client_utils/http_client_utils.c
//
// Reusable HTTP client utilities extracted from weather.c.
// All functions that are internal to this module are file-static.
// Only http_request_execute() is exported via the header.

#include "http_client_utils.h"
#include "esp_log.h"
#include <stdlib.h>
#include <string.h>

static const char *TAG = "http_utils";

// ─── Status evaluation ────────────────────────────────────────────────────────

/**
 * @brief Map an HTTP status code to a retry disposition.
 *
 * - 200 → OK
 * - 429 → RATE_LIMIT (caller should apply a long back-off)
 * - 4xx → ABORT (client error; retrying will not help)
 * - other non-200 → RETRY
 *
 * @param status  HTTP response status code.
 * @return Retry disposition.
 */
static http_attempt_result_t evaluate_http_status(int status)
{
    http_attempt_result_t result = HTTP_ATTEMPT_RETRY;
    if      (status == 200) result = HTTP_ATTEMPT_OK;
    else if (status == 429) result = HTTP_ATTEMPT_RATE_LIMIT;
    else if (status >= 400) result = HTTP_ATTEMPT_ABORT;
    else
        ESP_LOGW(TAG, "Unexpected HTTP status %d", status);
    return result;
}

// ─── Buffer management ────────────────────────────────────────────────────────

/**
 * @brief Determine initial buffer capacity from a Content-Length hint.
 *
 * Clamps the hint to 32 KB to avoid over-committing heap on adversarial
 * or malformed responses.  Falls back to 4 KB when the hint is absent or
 * out of range.
 *
 * @param hint_len  Content-Length value; -1 if absent.
 * @return Initial buffer capacity in bytes.
 */
static size_t http_initial_cap(int hint_len)
{
    bool valid = (hint_len > 0) && ((size_t)hint_len < 32 * 1024);
    return valid ? (size_t)hint_len + 1 : 4096;
}

/**
 * @brief Attempt to double a heap buffer's capacity.
 *
 * On failure the original pointer is unchanged and NULL is returned.
 * The caller should stop reading and use whatever data was collected.
 *
 * @param buf  Current heap buffer.
 * @param cap  Current capacity; updated on success.
 * @return New pointer on success, NULL on realloc failure.
 */
static char *http_buf_grow(char *buf, size_t *cap)
{
    size_t newcap = *cap * 2;
    char  *grown  = realloc(buf, newcap);
    if (!grown) {
        ESP_LOGE(TAG, "http_buf_grow: realloc to %zu bytes failed (free=%lu)",
                 newcap, (unsigned long)esp_get_free_heap_size());
        return NULL;
    }
    *cap = newcap;
    return grown;
}

/**
 * @brief Append a read chunk to the response buffer, growing it if needed.
 *
 * Advances *total by chunk bytes. If near capacity, doubles the buffer.
 * Returns false if grow fails; caller should break the read loop.
 *
 * @param buf    Pointer to the buffer pointer (may be updated on grow).
 * @param cap    Buffer capacity (updated on grow).
 * @param total  Running byte count (incremented by chunk).
 * @param chunk  Number of bytes just read.
 * @return true if the buffer is still usable; false if grow failed.
 */
static bool http_buf_append(char **buf, size_t *cap, size_t *total, size_t chunk)
{
    *total += chunk;
    if (*total < *cap - 1) return true;
    char *grown = http_buf_grow(*buf, cap);
    if (!grown) return false;
    *buf = grown;
    return true;
}

// ─── Read / open helpers ──────────────────────────────────────────────────────

/**
 * @brief Read all remaining body bytes from an open HTTP client into heap.
 *
 * The returned buffer is null-terminated regardless of content type.
 * Caller must free the returned pointer.
 *
 * @param client    Open client handle (headers already fetched).
 * @param hint_len  Content-Length hint; -1 if unknown.
 * @param out_len   Set to bytes read on return.
 * @return Heap buffer, or NULL on allocation failure.
 */
static char *http_read_all(esp_http_client_handle_t client, int hint_len, size_t *out_len)
{
    size_t cap   = http_initial_cap(hint_len);
    size_t total = 0;
    char  *buf   = malloc(cap);
    if (!buf) {
        ESP_LOGE(TAG, "http_read_all: malloc(%zu) failed (free=%lu)",
                 cap, (unsigned long)esp_get_free_heap_size());
        return NULL;
    }
    int r;
    while ((r = esp_http_client_read(client, buf + total, (int)(cap - total - 1))) > 0) {
        if (!http_buf_append(&buf, &cap, &total, (size_t)r)) break;
    }
    buf[total] = '\0';
    *out_len   = total;
    return buf;
}

/**
 * @brief Open connection, fetch headers, and evaluate the HTTP status.
 *
 * If the open fails, *result is set to HTTP_ATTEMPT_RETRY and ESP_FAIL is
 * returned.  The caller must close/cleanup the client only when ESP_OK is
 * returned.
 *
 * @param client    Initialised client handle.
 * @param out_clen  Set to Content-Length from response headers.
 * @param result    Set to retry disposition.
 * @return ESP_OK on successful open; ESP_FAIL otherwise.
 */
static esp_err_t http_open_and_evaluate(esp_http_client_handle_t client,
                                         int *out_clen,
                                         http_attempt_result_t *result)
{
    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "http_open_and_evaluate: open failed: %s", esp_err_to_name(err));
        *result = HTTP_ATTEMPT_RETRY;
        return ESP_FAIL;
    }
    *out_clen = esp_http_client_fetch_headers(client);
    int status = esp_http_client_get_status_code(client);
    *result = evaluate_http_status(status);
    ESP_LOGI(TAG, "HTTP %d Content-Length %d result=%d", status, *out_clen, (int)*result);
    return ESP_OK;
}

/**
 * @brief Open, conditionally read body, then close the client.
 *
 * On HTTP_ATTEMPT_OK the caller receives a heap buffer.
 * The caller is still responsible for esp_http_client_cleanup().
 *
 * @param client   Initialised client handle.
 * @param result   Set to attempt outcome.
 * @param out_len  Set to body bytes on HTTP_ATTEMPT_OK.
 * @return Heap buffer on HTTP_ATTEMPT_OK, NULL otherwise.
 */
static char *http_open_read_close(esp_http_client_handle_t client,
                                   http_attempt_result_t *result,
                                   size_t *out_len)
{
    int   clen   = 0;
    char *buf    = NULL;
    bool  opened = (http_open_and_evaluate(client, &clen, result) == ESP_OK);

    if (opened && *result == HTTP_ATTEMPT_OK) {
        buf = http_read_all(client, clen, out_len);
        if (!buf) {
            *result = HTTP_ATTEMPT_ABORT;
        } else {
            ESP_LOGI(TAG, "Read %zu bytes", *out_len);
        }
    }
    if (opened) esp_http_client_close(client);
    return buf;
}

// ─── Public API ───────────────────────────────────────────────────────────────

char *http_request_execute(const esp_http_client_config_t *cfg,
                            http_attempt_result_t *result,
                            size_t *out_len)
{
    esp_http_client_handle_t client = esp_http_client_init(cfg);
    if (!client) {
        ESP_LOGE(TAG, "http_request_execute: esp_http_client_init failed");
        *result = HTTP_ATTEMPT_RETRY;
        return NULL;
    }
    char *buf = http_open_read_close(client, result, out_len);
    esp_http_client_cleanup(client);
    return buf;
}
