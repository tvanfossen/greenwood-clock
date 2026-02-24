// components/http_api/http_api.c

#include "http_api.h"
#include "ota.h"
#include "secrets.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "sdcard.h"
#include "debug_log.h"
#include <sys/stat.h>
#include <dirent.h>
#include <string.h>
#include <unistd.h>

static const char* TAG = "http_api";
static httpd_handle_t server = NULL;

// Maximum upload size: 10 MB
#define MAX_UPLOAD_SIZE     (10 * 1024 * 1024)
#define UPLOAD_BUFFER_SIZE  (4 * 1024)

// =============================================================================
// Upload helpers
// =============================================================================

/**
 * @brief Verify SD card is mounted and content length is within limits.
 *
 * Sends the appropriate HTTP error response on failure.
 *
 * @param req  Incoming request.
 * @return ESP_OK if preconditions pass; ESP_FAIL otherwise.
 */
static esp_err_t upload_check_preconditions(httpd_req_t* req)
{
    if (!sdcard_is_mounted()) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "SD card not mounted");
        return ESP_FAIL;
    }
    if (req->content_len > MAX_UPLOAD_SIZE) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "File too large (max 10 MB)");
        return ESP_FAIL;
    }
    return ESP_OK;
}

/**
 * @brief Create the parent directory for @p filepath if it does not exist.
 *
 * @param filepath  Full destination path (e.g. /sdcard/foo/bar.bin).
 */
static void upload_ensure_dir(const char* filepath)
{
    char dirpath[256];
    strncpy(dirpath, filepath, sizeof(dirpath));
    char* last_slash = strrchr(dirpath, '/');
    if (last_slash) {
        *last_slash = '\0';
        mkdir(dirpath, 0755);  // Non-recursive; caller must create parents first
    }
}

/**
 * @brief Perform one receive iteration: read from @p req and write to @p f.
 *
 * On a hard error, closes @p f, unlinks @p filepath, and sends the HTTP error.
 *
 * @param req       Incoming request.
 * @param f         Open file handle.
 * @param filepath  Path used only for unlinking on error.
 * @param buf       Caller-supplied scratch buffer.
 * @param buf_size  Size of @p buf.
 * @param remaining Bytes still expected from the client.
 * @return Bytes written (>0), 0 on timeout (retry), -1 on error.
 */
static int upload_recv_chunk(httpd_req_t* req, FILE* f, const char* filepath,
                              char* buf, size_t buf_size, size_t remaining)
{
    int r = httpd_req_recv(req, buf, MIN(buf_size, remaining));
    if (r == HTTPD_SOCK_ERR_TIMEOUT) return 0;
    if (r > 0) { fwrite(buf, 1, r, f); return r; }
    fclose(f);
    unlink(filepath);
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Upload interrupted");
    return -1;
}

/**
 * @brief Receive all body bytes from @p req into @p f.
 *
 * On error, @p f has already been closed and @p filepath unlinked.
 * On success, closes @p f and sets @p total_out to bytes written.
 *
 * @param req        Incoming request.
 * @param f          Open file handle to write into.
 * @param filepath   Path used only for unlinking on error.
 * @param total_out  Set to bytes written on success.
 * @return ESP_OK or ESP_FAIL.
 */
static esp_err_t upload_receive_loop(httpd_req_t* req, FILE* f,
                                      const char* filepath, size_t* total_out)
{
    char buf[UPLOAD_BUFFER_SIZE];
    size_t total = 0;
    int r;
    while (total < req->content_len) {
        r = upload_recv_chunk(req, f, filepath, buf, sizeof(buf), req->content_len - total);
        if (r < 0) return ESP_FAIL;
        total += (size_t)r;
    }
    fclose(f);
    *total_out = total;
    return ESP_OK;
}

/**
 * @brief Open @p filepath for writing and receive the full request body.
 *
 * @param req        Incoming request.
 * @param filepath   Destination path.
 * @param total_out  Set to bytes received on success.
 * @return ESP_OK or ESP_FAIL.
 */
