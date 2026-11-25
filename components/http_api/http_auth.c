/**
 * @file http_auth.c
 * @version 1
 * @brief The gate in front of every handler, and POST /api/pair (see http_auth.h).
 */
#include "http_auth.h"
#include "auth.h"

#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include "esp_log.h"

static const char *TAG = "http_auth";

#define GATES_MAX 96
#define PAIR_BODY_MAX 256

/** One gated handler: what it needs and what it is. */
typedef struct {
    esp_err_t (*handler)(httpd_req_t *req);
    auth_level_t level;
} gate_t;

static gate_t s_gates[GATES_MAX];
static int    s_gate_count;

static const char *const ADMIN_PREFIXES[] = {
    "/ota", "/debug/", "/files/", "/api/coredump", "/api/sdcard/", "/api/debug/",
    "/api/logs/", "/api/boot_history", "/api/disk", "/api/screenshot",
};

/**
 * @brief The level a path needs.
 * @version 1
 * @utility
 * @return It.
 */
static auth_level_t level_of(const char *uri)
{
    auth_level_t level = strncmp(uri, "/api/", 5) == 0 ? AUTH_SESSION : AUTH_PUBLIC;
    for (size_t i = 0; i < sizeof(ADMIN_PREFIXES) / sizeof(ADMIN_PREFIXES[0]); i++) {
        if (strncmp(uri, ADMIN_PREFIXES[i], strlen(ADMIN_PREFIXES[i])) == 0) level = AUTH_ADMIN;
    }
    return level;
}

/**
 * @brief The gate: check the bearer token, then run the handler (or answer 401).
 * @version 1
 * @callback
 * @return The handler's result, or ESP_OK after a 401.
 */
static esp_err_t gate_handler(httpd_req_t *req)
{
    const gate_t *g = (const gate_t *)req->user_ctx;
    char hdr[96];
    const char *bearer = NULL;
    if (httpd_req_get_hdr_value_str(req, "Authorization", hdr, sizeof(hdr)) == ESP_OK &&
        strncmp(hdr, "Bearer ", 7) == 0) {
        bearer = hdr + 7;
    }
    if (auth_allows(g->level, bearer)) return g->handler(req);
    ESP_LOGW(TAG, "401 %s (needs %s)", req->uri, g->level == AUTH_ADMIN ? "admin" : "a session");
    httpd_resp_set_status(req, "401 Unauthorized");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"error\":\"scan the QR code on the clock (Settings > Phone)\"}",
                    HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

/**
 * @brief Register a URI behind the gate.
 * @version 1
 * @req REQ-UI-005
 */
void http_auth_register(httpd_handle_t srv, const httpd_uri_t *uri)
{
    if (s_gate_count >= GATES_MAX) {
        ESP_LOGE(TAG, "too many handlers: %s not registered", uri->uri);
        return;
    }
    gate_t *g = &s_gates[s_gate_count++];
    *g = (gate_t){ uri->handler, level_of(uri->uri) };
    httpd_uri_t gated = *uri;
    gated.handler = gate_handler;
    gated.user_ctx = g;
    esp_err_t err = httpd_register_uri_handler(srv, &gated);
    if (err != ESP_OK) ESP_LOGE(TAG, "register %s: %s", uri->uri, esp_err_to_name(err));
}

/**
 * @brief The request body as a NUL-terminated string in @p buf.
 * @version 1
 * @utility
 * @return Whether it was read whole.
 */
static bool read_body(httpd_req_t *req, char *buf, size_t n)
{
    size_t len = req->content_len, got = 0;
    bool ok = len > 0 && len < n;
    while (ok && got < len) {
        int r = httpd_req_recv(req, buf + got, len - got);
        ok = r > 0;
        got += ok ? (size_t)r : 0;
    }
    if (ok) buf[len] = '\0';
    return ok;
}

/**
 * @brief POST /api/pair {"code": "..."} → {"token": "...", "expires_in": 1800}, or 403.
 * @version 1
 * @callback
 * @return ESP_OK.
 */
static esp_err_t pair_handler(httpd_req_t *req)
{
    char body[PAIR_BODY_MAX], token[AUTH_TOKEN_LEN];
    cJSON *root = read_body(req, body, sizeof(body)) ? cJSON_Parse(body) : NULL;
    const cJSON *code = cJSON_GetObjectItemCaseSensitive(root, "code");
    bool ok = cJSON_IsString(code) && auth_pair(code->valuestring, token);
    cJSON_Delete(root);
    httpd_resp_set_type(req, "application/json");
    if (!ok) {
        httpd_resp_set_status(req, "403 Forbidden");
        httpd_resp_send(req, "{\"error\":\"that code is used or expired: scan again\"}",
                        HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }
    char out[96];
    snprintf(out, sizeof(out), "{\"token\":\"%s\",\"expires_in\":%lld}", token,
             (long long)(AUTH_SESSION_TTL_US / 1000000));
    httpd_resp_send(req, out, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

/**
 * @brief Register POST /api/pair (public).
 * @version 1
 * @req REQ-UI-005
 */
void http_auth_register_pairing(httpd_handle_t srv)
{
    static const httpd_uri_t pair = {
        .uri = "/api/pair", .method = HTTP_POST, .handler = pair_handler};
    httpd_register_uri_handler(srv, &pair);   // the one ungated /api endpoint
}
