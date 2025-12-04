// components/http_api/http_api.c

#include "http_api.h"
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
#define MAX_UPLOAD_SIZE (10 * 1024 * 1024)
#define UPLOAD_BUFFER_SIZE (4 * 1024)

/**
 * @brief Upload file to SD card
 * POST /files/[path]
 * Body: raw file data (application/octet-stream)
 */
static esp_err_t upload_file_handler(httpd_req_t *req) {
    char filepath[256];
    size_t buf_len;

    // Get URI path (remove /files/ prefix)
    const char* uri = req->uri + 7;  // Skip "/files/"
    snprintf(filepath, sizeof(filepath), "/sdcard/%s", uri);

    ESP_LOGI(TAG, "Upload request: %s -> %s", req->uri, filepath);

    // Check SD card
    if (!sdcard_is_mounted()) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "SD card not mounted");
        return ESP_FAIL;
    }

    // Check content length
    if (req->content_len > MAX_UPLOAD_SIZE) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "File too large (max 10 MB)");
        return ESP_FAIL;
    }

    // Create directory if needed
    char dirpath[256];
    strncpy(dirpath, filepath, sizeof(dirpath));
    char* last_slash = strrchr(dirpath, '/');
    if (last_slash) {
        *last_slash = '\0';
        mkdir(dirpath, 0755);  // Recursive mkdir not implemented, user must create dirs first
    }

    // Open file for writing
    FILE* f = fopen(filepath, "wb");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open file for writing: %s", filepath);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to create file");
        return ESP_FAIL;
    }

    // Receive and write data
    char buf[UPLOAD_BUFFER_SIZE];
    size_t total_received = 0;
    int received;

    while (total_received < req->content_len) {
        received = httpd_req_recv(req, buf, MIN(sizeof(buf), req->content_len - total_received));
        if (received <= 0) {
            if (received == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;
            }
            fclose(f);
            unlink(filepath);  // Delete partial file
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Upload interrupted");
            return ESP_FAIL;
        }

        fwrite(buf, 1, received, f);
        total_received += received;
    }

    fclose(f);

    ESP_LOGI(TAG, "File uploaded successfully: %s (%zu bytes)", filepath, total_received);

    // Send response
    char response[256];
    snprintf(response, sizeof(response),
             "{\"status\":\"success\",\"path\":\"%s\",\"size\":%zu}\n",
             filepath, total_received);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);

    return ESP_OK;
}

/**
 * @brief Download file from SD card or list directory
 * GET /files/[path]
 */
static esp_err_t download_file_handler(httpd_req_t *req) {
    char filepath[256];
    struct stat st;

    // Get URI path (remove /files/ prefix)
    const char* uri = req->uri + 7;  // Skip "/files/"

    // If empty path, list root directory
    if (strlen(uri) == 0) {
        uri = "";
    }

    snprintf(filepath, sizeof(filepath), "/sdcard/%s", uri);

    ESP_LOGI(TAG, "Download/list request: %s -> %s", req->uri, filepath);

    // Check SD card
    if (!sdcard_is_mounted()) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "SD card not mounted");
        return ESP_FAIL;
    }

    // Check if path exists
    if (stat(filepath, &st) != 0) {
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }

    // If directory, list contents
    if (S_ISDIR(st.st_mode)) {
        DIR* dir = opendir(filepath);
        if (!dir) {
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to open directory");
            return ESP_FAIL;
        }

        // Send JSON list
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr_chunk(req, "{\"files\":[\n");

        struct dirent* entry;
        bool first = true;
        while ((entry = readdir(dir)) != NULL) {
            if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
                continue;
            }

            char entry_path[512];
            snprintf(entry_path, sizeof(entry_path), "%s/%s", filepath, entry->d_name);

            struct stat entry_st;
            if (stat(entry_path, &entry_st) == 0) {
                if (!first) httpd_resp_sendstr_chunk(req, ",\n");
                first = false;

                char json_entry[256];
                snprintf(json_entry, sizeof(json_entry),
                         "{\"name\":\"%s\",\"size\":%ld,\"type\":\"%s\"}",
                         entry->d_name,
                         entry_st.st_size,
                         S_ISDIR(entry_st.st_mode) ? "dir" : "file");
                httpd_resp_sendstr_chunk(req, json_entry);
            }
        }

        closedir(dir);
        httpd_resp_sendstr_chunk(req, "\n]}\n");
        httpd_resp_send_chunk(req, NULL, 0);  // End of chunks

        return ESP_OK;
    }

    // If file, download it
    FILE* f = fopen(filepath, "rb");
    if (!f) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to open file");
        return ESP_FAIL;
    }

    // Set headers
    httpd_resp_set_type(req, "application/octet-stream");

    // Extract filename for Content-Disposition
    const char* filename = strrchr(filepath, '/');
    filename = filename ? filename + 1 : filepath;
    char content_disp[256];
    snprintf(content_disp, sizeof(content_disp), "attachment; filename=\"%s\"", filename);
    httpd_resp_set_hdr(req, "Content-Disposition", content_disp);

    // Stream file
    char buf[UPLOAD_BUFFER_SIZE];
    size_t len;
    while ((len = fread(buf, 1, sizeof(buf), f)) > 0) {
        if (httpd_resp_send_chunk(req, buf, len) != ESP_OK) {
            fclose(f);
            return ESP_FAIL;
        }
    }

    fclose(f);
    httpd_resp_send_chunk(req, NULL, 0);  // End of chunks

    ESP_LOGI(TAG, "File downloaded: %s", filepath);

    return ESP_OK;
}