static esp_err_t upload_open_and_receive(httpd_req_t* req, const char* filepath,
                                          size_t* total_out)
{
    FILE* f = fopen(filepath, "wb");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open for writing: %s", filepath);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to create file");
        return ESP_FAIL;
    }
    return upload_receive_loop(req, f, filepath, total_out);
}

/**
 * @brief Send a JSON success response for a completed upload.
 *
 * @param req       Incoming request.
 * @param filepath  Path where the file was written.
 * @param total     Number of bytes written.
 */
static void upload_send_success(httpd_req_t* req, const char* filepath, size_t total)
{
    ESP_LOGI(TAG, "File uploaded: %s (%zu bytes)", filepath, total);
    char response[256];
    snprintf(response, sizeof(response),
             "{\"status\":\"success\",\"path\":\"%s\",\"size\":%zu}\n",
             filepath, total);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
}

// =============================================================================
// Download / directory helpers
// =============================================================================

/**
 * @brief Check SD card mounted and that @p filepath exists via stat.
 *
 * Sends the appropriate HTTP error on failure.
 *
 * @param req      Incoming request.
 * @param filepath Path to check.
 * @param st_out   Populated with stat result on success.
 * @return ESP_OK, or an error code with HTTP response already sent.
 */
static esp_err_t download_resolve_path(httpd_req_t* req, const char* filepath,
                                        struct stat* st_out)
{
    if (!sdcard_is_mounted()) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "SD card not mounted");
        return ESP_ERR_INVALID_STATE;
    }
    if (stat(filepath, st_out) != 0) {
        httpd_resp_send_404(req);
        return ESP_ERR_NOT_FOUND;
    }
    return ESP_OK;
}

/**
 * @brief Format and stream a single directory entry as a JSON object.
 *
 * Skips entries whose stat fails. Prepends a comma separator after the first.
 *
 * @param req       Incoming request.
 * @param dirpath   Parent directory path.
 * @param entry     Directory entry.
 * @param first_out Tracks whether a comma separator is needed; updated in place.
 */
static void download_send_dir_entry(httpd_req_t* req, const char* dirpath,
                                     const struct dirent* entry, bool* first_out)
{
    char entry_path[512];
    snprintf(entry_path, sizeof(entry_path), "%s/%s", dirpath, entry->d_name);
    struct stat st;
    if (stat(entry_path, &st) != 0) return;
    if (!*first_out) httpd_resp_sendstr_chunk(req, ",\n");
    *first_out = false;
    char json[256];
    snprintf(json, sizeof(json),
             "{\"name\":\"%s\",\"size\":%ld,\"type\":\"%s\"}",
             entry->d_name, st.st_size,
             S_ISDIR(st.st_mode) ? "dir" : "file");
    httpd_resp_sendstr_chunk(req, json);
}

/**
 * @brief Iterate @p dir and stream each non-dot entry as JSON.
 *
 * @param req      Incoming request.
 * @param dir      Open directory handle.
 * @param filepath Parent directory path.
 */
static void download_scan_dir(httpd_req_t* req, DIR* dir, const char* filepath)
{
    struct dirent* entry;
    bool first = true;
    while ((entry = readdir(dir)) != NULL) {
        bool is_dot = (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0);
        if (!is_dot) download_send_dir_entry(req, filepath, entry, &first);
    }
}

/**
 * @brief Open @p filepath as a directory and stream its contents as JSON.
 *
 * Response format: {"files":[{"name":…,"size":…,"type":…},…]}
 *
 * @param req       Incoming request.
 * @param filepath  Directory path.
 * @return ESP_OK or ESP_FAIL.
 */
