/**
 * @file http_json.c
 * @version 1
 * @brief The API handlers' JSON plumbing (see http_json.h).
 */
#include "http_json.h"

#include <stdlib.h>

/**
 * @brief The request's whole body (NUL-terminated; free it), or NULL when larger than @p max
 *        or cut short.
 * @version 1
 * @req REQ-UI-005
 * @return The body.
 */
char *http_json_body(httpd_req_t *req, size_t max)
{
    char *body = req->content_len < max ? calloc(1, req->content_len + 1) : NULL;
    size_t got = 0;
    while (body && got < req->content_len) {
        int r = httpd_req_recv(req, body + got, req->content_len - got);
        if (r <= 0) break;
        got += (size_t)r;
    }
    if (body && got != req->content_len) {
        free(body);
        body = NULL;
    }
    return body;
}

/**
 * @brief Send @p status with JSON @p body (a cJSON object; deleted).
 * @version 1
 * @req REQ-UI-005
 */
void http_json_send(httpd_req_t *req, const char *status, cJSON *body)
{
    char *json = cJSON_PrintUnformatted(body);
    cJSON_Delete(body);
    httpd_resp_set_status(req, status);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json ? json : "{}", HTTPD_RESP_USE_STRLEN);
    free(json);
}

/**
 * @brief {"error": @p why}.
 * @version 1
 * @req REQ-UI-005
 * @return The object.
 */
cJSON *http_json_error(const char *why)
{
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "error", why);
    return o;
}