/**
 * @brief Get basic status
 * GET /api/status
 */
static esp_err_t status_handler(httpd_req_t *req) {
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
 * @brief Test debug log writing
 * GET /api/logs/test
 */
static esp_err_t logs_test_handler(httpd_req_t *req) {
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
 * @brief Download debug log file
 * GET /api/logs/download
 */
static esp_err_t logs_download_handler(httpd_req_t *req) {
    // Flush log buffer before sending
    debug_log_flush();

    const char* log_path = debug_log_get_path();
    if (log_path == NULL) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Debug logging not active");
        return ESP_FAIL;
    }

    // Open log file
    FILE* f = fopen(log_path, "rb");
    if (!f) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to open log file");
        return ESP_FAIL;
    }

    // Set headers
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_set_hdr(req, "Content-Disposition", "attachment; filename=\"debug.log\"");

    // Stream file
    char buf[UPLOAD_BUFFER_SIZE];
    size_t len;
    while ((len = fread(buf, 1, sizeof(buf), f)) > 0) {
        if (httpd_resp_send_chunk(req, buf, len) != ESP_OK) {
            fclose(f);
            return ESP_FAIL;
        }
    }

    fclose(f);
    httpd_resp_send_chunk(req, NULL, 0);  // End of chunks

    ESP_LOGI(TAG, "Log file downloaded: %s", log_path);
    return ESP_OK;
}

// URI handlers
static const httpd_uri_t uri_upload = {
    .uri       = "/files/*",
    .method    = HTTP_POST,
    .handler   = upload_file_handler,
    .user_ctx  = NULL
};

static const httpd_uri_t uri_download = {
    .uri       = "/files/*",
    .method    = HTTP_GET,
    .handler   = download_file_handler,
    .user_ctx  = NULL
};

static const httpd_uri_t uri_status = {
    .uri       = "/api/status",
    .method    = HTTP_GET,
    .handler   = status_handler,
    .user_ctx  = NULL
};

static const httpd_uri_t uri_logs = {
    .uri       = "/api/logs/download",
    .method    = HTTP_GET,
    .handler   = logs_download_handler,
    .user_ctx  = NULL
};

static const httpd_uri_t uri_logs_test = {
    .uri       = "/api/logs/test",
    .method    = HTTP_GET,
    .handler   = logs_test_handler,
    .user_ctx  = NULL
};

esp_err_t http_api_start(void) {
    if (server != NULL) {
        ESP_LOGW(TAG, "HTTP server already running");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Starting HTTP API server on port 80");

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;
    config.max_uri_handlers = 16;
    config.stack_size = 8192;
    config.uri_match_fn = httpd_uri_match_wildcard;  // Enable wildcard matching

    esp_err_t ret = httpd_start(&server, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server: %s", esp_err_to_name(ret));
        return ret;
    }

    // Register URI handlers
    httpd_register_uri_handler(server, &uri_status);
    httpd_register_uri_handler(server, &uri_logs);
    httpd_register_uri_handler(server, &uri_logs_test);
    httpd_register_uri_handler(server, &uri_upload);
    httpd_register_uri_handler(server, &uri_download);

    ESP_LOGI(TAG, "HTTP API server started successfully");
    return ESP_OK;
}

esp_err_t http_api_stop(void) {
    if (server == NULL) {
        ESP_LOGW(TAG, "HTTP server not running");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Stopping HTTP API server");
    esp_err_t ret = httpd_stop(server);
    server = NULL;
    return ret;
}

bool http_api_is_running(void) {
    return (server != NULL);
}