static esp_err_t download_list_directory(httpd_req_t* req, const char* filepath)
{
    DIR* dir = opendir(filepath);
    if (!dir) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to open directory");
        return ESP_FAIL;
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr_chunk(req, "{\"files\":[\n");
    download_scan_dir(req, dir, filepath);
    closedir(dir);
    httpd_resp_sendstr_chunk(req, "\n]}\n");
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

/**
 * @brief Set Content-Type and Content-Disposition headers for a file download.
 *
 * @param req       Incoming request.
 * @param filepath  Path used to derive the filename.
 */
static void download_set_file_headers(httpd_req_t* req, const char* filepath)
{
    httpd_resp_set_type(req, "application/octet-stream");
    const char* fname = strrchr(filepath, '/');
    fname = fname ? fname + 1 : filepath;
    char content_disp[256];
    snprintf(content_disp, sizeof(content_disp), "attachment; filename=\"%s\"", fname);
    httpd_resp_set_hdr(req, "Content-Disposition", content_disp);
}

/**
 * @brief Stream the contents of @p f to the HTTP client in 4 KB chunks.
 *
 * Sends the terminal NULL chunk on success. The caller is responsible for
 * closing @p f regardless of the return value.
 *
 * @param req  Incoming request.
 * @param f    Open file handle positioned at start.
 * @return ESP_OK or ESP_FAIL if a chunk send fails.
 */
static esp_err_t download_pump_file(httpd_req_t* req, FILE* f)
{
    char buf[UPLOAD_BUFFER_SIZE];
    size_t len;
    while ((len = fread(buf, 1, sizeof(buf), f)) > 0) {
        if (httpd_resp_send_chunk(req, buf, len) != ESP_OK) return ESP_FAIL;
    }
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

/**
 * @brief Open @p filepath as a file and stream it to the HTTP client.
 *
 * @param req       Incoming request.
 * @param filepath  Path to the file to send.
 * @return ESP_OK or ESP_FAIL.
 */
static esp_err_t download_stream_file(httpd_req_t* req, const char* filepath)
{
    FILE* f = fopen(filepath, "rb");
    if (!f) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to open file");
        return ESP_FAIL;
    }
    download_set_file_headers(req, filepath);
    esp_err_t err = download_pump_file(req, f);
    fclose(f);
    if (err == ESP_OK) ESP_LOGI(TAG, "File downloaded: %s", filepath);
    return err;
}

// =============================================================================
// Log download helpers
// =============================================================================

/**
 * @brief Set HTTP headers appropriate for a debug log download.
 *
 * @param req  Incoming request.
 */
static void logs_set_response_headers(httpd_req_t* req)
{
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_set_hdr(req, "Content-Disposition", "attachment; filename=\"debug.log\"");
}

/**
 * @brief Open @p log_path, set headers, and stream the log to the client.
 *
 * @param req       Incoming request.
 * @param log_path  Path to the log file.
 * @return ESP_OK or ESP_FAIL.
 */
static esp_err_t logs_open_and_send(httpd_req_t* req, const char* log_path)
{
    FILE* f = fopen(log_path, "rb");
    if (!f) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to open log file");
        return ESP_FAIL;
    }
    logs_set_response_headers(req);
    esp_err_t err = download_pump_file(req, f);
    fclose(f);
    if (err == ESP_OK) ESP_LOGI(TAG, "Log file downloaded: %s", log_path);
    return err;
}

// =============================================================================
// OTA push helpers
// =============================================================================

/**
 * @brief Send a 401 Unauthorized response (HTTP server API has no 401 code).
 *
 * @param req  Incoming request.
 */
static void ota_send_unauthorized(httpd_req_t* req)
{
    httpd_resp_set_status(req, "401 Unauthorized");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"error\":\"Unauthorized\"}", HTTPD_RESP_USE_STRLEN);
}

/**
 * @brief Verify the Authorization: Bearer header matches OTA_API_TOKEN.
 *
 * Sends a 401 response on failure.
 *
 * @param req  Incoming request.
 * @return ESP_OK on success; ESP_FAIL on auth failure.
 */
static esp_err_t ota_push_check_auth(httpd_req_t* req)
{
    char auth[160];
    if (httpd_req_get_hdr_value_str(req, "Authorization", auth, sizeof(auth)) != ESP_OK) {
        ESP_LOGW(TAG, "OTA push: missing Authorization header");
        ota_send_unauthorized(req);
        return ESP_FAIL;
    }
    char expected[160];
    snprintf(expected, sizeof(expected), "Bearer %s", OTA_API_TOKEN);
    if (strcmp(auth, expected) != 0) {
        ESP_LOGW(TAG, "OTA push: invalid token");
        ota_send_unauthorized(req);
        return ESP_FAIL;
    }
    return ESP_OK;
}

