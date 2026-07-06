// components/http_api/http_api.c

#include "http_api.h"
#include "ota.h"
#include "secrets.h"
#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_system.h"
#include "sdcard.h"
#include "debug_log.h"
#include "udp_log.h"
#include "display_fsm.h"
#include "nws.h"
#include "settings.h"
#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_core_dump.h"
#include "esp_partition.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <sys/stat.h>
#include <dirent.h>
#include "esp_vfs_fat.h"
#include "bsp/esp32_p4_function_ev_board.h"
#include "lvgl.h"
#include "esp_lvgl_port.h"
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdlib.h>

static const char* TAG = "http_api";
static httpd_handle_t server = NULL;
static SemaphoreHandle_t s_launch_sem = NULL;

void http_api_set_launch_sem(SemaphoreHandle_t sem)
{
    s_launch_sem = sem;
}

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
    strncpy(dirpath, filepath, sizeof(dirpath) - 1);
    dirpath[sizeof(dirpath) - 1] = '\0';
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
// Recursively create parent directories for a file path (like mkdir -p).
static void mkdirs(const char *filepath)
{
    char tmp[256];
    strlcpy(tmp, filepath, sizeof(tmp));
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
}

static esp_err_t upload_open_and_receive(httpd_req_t* req, const char* filepath,
                                          size_t* total_out)
{
    mkdirs(filepath);
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
static esp_err_t delete_file_handler(httpd_req_t *req);
static esp_err_t status_handler(httpd_req_t *req);
static esp_err_t boot_history_handler(httpd_req_t *req);
static esp_err_t disk_usage_handler(httpd_req_t *req);
static esp_err_t sdcard_format_handler(httpd_req_t *req);
static esp_err_t logs_test_handler(httpd_req_t *req);
static esp_err_t logs_download_handler(httpd_req_t *req);
static esp_err_t ota_push_handler(httpd_req_t *req);
static esp_err_t ota_status_handler(httpd_req_t *req);
static esp_err_t debug_udp_log_start_handler(httpd_req_t *req);
static esp_err_t debug_udp_log_stop_handler(httpd_req_t *req);
static esp_err_t debug_reboot_handler(httpd_req_t *req);
static esp_err_t debug_launch_handler(httpd_req_t *req);
static esp_err_t www_file_handler(httpd_req_t *req);
static esp_err_t display_state_get_handler(httpd_req_t *req);
static esp_err_t display_state_post_handler(httpd_req_t *req);
static esp_err_t display_surprise_handler(httpd_req_t *req);
static esp_err_t weather_current_handler(httpd_req_t *req);
static esp_err_t weather_forecast_handler(httpd_req_t *req);
static esp_err_t weather_alerts_handler(httpd_req_t *req);
static esp_err_t schedule_get_handler(httpd_req_t *req);
static esp_err_t schedule_post_handler(httpd_req_t *req);
static esp_err_t assets_list_handler(httpd_req_t *req);
static esp_err_t assets_upload_handler(httpd_req_t *req);
static esp_err_t settings_get_handler(httpd_req_t *req);
static esp_err_t settings_post_handler(httpd_req_t *req);

/**
 * @brief Apply non-default httpd configuration values.
 *
 * @param config  Configuration struct to populate.
 */
static void http_api_configure_server(httpd_config_t* config)
{
    config->server_port     = 80;
    config->max_uri_handlers = 40;
    config->stack_size      = 8192;
    config->uri_match_fn    = httpd_uri_match_wildcard;
}

// ---------------------------------------------------------------------------
// Core dump retrieval — Bearer-auth'd (the dump contains RAM: keys, tokens).
// Decode the served blob with: esp-coredump info_corefile -c core.elf <elf>
// ---------------------------------------------------------------------------

static const esp_partition_t *coredump_partition(void)
{
    return esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                    ESP_PARTITION_SUBTYPE_DATA_COREDUMP, NULL);
}

// GET /api/coredump/info → {"present":bool,"size":N,"partition":bool}
static esp_err_t coredump_info_handler(httpd_req_t *req)
{
    if (ota_push_check_auth(req) != ESP_OK) return ESP_FAIL;

    size_t addr = 0, size = 0;
    bool present = (esp_core_dump_image_get(&addr, &size) == ESP_OK && size > 0);

    char body[128];
    snprintf(body, sizeof(body), "{\"present\":%s,\"size\":%u,\"partition\":%s}\n",
             present ? "true" : "false", (unsigned)size,
             coredump_partition() ? "true" : "false");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, body, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// GET /api/coredump → raw ELF coredump blob.
static esp_err_t coredump_get_handler(httpd_req_t *req)
{
    if (ota_push_check_auth(req) != ESP_OK) return ESP_FAIL;

    const esp_partition_t *part = coredump_partition();
    size_t addr = 0, size = 0;
    if (!part || esp_core_dump_image_get(&addr, &size) != ESP_OK || size == 0) {
        httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "No coredump present");
        return ESP_FAIL;
    }

    uint8_t *buf = (uint8_t *)malloc(2048);
    if (!buf) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OOM");
        return ESP_FAIL;
    }
    httpd_resp_set_type(req, "application/octet-stream");
    httpd_resp_set_hdr(req, "Content-Disposition", "attachment; filename=\"core.elf\"");

    esp_err_t err = ESP_OK;
    for (size_t off = 0; off < size && err == ESP_OK; off += 2048) {
        size_t chunk = (size - off > 2048) ? 2048 : (size - off);
        if (esp_partition_read(part, off, buf, chunk) != ESP_OK ||
            httpd_resp_send_chunk(req, (char *)buf, chunk) != ESP_OK) {
            err = ESP_FAIL;
        }
    }
    free(buf);
    httpd_resp_send_chunk(req, NULL, 0);   // terminate response
    return err;
}