/**
 * @brief Verify the request carries a non-zero Content-Length.
 *
 * Sends a 400 response on failure.
 *
 * @param req  Incoming request.
 * @return ESP_OK on success; ESP_FAIL if content_len is 0.
 */
static esp_err_t ota_push_check_size(httpd_req_t* req)
{
    if (req->content_len == 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing firmware data");
        return ESP_FAIL;
    }
    return ESP_OK;
}

/**
 * @brief Receive all firmware body bytes and write them via ota_push_write().
 *
 * @param req        Incoming request.
 * @param total_size Expected firmware size (from Content-Length).
 * @return ESP_OK on success; ESP_FAIL on receive or write error.
 */
static esp_err_t ota_push_receive_loop(httpd_req_t* req, size_t total_size)
{
    char buf[UPLOAD_BUFFER_SIZE];
    size_t received = 0;
    while (received < total_size) {
        int r = httpd_req_recv(req, buf, MIN(sizeof(buf), total_size - received));
        if (r == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (r <= 0) return ESP_FAIL;
        esp_err_t err = ota_push_write(buf, (size_t)r);
        if (err != ESP_OK) return err;
        received += (size_t)r;
    }
    return ESP_OK;
}

/**
 * @brief Call ota_push_begin() and send HTTP error on failure.
 *
 * @param req  Incoming request.
 * @return ESP_OK on success.
 */
static esp_err_t ota_push_start(httpd_req_t* req)
{
    ESP_LOGI(TAG, "OTA push: receiving %zu bytes", (size_t)req->content_len);
    esp_err_t err = ota_push_begin((size_t)req->content_len);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, ota_get_last_error());
    }
    return err;
}

/**
 * @brief Run the firmware receive loop; abort OTA and send HTTP error on failure.
 *
 * @param req  Incoming request.
 * @return ESP_OK on success.
 */
static esp_err_t ota_push_download(httpd_req_t* req)
{
    esp_err_t err = ota_push_receive_loop(req, (size_t)req->content_len);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OTA push: receive failed, aborting");
        ota_push_abort();
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Firmware receive failed");
    }
    return err;
}

/**
 * @brief Validate and commit the OTA image; send HTTP response before rebooting.
 *
 * Calls ota_push_finish() to validate and set the boot partition, sends a
 * JSON success response, then calls ota_push_commit() which reboots the device.
 * Only returns (with an error code) if finish fails.
 *
 * @param req  Incoming request.
 * @return Error code on failure; does not return on success (device reboots).
 */
static esp_err_t ota_push_complete(httpd_req_t* req)
{
    esp_err_t err = ota_push_finish();
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, ota_get_last_error());
        return err;
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"rebooting\"}\n");
    ota_push_commit();  // does not return
    return ESP_OK;
}

/**
 * @brief Orchestrate start → download → finish for an authenticated OTA request.
 *
 * @param req  Incoming request (auth and size already verified).
 * @return Error code on failure; does not return on success (device reboots).
 */
static esp_err_t ota_push_execute(httpd_req_t* req)
{
    esp_err_t err = ota_push_start(req);
    if (err != ESP_OK) return err;
    err = ota_push_download(req);
    if (err != ESP_OK) return err;
    return ota_push_complete(req);
}

// =============================================================================
// HTTP API server helpers
// =============================================================================

// Forward declarations for handlers referenced by http_api_register_handlers
static esp_err_t upload_file_handler(httpd_req_t *req);
static esp_err_t download_file_handler(httpd_req_t *req);
static esp_err_t status_handler(httpd_req_t *req);
static esp_err_t logs_test_handler(httpd_req_t *req);
static esp_err_t logs_download_handler(httpd_req_t *req);
static esp_err_t ota_push_handler(httpd_req_t *req);
static esp_err_t ota_status_handler(httpd_req_t *req);

/**
 * @brief Apply non-default httpd configuration values.
 *
 * @param config  Configuration struct to populate.
 */
static void http_api_configure_server(httpd_config_t* config)
{
    config->server_port     = 80;
    config->max_uri_handlers = 16;
    config->stack_size      = 8192;
    config->uri_match_fn    = httpd_uri_match_wildcard;
}

/**
 * @brief Register file, status, and log URI handlers.
 *
 * @param srv  Running httpd server handle.
 */
static void http_api_register_file_handlers(httpd_handle_t srv)
{
    static const httpd_uri_t uri_status = {
        .uri     = "/api/status",
        .method  = HTTP_GET,
        .handler = status_handler,
    };
    static const httpd_uri_t uri_logs = {
        .uri     = "/api/logs/download",
        .method  = HTTP_GET,
        .handler = logs_download_handler,
    };
    static const httpd_uri_t uri_logs_test = {
        .uri     = "/api/logs/test",
        .method  = HTTP_GET,
        .handler = logs_test_handler,
    };
    static const httpd_uri_t uri_upload = {
        .uri     = "/files/*",
        .method  = HTTP_POST,
        .handler = upload_file_handler,
    };
    static const httpd_uri_t uri_download = {
        .uri     = "/files/*",
        .method  = HTTP_GET,
        .handler = download_file_handler,
    };
    httpd_register_uri_handler(srv, &uri_status);
    httpd_register_uri_handler(srv, &uri_logs);
    httpd_register_uri_handler(srv, &uri_logs_test);
    httpd_register_uri_handler(srv, &uri_upload);
    httpd_register_uri_handler(srv, &uri_download);
}

/**
 * @brief Register OTA push and status URI handlers.
 *
 * @param srv  Running httpd server handle.
 */
static void http_api_register_ota_handlers(httpd_handle_t srv)
{
    static const httpd_uri_t uri_ota_push = {
        .uri     = "/ota",
        .method  = HTTP_POST,
        .handler = ota_push_handler,
    };
    static const httpd_uri_t uri_ota_status = {
        .uri     = "/ota/status",
        .method  = HTTP_GET,
        .handler = ota_status_handler,
    };
    httpd_register_uri_handler(srv, &uri_ota_push);
    httpd_register_uri_handler(srv, &uri_ota_status);
}

/**
 * @brief Register all URI handlers with the running server.
 *
 * @param srv  Running httpd server handle.
 */
static void http_api_register_handlers(httpd_handle_t srv)
{
    http_api_register_file_handlers(srv);
    http_api_register_ota_handlers(srv);
}

// =============================================================================
// Request handlers
// =============================================================================

/**
 * @brief Upload file to SD card.
 * POST /files/[path] — body is raw file data (application/octet-stream).
 */
static esp_err_t upload_file_handler(httpd_req_t *req)
{
    char filepath[256];
    snprintf(filepath, sizeof(filepath), "/sdcard/%s", req->uri + 7);
    ESP_LOGI(TAG, "Upload: %s -> %s", req->uri, filepath);
    esp_err_t err = upload_check_preconditions(req);
    if (err != ESP_OK) return err;
    upload_ensure_dir(filepath);
    size_t total = 0;
    err = upload_open_and_receive(req, filepath, &total);
    if (err == ESP_OK) upload_send_success(req, filepath, total);
    return err;
}

/**
 * @brief Download a file or list directory contents.
 * GET /files/[path]
 */
static esp_err_t download_file_handler(httpd_req_t *req)
{
    char filepath[256];
    snprintf(filepath, sizeof(filepath), "/sdcard/%s", req->uri + 7);
    ESP_LOGI(TAG, "Download/list: %s -> %s", req->uri, filepath);
    struct stat st;
    esp_err_t err = download_resolve_path(req, filepath, &st);
    if (err != ESP_OK) return err;
    if (S_ISDIR(st.st_mode)) return download_list_directory(req, filepath);
    return download_stream_file(req, filepath);
}

/**
 * @brief Return basic device status as JSON.
 * GET /api/status
 */