// GET /api/screenshot → grab the active display framebuffer (the REAL on-screen
// pixels, including any SW/PPA composite artifacts) and stream it. Response:
// an ASCII header line "w h stride cf\n" followed by the raw pixel data.
// Decode + view with tools/screenshot.py.
static esp_err_t screenshot_get_handler(httpd_req_t *req)
{
    lv_display_t *disp = lv_display_get_default();
    if (!disp) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no display");
        return ESP_FAIL;
    }

    uint32_t w = 0, h = 0, stride = 0, size = 0, cf = 0;
    uint8_t *copy = NULL;

    // Copy the framebuffer under the LVGL lock (the panel DMA reads it
    // concurrently; a quick memcpy gives a coherent snapshot).
    lvgl_port_lock(0);
    lv_draw_buf_t *fb = lv_display_get_buf_active(disp);
    if (fb && fb->data) {
        w      = lv_display_get_horizontal_resolution(disp);
        h      = lv_display_get_vertical_resolution(disp);
        stride = fb->header.stride;
        cf     = fb->header.cf;
        size   = stride * h;
        copy   = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (copy) memcpy(copy, fb->data, size);
    }
    lvgl_port_unlock();

    if (!copy) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "capture failed");
        return ESP_FAIL;
    }

    char hdr[64];
    int hlen = snprintf(hdr, sizeof(hdr), "%lu %lu %lu %lu\n",
                        (unsigned long)w, (unsigned long)h,
                        (unsigned long)stride, (unsigned long)cf);
    httpd_resp_set_type(req, "application/octet-stream");
    httpd_resp_set_hdr(req, "Content-Disposition", "attachment; filename=\"screen.raw\"");

    esp_err_t err = httpd_resp_send_chunk(req, hdr, hlen);
    for (size_t off = 0; off < size && err == ESP_OK; off += 4096) {
        size_t chunk = (size - off > 4096) ? 4096 : (size - off);
        err = httpd_resp_send_chunk(req, (char *)copy + off, chunk);
    }
    heap_caps_free(copy);
    httpd_resp_send_chunk(req, NULL, 0);
    return err;
}