static esp_err_t status_handler(httpd_req_t *req)
{
    char response[512];
    const char* log_path = debug_log_get_path();
    bool logging_active = debug_log_is_active();
    snprintf(response, sizeof(response),
             "{\n"
             "  \"status\": \"ok\",\n"
             "  \"uptime\": %lld,\n"
             "  \"free_heap\": %lu,\n"
             "  \"sd_card\": %s,\n"
             "  \"debug_log_active\": %s,\n"
             "  \"debug_log_path\": \"%s\"\n"
             "}\n",
             esp_timer_get_time() / 1000000,
             (unsigned long)esp_get_free_heap_size(),
             sdcard_is_mounted() ? "true" : "false",
             logging_active ? "true" : "false",
             log_path ? log_path : "null");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

/**
 * @brief Trigger a test write to the debug log.
 * GET /api/logs/test
 */
static esp_err_t logs_test_handler(httpd_req_t *req)
{
    esp_err_t ret = debug_log_test_write();
    char response[128];
    if (ret == ESP_OK) {
        snprintf(response, sizeof(response),
                 "{\"status\":\"success\",\"message\":\"Test message written to log\"}\n");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
    } else {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                            "Debug logging not active or test write failed");
    }
    return ret;
}

/**
 * @brief Download the debug log file.
 * GET /api/logs/download
 *
 * Temporarily closes the debug log write handle before reading because FAT32
 * does not support concurrent file handles on the same path.  The handle is
 * reopened immediately after the transfer completes.
 */
static esp_err_t logs_download_handler(httpd_req_t *req)
{
    const char* log_path = debug_log_pause_for_read();
    if (!log_path) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Debug logging not active");
        return ESP_FAIL;
    }
    esp_err_t err = logs_open_and_send(req, log_path);
    debug_log_reopen();
    return err;
}

/**
 * @brief Receive a firmware image via HTTP POST and flash it to the OTA partition.
 * POST /ota — body is raw firmware binary.
 *
 * Requires Authorization: Bearer <OTA_API_TOKEN>.
 * Reboots the device on success; returns error code on failure.
 */
static esp_err_t ota_push_handler(httpd_req_t *req)
{
    esp_err_t err = ota_push_check_auth(req);
    if (err != ESP_OK) return err;
    err = ota_push_check_size(req);
    if (err != ESP_OK) return err;
    return ota_push_execute(req);
}

/**
 * @brief Return the current OTA push state as JSON.
 * GET /ota/status
 *
 * Response: {"state":"idle"|"receiving"|"validating"|"rebooting"|"error",
 *            "bytes_written":N,"total_bytes":N,"error":"..."}
 */
static esp_err_t ota_status_handler(httpd_req_t *req)
{
    static const char* const state_str[] = {
        "idle", "receiving", "validating", "rebooting", "error"
    };
    ota_status_t st = ota_get_status();
    char response[256];
    snprintf(response, sizeof(response),
             "{\"state\":\"%s\",\"bytes_written\":%zu,"
             "\"total_bytes\":%zu,\"error\":\"%s\"}\n",
             state_str[st.state], st.bytes_written,
             st.total_bytes, st.error_msg);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// =============================================================================
// Public API
// =============================================================================

esp_err_t http_api_start(void)
{
    if (server != NULL) {
        ESP_LOGW(TAG, "HTTP server already running");
        return ESP_OK;
    }
    ESP_LOGI(TAG, "Starting HTTP API server on port 80");
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    http_api_configure_server(&config);
    esp_err_t ret = httpd_start(&server, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server: %d", (int)ret);
        return ret;
    }
    http_api_register_handlers(server);
    ESP_LOGI(TAG, "HTTP API server started successfully");
    return ESP_OK;
}

esp_err_t http_api_stop(void)
{
    if (server == NULL) {
        ESP_LOGW(TAG, "HTTP server not running");
        return ESP_OK;
    }
    ESP_LOGI(TAG, "Stopping HTTP API server");
    esp_err_t ret = httpd_stop(server);
    server = NULL;
    return ret;
}

bool http_api_is_running(void)
{
    return (server != NULL);
}