// POST /api/coredump/erase → clear the stored coredump.
static esp_err_t coredump_erase_handler(httpd_req_t *req)
{
    if (ota_push_check_auth(req) != ESP_OK) return ESP_FAIL;

    if (esp_core_dump_image_erase() != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "erase failed");
        return ESP_FAIL;
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"status\":\"erased\"}\n", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
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
    static const httpd_uri_t uri_boot_hist = {
        .uri     = "/api/boot_history",
        .method  = HTTP_GET,
        .handler = boot_history_handler,
    };
    static const httpd_uri_t uri_disk = {
        .uri     = "/api/disk",
        .method  = HTTP_GET,
        .handler = disk_usage_handler,
    };
    static const httpd_uri_t uri_sd_format = {
        .uri     = "/api/sdcard/format",
        .method  = HTTP_POST,
        .handler = sdcard_format_handler,
    };
    static const httpd_uri_t uri_cd_info = {
        .uri     = "/api/coredump/info",
        .method  = HTTP_GET,
        .handler = coredump_info_handler,
    };
    static const httpd_uri_t uri_cd_get = {
        .uri     = "/api/coredump",
        .method  = HTTP_GET,
        .handler = coredump_get_handler,
    };
    static const httpd_uri_t uri_cd_erase = {
        .uri     = "/api/coredump/erase",
        .method  = HTTP_POST,
        .handler = coredump_erase_handler,
    };
    static const httpd_uri_t uri_screenshot = {
        .uri     = "/api/screenshot",
        .method  = HTTP_GET,
        .handler = screenshot_get_handler,
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
    static const httpd_uri_t uri_delete = {
        .uri     = "/files/*",
        .method  = HTTP_DELETE,
        .handler = delete_file_handler,
    };
    httpd_register_uri_handler(srv, &uri_status);
    httpd_register_uri_handler(srv, &uri_boot_hist);
    httpd_register_uri_handler(srv, &uri_disk);
    httpd_register_uri_handler(srv, &uri_sd_format);
    httpd_register_uri_handler(srv, &uri_cd_info);
    httpd_register_uri_handler(srv, &uri_cd_get);
    httpd_register_uri_handler(srv, &uri_cd_erase);
    httpd_register_uri_handler(srv, &uri_screenshot);
    httpd_register_uri_handler(srv, &uri_logs);
    httpd_register_uri_handler(srv, &uri_logs_test);
    httpd_register_uri_handler(srv, &uri_upload);
    httpd_register_uri_handler(srv, &uri_download);
    httpd_register_uri_handler(srv, &uri_delete);
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
 * @brief Register debug endpoint handlers (UDP log, reboot).
 *
 * @param srv  Running httpd server handle.
 */
static void http_api_register_debug_handlers(httpd_handle_t srv)
{
    static const httpd_uri_t uri_udp_log_start = {
        .uri     = "/debug/udp_log",
        .method  = HTTP_POST,
        .handler = debug_udp_log_start_handler,
    };
    static const httpd_uri_t uri_udp_log_stop = {
        .uri     = "/debug/udp_log",
        .method  = HTTP_DELETE,
        .handler = debug_udp_log_stop_handler,
    };
    static const httpd_uri_t uri_reboot = {
        .uri     = "/debug/reboot",
        .method  = HTTP_POST,
        .handler = debug_reboot_handler,
    };
    static const httpd_uri_t uri_launch = {
        .uri     = "/debug/launch",
        .method  = HTTP_POST,
        .handler = debug_launch_handler,
    };
    httpd_register_uri_handler(srv, &uri_udp_log_start);
    httpd_register_uri_handler(srv, &uri_udp_log_stop);
    httpd_register_uri_handler(srv, &uri_reboot);
    httpd_register_uri_handler(srv, &uri_launch);
}

/**
 * @brief Register display control and weather data URI handlers.
 *
 * @param srv  Running httpd server handle.
 */
static void http_api_register_display_handlers(httpd_handle_t srv)
{
    static const httpd_uri_t uri_www = {
        .uri     = "/www/*",
        .method  = HTTP_GET,
        .handler = www_file_handler,
    };
    static const httpd_uri_t uri_display_state_get = {
        .uri     = "/api/display/state",
        .method  = HTTP_GET,
        .handler = display_state_get_handler,
    };
    static const httpd_uri_t uri_display_state_post = {
        .uri     = "/api/display/state",
        .method  = HTTP_POST,
        .handler = display_state_post_handler,
    };
    static const httpd_uri_t uri_display_surprise = {
        .uri     = "/api/display/surprise",
        .method  = HTTP_POST,
        .handler = display_surprise_handler,
    };
    static const httpd_uri_t uri_weather_current = {
        .uri     = "/api/weather/current",
        .method  = HTTP_GET,
        .handler = weather_current_handler,
    };
    static const httpd_uri_t uri_weather_forecast = {
        .uri     = "/api/weather/forecast",
        .method  = HTTP_GET,
        .handler = weather_forecast_handler,
    };
    static const httpd_uri_t uri_weather_alerts = {
        .uri     = "/api/weather/alerts",
        .method  = HTTP_GET,
        .handler = weather_alerts_handler,
    };
    static const httpd_uri_t uri_schedule_get = {
        .uri     = "/api/display/schedule",
        .method  = HTTP_GET,
        .handler = schedule_get_handler,
    };
    static const httpd_uri_t uri_schedule_post = {
        .uri     = "/api/display/schedule",
        .method  = HTTP_POST,
        .handler = schedule_post_handler,
    };
    static const httpd_uri_t uri_assets_list = {
        .uri     = "/api/assets/list",
        .method  = HTTP_GET,
        .handler = assets_list_handler,
    };
    static const httpd_uri_t uri_assets_upload = {
        .uri     = "/api/assets/upload",
        .method  = HTTP_POST,
        .handler = assets_upload_handler,
    };
    static const httpd_uri_t uri_settings_get = {
        .uri     = "/api/settings",
        .method  = HTTP_GET,
        .handler = settings_get_handler,
    };
    static const httpd_uri_t uri_settings_post = {
        .uri     = "/api/settings",
        .method  = HTTP_POST,
        .handler = settings_post_handler,
    };
    httpd_register_uri_handler(srv, &uri_display_state_get);
    httpd_register_uri_handler(srv, &uri_display_state_post);
    httpd_register_uri_handler(srv, &uri_display_surprise);
    httpd_register_uri_handler(srv, &uri_schedule_get);
    httpd_register_uri_handler(srv, &uri_schedule_post);
    httpd_register_uri_handler(srv, &uri_weather_current);
    httpd_register_uri_handler(srv, &uri_weather_forecast);
    httpd_register_uri_handler(srv, &uri_weather_alerts);
    httpd_register_uri_handler(srv, &uri_assets_list);
    httpd_register_uri_handler(srv, &uri_assets_upload);
    httpd_register_uri_handler(srv, &uri_settings_get);
    httpd_register_uri_handler(srv, &uri_settings_post);
    // www wildcard MUST be registered LAST (wildcard matching priority)
    httpd_register_uri_handler(srv, &uri_www);
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
    http_api_register_debug_handlers(srv);
    http_api_register_display_handlers(srv);
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
extern bool main_tsens_get_last(float *out_last, float *out_min, float *out_max);

// Mirror of struct boot_hist_entry_t in main.cpp (packed 8 bytes).
struct boot_hist_entry_t {
    uint8_t  reset_reason;
    uint8_t  _pad[3];
    uint32_t timestamp;
};
extern size_t main_boot_history_get(struct boot_hist_entry_t *out, size_t cap);

/**
 * GET /api/disk
 * Returns SD card capacity: total bytes, free bytes, used bytes,
 * percent used. Uses esp_vfs_fat_info() — the ESP-IDF wrapper that
 * walks the FAT cluster table for free count.
 */
static esp_err_t disk_usage_handler(httpd_req_t *req)
{
    uint64_t total = 0, free_bytes = 0;
    esp_err_t err = esp_vfs_fat_info("/sdcard", &total, &free_bytes);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                            "esp_vfs_fat_info failed");
        return ESP_FAIL;
    }
    uint64_t used = total > free_bytes ? (total - free_bytes) : 0;
    int pct = (total > 0) ? (int)((used * 100) / total) : 0;

    // Card HARDWARE truth from the in-RAM sdmmc_card_t (no SD I/O): the negotiated
    // clock and the CSD capacity. If real_freq is high (~40MHz) and reads are timing
    // out, a bus/speed/signal issue is likely (not a dead card); if csd capacity !=
    // the FS total, the FS view is stale/wrong.
    sdmmc_card_t *card = bsp_sdcard_get_handle();
    char card_name[16] = "?";
    int real_freq = 0, max_freq = 0;
    unsigned long long csd_bytes = 0;
    if (card) {
        snprintf(card_name, sizeof(card_name), "%s", card->cid.name);
        real_freq = card->real_freq_khz;
        max_freq  = card->max_freq_khz;
        csd_bytes = (unsigned long long)card->csd.capacity * card->csd.sector_size;
    }

    char body[384];
    snprintf(body, sizeof(body),
             "{\"mount\":\"/sdcard\","
             "\"total_bytes\":%llu,"
             "\"free_bytes\":%llu,"
             "\"used_bytes\":%llu,"
             "\"used_pct\":%d,"
             "\"card_name\":\"%s\","
             "\"csd_capacity_bytes\":%llu,"
             "\"real_freq_khz\":%d,"
             "\"max_freq_khz\":%d}\n",
             (unsigned long long)total,
             (unsigned long long)free_bytes,
             (unsigned long long)used,
             pct, card_name, csd_bytes, real_freq, max_freq);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, body, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

/**
 * POST /api/sdcard/format
 *
 * Reformats the SD card FAT filesystem in place. Wipes all user files on
 * /sdcard (lottie, backgrounds, maps, photos, logs). Requires Bearer auth
 * (same token as OTA push) to prevent accidental triggering.
 *
 * The debug log is paused for the duration so its file handle doesn't
 * fight with the format operation.
 */
static esp_err_t sdcard_format_handler(httpd_req_t *req)
{
    /* Auth check — same Bearer token as OTA push */
    char auth[128];
    if (httpd_req_get_hdr_value_str(req, "Authorization", auth, sizeof(auth)) != ESP_OK) {
        ESP_LOGW(TAG, "SD format: missing Authorization header");
        httpd_resp_set_status(req, "401 Unauthorized");
        httpd_resp_send(req, "{\"error\":\"missing auth\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }
    char expected[160];
    snprintf(expected, sizeof(expected), "Bearer %s", OTA_API_TOKEN);
    if (strcmp(auth, expected) != 0) {
        ESP_LOGW(TAG, "SD format: bad token");
        httpd_resp_set_status(req, "403 Forbidden");
        httpd_resp_send(req, "{\"error\":\"bad token\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }

    sdmmc_card_t *card = bsp_sdcard_get_handle();
    if (!card) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no SD card");
        return ESP_FAIL;
    }

    ESP_LOGW(TAG, "SD format: starting — debug log paused, all /sdcard data will be wiped");
    debug_log_pause_for_read();

    esp_err_t err = esp_vfs_fat_sdcard_format("/sdcard", card);

    debug_log_reopen();

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SD format failed: %s", esp_err_to_name(err));
        char body[128];
        snprintf(body, sizeof(body), "{\"status\":\"error\",\"err\":\"%s\"}",
                 esp_err_to_name(err));
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_send(req, body, HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "SD format complete — FAT volume rewritten");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req,
        "{\"status\":\"ok\",\"note\":\"FAT volume reformatted; all /sdcard files erased\"}",
        HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t boot_history_handler(httpd_req_t *req)
{
    struct boot_hist_entry_t entries[16];
    size_t n = main_boot_history_get(entries, 16);
    // JSON: {"boot_history":[{reason:%u,timestamp:%lu},...]}
    char body[1024];
    int off = snprintf(body, sizeof(body), "{\"boot_history\":[");
    for (size_t i = 0; i < n; i++) {
        off += snprintf(body + off, sizeof(body) - off,
                        "%s{\"reason\":%u,\"timestamp\":%lu}",
                        i == 0 ? "" : ",",
                        entries[i].reset_reason,
                        (unsigned long)entries[i].timestamp);
        if ((size_t)off >= sizeof(body)) break;
    }
    snprintf(body + off, sizeof(body) - off, "]}");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, body, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t status_handler(httpd_req_t *req)
{
    char response[640];
    const char* log_path = debug_log_get_path();
    bool logging_active = debug_log_is_active();

    float tsens_c = 0.0f, tsens_lo = 0.0f, tsens_hi = 0.0f;
    bool  tsens_ok = main_tsens_get_last(&tsens_c, &tsens_lo, &tsens_hi);

    if (tsens_ok) {
        snprintf(response, sizeof(response),
                 "{\n"
                 "  \"status\": \"ok\",\n"
                 "  \"uptime\": %lld,\n"
                 "  \"free_heap\": %lu,\n"
                 "  \"sd_card\": %s,\n"
                 "  \"debug_log_active\": %s,\n"
                 "  \"debug_log_path\": \"%s\",\n"
                 "  \"tsens_c\": %.1f,\n"
                 "  \"tsens_min_c\": %.1f,\n"
                 "  \"tsens_max_c\": %.1f\n"
                 "}\n",
                 esp_timer_get_time() / 1000000,
                 (unsigned long)esp_get_free_heap_size(),
                 sdcard_is_mounted() ? "true" : "false",
                 logging_active ? "true" : "false",
                 log_path ? log_path : "null",
                 tsens_c, tsens_lo, tsens_hi);
    } else {
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
    }
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
 * @brief Check SD card mounted and stat @p filepath; send HTTP error on failure.
 *
 * @param req       Incoming request.
 * @param filepath  Path to check.
 * @param st_out    Populated with stat result on success.
 * @return ESP_OK, or ESP_FAIL with HTTP response already sent.
 */
static esp_err_t delete_resolve_and_stat(httpd_req_t* req, const char* filepath,
                                          struct stat* st_out)
{
    if (!sdcard_is_mounted()) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "SD card not mounted");
        return ESP_FAIL;
    }
    if (stat(filepath, st_out) != 0) {
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }
    return ESP_OK;
}

/**
 * @brief Remove @p filepath (file or empty directory) and send JSON response.
 *
 * @param req       Incoming request.
 * @param filepath  Path to delete.
 * @param st        Stat result used to distinguish file from directory.
 * @return ESP_OK or ESP_FAIL.
 */
static esp_err_t delete_perform(httpd_req_t* req, const char* filepath,
                                 const struct stat* st)
{
    int result = S_ISDIR(st->st_mode) ? rmdir(filepath) : unlink(filepath);
    if (result != 0) {
        ESP_LOGE(TAG, "Delete failed for %s: errno %d", filepath, errno);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Delete failed");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Deleted: %s", filepath);
    char response[256];
    snprintf(response, sizeof(response), "{\"deleted\":\"%s\"}\n", filepath);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

/**
 * @brief Delete a file or empty directory from the SD card.
 * DELETE /files/[path]
 */
static esp_err_t delete_file_handler(httpd_req_t *req)
{
    char filepath[256];
    snprintf(filepath, sizeof(filepath), "/sdcard/%s", req->uri + 7);
    ESP_LOGI(TAG, "Delete: %s -> %s", req->uri, filepath);
    struct stat st;
    esp_err_t err = delete_resolve_and_stat(req, filepath, &st);
    if (err != ESP_OK) return err;
    return delete_perform(req, filepath, &st);
}

// =============================================================================
// Debug endpoint helpers
// =============================================================================

/**
 * @brief Extract a quoted JSON string value for @p key from @p body.
 *
 * Searches for @p key (e.g. "\"host\""), then extracts the next quoted value.
 *
 * @param body     Null-terminated request body.
 * @param key      JSON key string to search for (including surrounding quotes).
 * @param out      Destination buffer.
 * @param out_len  Size of @p out.
 * @return ESP_OK on success, ESP_FAIL if key not found or value malformed.
 */
static esp_err_t parse_json_string_field(const char* body, const char* key,
                                          char* out, size_t out_len)
{
    const char* p = strstr(body, key);
    if (p) p = strchr(p + strlen(key), '"');
    if (p) p++;
    const char* end = p ? strchr(p, '"') : NULL;
    if (!p || !end) return ESP_FAIL;
    size_t len = (size_t)(end - p);
    if (len == 0 || len >= out_len) return ESP_FAIL;
    memcpy(out, p, len);
    out[len] = '\0';
    return ESP_OK;
}

/**
 * @brief Extract the numeric "port" field from a JSON body.
 *
 * @param body     Null-terminated request body.
 * @param port_out Receives the parsed port (1–65535).
 * @return ESP_OK on success, ESP_FAIL if field is missing or out of range.
 */
static esp_err_t parse_json_port_field(const char* body, uint16_t* port_out)
{
    const char* p = strstr(body, "\"port\"");
    if (!p) return ESP_FAIL;
    p += 6;
    while (*p == ':' || *p == ' ') p++;
    int port = atoi(p);
    if (port <= 0 || port > 65535) return ESP_FAIL;
    *port_out = (uint16_t)port;
    return ESP_OK;
}

/**
 * @brief Parse {"host":"<ip>","port":<n>} from request body.
 *
 * @param req       Incoming request.
 * @param host_out  Buffer to receive the host string.
 * @param host_len  Size of host_out.
 * @param port_out  Receives the parsed port number.
 * @return ESP_OK on success, ESP_FAIL if body is missing or malformed.
 */
static esp_err_t debug_parse_udp_log_body(httpd_req_t* req,
                                           char* host_out, size_t host_len,
                                           uint16_t* port_out)
{
    char body[160];
    int received = httpd_req_recv(req, body, (int)(sizeof(body) - 1));
    if (received <= 0) return ESP_FAIL;
    body[received] = '\0';
    if (parse_json_string_field(body, "\"host\"", host_out, host_len) != ESP_OK) return ESP_FAIL;
    return parse_json_port_field(body, port_out);
}

/**
 * @brief Start UDP log streaming.
 * POST /debug/udp_log — body: {"host":"192.168.1.x","port":5555}
 */
static esp_err_t debug_udp_log_start_handler(httpd_req_t *req)
{
    char host[64];
    uint16_t port = 0;
    if (debug_parse_udp_log_body(req, host, sizeof(host), &port) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                            "Body must be {\"host\":\"<ip>\",\"port\":<n>}");
        return ESP_FAIL;
    }
    esp_err_t err = udp_log_init(host, port);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                            "Failed to start UDP log streaming");
        return err;
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"status\":\"ok\"}\n", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

/**
 * @brief Stop UDP log streaming.
 * DELETE /debug/udp_log
 */
static esp_err_t debug_udp_log_stop_handler(httpd_req_t *req)
{
    udp_log_deinit();
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"status\":\"ok\"}\n", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

/**
 * @brief Reboot task — delays briefly so the HTTP response can flush.
 */
static void reboot_task(void* arg)
{
    vTaskDelay(pdMS_TO_TICKS(500));
    debug_log_flush();
    esp_restart();
}

/**
 * @brief Trigger a device reboot via HTTP.
 * POST /debug/reboot
 */
static esp_err_t debug_reboot_handler(httpd_req_t *req)
{
    ESP_LOGW(TAG, "Remote reboot requested via HTTP");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"status\":\"rebooting\"}\n", HTTPD_RESP_USE_STRLEN);
    xTaskCreate(reboot_task, "http_reboot", 2048, NULL, 5, NULL);
    return ESP_OK;
}

/**
 * @brief Signal the boot sequence to proceed to display init immediately.
 * POST /debug/launch
 *
 * Gives the launch semaphore registered via http_api_set_launch_sem().
 * app_main() blocks on the semaphore in boot_await_launch() and will
 * proceed to boot_display_init() + display_fsm_init() upon waking.
 * No-op if the semaphore has not been registered yet (clock already running).
 */
static esp_err_t debug_launch_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "Remote launch requested — signaling boot semaphore");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"ok\"}");
    if (s_launch_sem) {
        xSemaphoreGive(s_launch_sem);
    } else {
        ESP_LOGW(TAG, "debug_launch_handler: no launch semaphore (clock may already be running)");
    }
    return ESP_OK;
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
// Static file server (www)
// =============================================================================

/**
 * @brief Determine MIME type from file extension.
 */
static const char *www_mime_type(const char *path)
{
    const char *ext = strrchr(path, '.');
    if (!ext) return "application/octet-stream";
    if (strcmp(ext, ".html") == 0 || strcmp(ext, ".htm") == 0) return "text/html";
    if (strcmp(ext, ".css") == 0)  return "text/css";
    if (strcmp(ext, ".js") == 0)   return "application/javascript";
    if (strcmp(ext, ".json") == 0) return "application/json";
    if (strcmp(ext, ".png") == 0)  return "image/png";
    if (strcmp(ext, ".jpg") == 0 || strcmp(ext, ".jpeg") == 0) return "image/jpeg";
    if (strcmp(ext, ".gif") == 0)  return "image/gif";
    if (strcmp(ext, ".svg") == 0)  return "image/svg+xml";
    if (strcmp(ext, ".ico") == 0)  return "image/x-icon";
    return "application/octet-stream";
}

/**
 * @brief Serve static files from SD card /sdcard/www/.
 * GET /www/... serves from /sdcard/www/...
 */
static esp_err_t www_file_handler(httpd_req_t *req)
{
    // /www/index.html → /sdcard/www/index.html
    char filepath[256];
    snprintf(filepath, sizeof(filepath), "/sdcard%s", req->uri);

    if (!sdcard_is_mounted()) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "SD card not mounted");
        return ESP_FAIL;
    }

    FILE *f = fopen(filepath, "rb");
    if (!f) {
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, www_mime_type(filepath));
    // Allow caching of static assets (1 hour)
    httpd_resp_set_hdr(req, "Cache-Control", "max-age=3600");

    char buf[UPLOAD_BUFFER_SIZE];
    size_t len;
    while ((len = fread(buf, 1, sizeof(buf), f)) > 0) {
        if (httpd_resp_send_chunk(req, buf, len) != ESP_OK) {
            fclose(f);
            return ESP_FAIL;
        }
    }
    fclose(f);
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

// =============================================================================
// Display control endpoints
// =============================================================================

/**
 * @brief Get current display state.
 * GET /api/display/state → {"state":"clock"}
 */
static esp_err_t display_state_get_handler(httpd_req_t *req)
{
    char response[128];
    snprintf(response, sizeof(response),
             "{\"state\":\"%s\",\"uptime\":%lld}\n",
             display_fsm_get_state_name(),
             esp_timer_get_time() / 1000000);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

/**
 * @brief Map state name string to display_state_id_t.
 * @return DISPLAY_STATE_MAX if not found.
 */
static display_state_id_t state_name_to_id(const char *name)
{
    static const struct { const char *name; display_state_id_t id; } map[] = {
        {"clock",     DISPLAY_STATE_CLOCK},
        {"weather",   DISPLAY_STATE_WEATHER},
        {"radar",     DISPLAY_STATE_RADAR},
        {"astronomy", DISPLAY_STATE_ASTRONOMY},
        {"photos",    DISPLAY_STATE_PHOTOS},
        {"ambient",   DISPLAY_STATE_AMBIENT},
    };
    for (size_t i = 0; i < sizeof(map)/sizeof(map[0]); i++) {
        if (strcmp(name, map[i].name) == 0) return map[i].id;
    }
    return DISPLAY_STATE_MAX;
}

/**
 * @brief Force display to a specific state.
 * POST /api/display/state — body: {"state":"weather"}
 */
static esp_err_t display_state_post_handler(httpd_req_t *req)
{
    char body[128];
    int received = httpd_req_recv(req, body, sizeof(body) - 1);
    if (received <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing body");
        return ESP_FAIL;
    }
    body[received] = '\0';

    cJSON *root = cJSON_Parse(body);
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    const cJSON *state_json = cJSON_GetObjectItemCaseSensitive(root, "state");
    if (!cJSON_IsString(state_json) || !state_json->valuestring) {
        cJSON_Delete(root);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing \"state\" field");
        return ESP_FAIL;
    }

    display_state_id_t id = state_name_to_id(state_json->valuestring);
    cJSON_Delete(root);

    if (id == DISPLAY_STATE_MAX) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Unknown state");
        return ESP_FAIL;
    }

    display_event_t evt = {};
    evt.type = DISPLAY_EVT_FORCE_STATE;
    evt.force_state.state = id;
    display_fsm_send_event(&evt);

    ESP_LOGI(TAG, "Force display state: %d", id);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"status\":\"ok\"}\n", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

/**
 * @brief Push a surprise message layout.
 * POST /api/display/surprise — body is JSON layout DSL
 */
static esp_err_t display_surprise_handler(httpd_req_t *req)
{
    if (req->content_len == 0 || req->content_len > 8192) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                            "Body required (max 8 KB)");
        return ESP_FAIL;
    }

    // Allocate in SPIRAM — FSM takes ownership
    char *json_buf = heap_caps_malloc(req->content_len + 1, MALLOC_CAP_SPIRAM);
    if (!json_buf) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return ESP_FAIL;
    }

    size_t total = 0;
    while (total < req->content_len) {
        int r = httpd_req_recv(req, json_buf + total, req->content_len - total);
        if (r == HTTPD_SOCK_ERR_TIMEOUT) continue;
        if (r <= 0) {
            free(json_buf);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Receive failed");
            return ESP_FAIL;
        }
        total += (size_t)r;
    }
    json_buf[total] = '\0';

    // Parse duration_s from JSON (default 30s)
    uint32_t duration_s = 30;
    cJSON *root = cJSON_Parse(json_buf);
    if (root) {
        const cJSON *dur = cJSON_GetObjectItemCaseSensitive(root, "duration_s");
        if (cJSON_IsNumber(dur) && dur->valueint > 0) {
            duration_s = (uint32_t)dur->valueint;
        }
        cJSON_Delete(root);
    }

    display_event_t evt = {};
    evt.type = DISPLAY_EVT_SURPRISE_MESSAGE;
    evt.surprise.json_str = json_buf;  // FSM takes ownership
    evt.surprise.duration_s = duration_s;
    display_fsm_send_event(&evt);

    ESP_LOGI(TAG, "Surprise message pushed (%zu bytes, %lus)", total, (unsigned long)duration_s);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"status\":\"ok\"}\n", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// =============================================================================
// Schedule endpoints
// =============================================================================

/**
 * @brief Get display schedule configuration.
 * GET /api/display/schedule
 */
static esp_err_t schedule_get_handler(httpd_req_t *req)
{
    clock_settings_t cfg;
    settings_load(&cfg);

    char response[512];
    snprintf(response, sizeof(response),
             "{\"weather_show_s\":%u,"
             "\"weather_cooldown_s\":%u,"
             "\"radar_show_s\":%u,"
             "\"radar_cooldown_s\":%u,"
             "\"astro_show_s\":%u,"
             "\"astro_cooldown_s\":%u,"
             "\"photos_interval_s\":%u,"
             "\"photos_show_s\":%u,"
             "\"ambient_interval_s\":%u,"
             "\"ambient_show_s\":%u}\n",
             cfg.weather_show_s, cfg.weather_cooldown_s,
             cfg.radar_show_s, cfg.radar_cooldown_s,
             cfg.astro_show_s, cfg.astro_cooldown_s,
             cfg.photos_interval_s, cfg.photos_show_s,
             cfg.ambient_interval_s, cfg.ambient_show_s);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

/**
 * @brief Update display schedule configuration.
 * POST /api/display/schedule — body: {"weather_show_s":30, ...}
 * Only provided fields are updated; others keep their current value.
 */
// Copy a uint16 JSON field into *out if present and numeric.
static void json_copy_u16(const cJSON *root, const char *key, uint16_t *out)
{
    const cJSON *j = cJSON_GetObjectItem(root, key);
    if (j && cJSON_IsNumber(j)) *out = (uint16_t)j->valueint;
}

// Overlay any present schedule timing fields from JSON onto cfg.
static void parse_schedule_fields(const cJSON *root, clock_settings_t *cfg)
{
    json_copy_u16(root, "weather_show_s",     &cfg->weather_show_s);
    json_copy_u16(root, "weather_cooldown_s", &cfg->weather_cooldown_s);
    json_copy_u16(root, "radar_show_s",       &cfg->radar_show_s);
    json_copy_u16(root, "radar_cooldown_s",   &cfg->radar_cooldown_s);
    json_copy_u16(root, "astro_show_s",       &cfg->astro_show_s);
    json_copy_u16(root, "astro_cooldown_s",   &cfg->astro_cooldown_s);
    json_copy_u16(root, "photos_interval_s",  &cfg->photos_interval_s);
    json_copy_u16(root, "photos_show_s",      &cfg->photos_show_s);
    json_copy_u16(root, "ambient_interval_s", &cfg->ambient_interval_s);
    json_copy_u16(root, "ambient_show_s",     &cfg->ambient_show_s);
}

// Push each state's updated duration/cooldown to the FSM scheduler.
static void notify_schedule_config(const clock_settings_t *cfg)
{
    struct { display_state_id_t id; uint16_t show_s; uint16_t cooldown_s; } sched[] = {
        { DISPLAY_STATE_WEATHER,   cfg->weather_show_s,   cfg->weather_cooldown_s },
        { DISPLAY_STATE_RADAR,     cfg->radar_show_s,     cfg->radar_cooldown_s },
        { DISPLAY_STATE_ASTRONOMY, cfg->astro_show_s,     cfg->astro_cooldown_s },
        { DISPLAY_STATE_PHOTOS,    cfg->photos_show_s,    cfg->photos_interval_s },
        { DISPLAY_STATE_AMBIENT,   cfg->ambient_show_s,   cfg->ambient_interval_s },
    };
    for (size_t i = 0; i < sizeof(sched) / sizeof(sched[0]); i++) {
        display_event_t evt = {};
        evt.type = DISPLAY_EVT_SCHEDULE_CONFIG;
        evt.schedule.state = sched[i].id;
        evt.schedule.display_duration_ms = (uint32_t)sched[i].show_s * 1000;
        evt.schedule.cooldown_ms = (uint32_t)sched[i].cooldown_s * 1000;
        evt.schedule.enabled = true;
        display_fsm_send_event(&evt);
    }
}

static esp_err_t schedule_post_handler(httpd_req_t *req)
{
    char body[512];
    int received = httpd_req_recv(req, body, sizeof(body) - 1);
    if (received <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing body");
        return ESP_FAIL;
    }
    body[received] = '\0';

    cJSON *root = cJSON_Parse(body);
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    clock_settings_t cfg;
    settings_load(&cfg);
    parse_schedule_fields(root, &cfg);
    cJSON_Delete(root);

    esp_err_t err = settings_save(&cfg);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to save settings");
        return ESP_FAIL;
    }

    notify_schedule_config(&cfg);

    ESP_LOGI(TAG, "Schedule config updated");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"status\":\"ok\"}\n", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// =============================================================================
// Asset management endpoints
// =============================================================================

/**
 * @brief List contents of a single SD card directory, appending to chunked response.
 */
static void assets_list_dir(httpd_req_t *req, const char *sd_path,
                            const char *category, bool *first_entry)
{
    DIR *dir = opendir(sd_path);
    if (!dir) return;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;

        char full[300];
        snprintf(full, sizeof(full), "%s/%s", sd_path, entry->d_name);
        struct stat st;
        int sz = 0;
        if (stat(full, &st) == 0) sz = (int)st.st_size;

        char buf[384];
        snprintf(buf, sizeof(buf),
                 "%s{\"name\":\"%s\",\"category\":\"%s\",\"size\":%d}",
                 *first_entry ? "" : ",\n",
                 entry->d_name, category, sz);
        httpd_resp_sendstr_chunk(req, buf);
        *first_entry = false;
    }
    closedir(dir);
}

/**
 * @brief List asset files across known SD card directories.
 * GET /api/assets/list → {"assets":[{"name":"clear.json","category":"lottie","size":1234},...]}
 */
static esp_err_t assets_list_handler(httpd_req_t *req)
{
    if (!sdcard_is_mounted()) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "SD card not mounted");
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr_chunk(req, "{\"assets\":[\n");

    bool first = true;
    assets_list_dir(req, "/sdcard/lottie", "lottie", &first);
    assets_list_dir(req, "/sdcard/backgrounds", "backgrounds", &first);
    assets_list_dir(req, "/sdcard/photos", "photos", &first);
    assets_list_dir(req, "/sdcard/www", "www", &first);

    httpd_resp_sendstr_chunk(req, "\n]}\n");
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

/**
 * @brief Upload an asset file to the SD card.
 * POST /api/assets/upload?path=lottie/weather/day/clear.json
 * Body is raw file content.
 */
static esp_err_t assets_upload_handler(httpd_req_t *req)
{
    // Get destination path from query parameter
    char query[256] = {0};
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                            "Missing ?path= query parameter");
        return ESP_FAIL;
    }
    char rel_path[200] = {0};
    if (httpd_query_key_value(query, "path", rel_path, sizeof(rel_path)) != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                            "Missing ?path= query parameter");
        return ESP_FAIL;
    }

    // Build absolute path and reuse upload helpers
    char filepath[256];
    snprintf(filepath, sizeof(filepath), "/sdcard/%s", rel_path);
    ESP_LOGI(TAG, "Asset upload: %s (%d bytes)", filepath, req->content_len);

    esp_err_t err = upload_check_preconditions(req);
    if (err != ESP_OK) return err;
    upload_ensure_dir(filepath);

    size_t total = 0;
    err = upload_open_and_receive(req, filepath, &total);
    if (err == ESP_OK) upload_send_success(req, filepath, total);
    return err;
}

// =============================================================================
// Device settings endpoints
// =============================================================================

/**
 * @brief Get device settings (brightness, text color, background).
 * GET /api/settings
 */
static esp_err_t settings_get_handler(httpd_req_t *req)
{
    clock_settings_t cfg;
    settings_load(&cfg);

    char response[384];
    snprintf(response, sizeof(response),
             "{\"brightness\":%u,"
             "\"text_color\":\"#%06lx\","
             "\"background_image\":\"%s\"}\n",
             cfg.brightness,
             (unsigned long)cfg.text_color,
             cfg.background_image);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

/**
 * @brief Update device settings.
 * POST /api/settings — body: {"brightness":75, "text_color":"#ff0000", "background_image":"A:/bg.png"}
 */
static esp_err_t settings_post_handler(httpd_req_t *req)
{
    char body[384];
    int received = httpd_req_recv(req, body, sizeof(body) - 1);
    if (received <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing body");
        return ESP_FAIL;
    }
    body[received] = '\0';

    cJSON *root = cJSON_Parse(body);
    if (!root) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    clock_settings_t cfg;
    settings_load(&cfg);

    const cJSON *j;
    if ((j = cJSON_GetObjectItem(root, "brightness")) && cJSON_IsNumber(j)) {
        int v = j->valueint;
        if (v >= 0 && v <= 100) cfg.brightness = (uint8_t)v;
    }
    if ((j = cJSON_GetObjectItem(root, "text_color")) && cJSON_IsString(j)) {
        // Parse "#RRGGBB" hex string
        const char *s = j->valuestring;
        if (s[0] == '#' && strlen(s) == 7) {
            cfg.text_color = (uint32_t)strtoul(s + 1, NULL, 16);
        }
    }
    if ((j = cJSON_GetObjectItem(root, "background_image")) && cJSON_IsString(j)) {
        strncpy(cfg.background_image, j->valuestring, sizeof(cfg.background_image) - 1);
        cfg.background_image[sizeof(cfg.background_image) - 1] = '\0';
    }

    cJSON_Delete(root);

    esp_err_t err = settings_save(&cfg);
    if (err != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to save settings");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Settings updated: brightness=%u text_color=#%06lx bg=%s",
             cfg.brightness, (unsigned long)cfg.text_color, cfg.background_image);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"status\":\"ok\"}\n", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// =============================================================================
// Weather data endpoints
// =============================================================================

/**
 * @brief Serialize conditions to JSON and send.
 * GET /api/weather/current
 */
static esp_err_t weather_current_handler(httpd_req_t *req)
{
    const nws_conditions_t *c = nws_get_conditions();
    if (!c || !c->valid) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"valid\":false}\n", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    char response[512];
    snprintf(response, sizeof(response),
             "{\"valid\":true,"
             "\"temp_c\":%.1f,"
             "\"feels_like_c\":%.1f,"
             "\"humidity\":%d,"
             "\"wind_speed_kmh\":%.1f,"
             "\"wind_dir\":\"%s\","
             "\"pressure_hpa\":%.1f,"
             "\"description\":\"%s\","
             "\"station\":\"%s\"}\n",
             c->temp_c, c->feels_like_c, c->humidity,
             c->wind_speed_kmh, c->wind_dir_cardinal,
             c->pressure_hpa, c->description, c->station_id);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

/**
 * @brief Serialize forecast periods to JSON and send.
 * GET /api/weather/forecast
 */
static esp_err_t weather_forecast_handler(httpd_req_t *req)
{
    const nws_forecast_t *fc = nws_get_forecast();
    if (!fc || !fc->valid) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"valid\":false}\n", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr_chunk(req, "{\"valid\":true,\"periods\":[\n");

    for (int i = 0; i < fc->period_count; i++) {
        const nws_forecast_period_t *p = &fc->periods[i];
        char buf[256];
        snprintf(buf, sizeof(buf),
                 "%s{\"name\":\"%s\",\"temp\":%d,\"unit\":\"%c\","
                 "\"short\":\"%s\",\"wind\":\"%s %s\",\"daytime\":%s}",
                 i > 0 ? ",\n" : "",
                 p->name, p->temperature, p->temp_unit,
                 p->short_forecast, p->wind_speed, p->wind_direction,
                 p->is_daytime ? "true" : "false");
        httpd_resp_sendstr_chunk(req, buf);
    }

    httpd_resp_sendstr_chunk(req, "\n]}\n");
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

/**
 * @brief Serialize active alerts to JSON and send.
 * GET /api/weather/alerts
 */
static esp_err_t weather_alerts_handler(httpd_req_t *req)
{
    const nws_alerts_t *al = nws_get_alerts();
    if (!al || !al->valid) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"valid\":false,\"count\":0}\n", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    httpd_resp_set_type(req, "application/json");

    char header[64];
    snprintf(header, sizeof(header), "{\"valid\":true,\"count\":%d,\"alerts\":[\n",
             al->alert_count);
    httpd_resp_sendstr_chunk(req, header);

    for (int i = 0; i < al->alert_count; i++) {
        const nws_alert_t *a = &al->alerts[i];
        char buf[512];
        snprintf(buf, sizeof(buf),
                 "%s{\"event\":\"%s\",\"severity\":\"%s\","
                 "\"urgency\":\"%s\",\"headline\":\"%.*s\"}",
                 i > 0 ? ",\n" : "",
                 a->event, a->severity, a->urgency,
                 200, a->headline);  // truncate headline for JSON safety
        httpd_resp_sendstr_chunk(req, buf);
    }

    httpd_resp_sendstr_chunk(req, "\n]}\n");
    httpd_resp_send_chunk(req, NULL, 0);
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
