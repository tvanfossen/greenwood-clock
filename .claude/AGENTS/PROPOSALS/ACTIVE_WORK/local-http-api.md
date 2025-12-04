# Local HTTP API Server

**Status**: ACTIVE_WORK (Limited Scope)
**Priority**: CRITICAL (Required for remote debugging of mounted device)
**Estimated Effort**: Medium (2-3 days)
**Created**: 2025-12-03
**Activated**: 2025-12-03

## Critical Requirements

**Context**: Clock is permanently mounted - USB access requires physical dismount. Need remote debugging and file management capabilities.

**Phase 1 Focus (CRITICAL)**:
1. **SD Card File Upload**: POST /files/* to upload backgrounds, animations, configs
2. **Rolling Debug Logs**: 10 MB max per file, 5 files max, stored on SD card
3. **Log Download**: GET /api/logs/download to retrieve debug logs remotely
4. **File Management**: List/download SD card files via HTTP

## Problem Statement

Currently, debugging and retrieving data from the Greenwood Clock requires:
- **Serial connection** via USB for logs (requires physical access)
- **SD card removal** to access exported logs (requires reboot, physical access)
- **Touchscreen navigation** to view system info (limited display space)
- **No programmatic access** to device state, logs, or configuration

Users need a way to:
- Retrieve debug logs remotely without USB/serial
- Check device status and health metrics via web browser or scripts
- Download log files without removing SD card
- Access system information programmatically (for monitoring, automation)
- Potentially update settings via API (future enhancement)

## Proposed Solution

Add a lightweight HTTP server that exposes REST API endpoints for device information, logs, and diagnostics.

### Architecture

```
┌─────────────────────────────────────────────────────────┐
│                    Greenwood Clock                       │
│                                                          │
│  ┌────────────────────────────────────────────────────┐ │
│  │           HTTP Server (Port 80)                     │ │
│  │              esp_http_server                        │ │
│  ├────────────────────────────────────────────────────┤ │
│  │ GET  /api/status        Device status & health      │ │
│  │ GET  /api/logs          Live debug logs (JSON)      │ │
│  │ GET  /api/logs/download Download logs (text)        │ │
│  │ GET  /api/system        System info (memory, WiFi)  │ │
│  │ GET  /api/settings      Current settings (JSON)     │ │
│  │ POST /api/settings      Update settings (future)    │ │
│  │ GET  /files/*           SD card file browser        │ │
│  └────────────────────────────────────────────────────┘ │
│           ↕                                              │
│  ┌────────────────────────────────────────────────────┐ │
│  │  Components: debug_log, settings, network, sdcard  │ │
│  └────────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────────┘
                       ↕ HTTP
              ┌──────────────────┐
              │   Web Browser    │
              │   curl/wget      │
              │   Python script  │
              └──────────────────┘
```

### Component Structure

```
components/
└── http_api/
    ├── CMakeLists.txt
    ├── http_api.h
    ├── http_api.c
    ├── handlers/
    │   ├── status_handler.c      # GET /api/status
    │   ├── logs_handler.c        # GET /api/logs
    │   ├── system_handler.c      # GET /api/system
    │   ├── settings_handler.c    # GET/POST /api/settings
    │   └── files_handler.c       # GET /files/*
    └── utils/
        ├── json_builder.c        # JSON response helpers
        └── auth.c                # Optional: Basic auth
```

## API Endpoints

### 1. GET /api/status

**Description**: Device status and health check

**Response** (JSON):
```json
{
  "status": "ok",
  "uptime": 3600,
  "timestamp": "2025-12-03T10:30:00Z",
  "device": {
    "name": "Greenwood Clock",
    "version": "1.0.2",
    "partition": "ota_0"
  },
  "network": {
    "connected": true,
    "ssid": "MyNetwork",
    "ip": "192.168.1.18",
    "rssi": -45
  },
  "memory": {
    "free_heap": 27621332,
    "min_heap": 26800000,
    "largest_block": 12000000
  },
  "storage": {
    "spiffs": {
      "total": 3145728,
      "used": 1572864,
      "percent": 50
    },
    "sdcard": {
      "mounted": true,
      "total": 15930671104,
      "used": 52428800,
      "percent": 0
    }
  }
}
```

**Use Cases**:
- Health check endpoint for monitoring scripts
- Quick status overview without serial connection
- Integration with home automation systems

### 2. GET /api/logs

**Description**: Retrieve recent debug logs in JSON format

**Query Parameters**:
- `level` - Filter by log level (V, D, I, W, E) - default: all
- `limit` - Number of lines to return - default: 100, max: 500
- `tail` - Get last N lines - default: true

**Response** (JSON):
```json
{
  "logs": [
    {
      "timestamp": "2025-12-03T10:30:15",
      "level": "I",
      "tag": "main",
      "message": "SNTP sync complete"
    },
    {
      "timestamp": "2025-12-03T10:30:20",
      "level": "E",
      "tag": "weather",
      "message": "Failed to fetch weather: ESP_ERR_TIMEOUT"
    }
  ],
  "total": 2,
  "more": false
}
```

**Use Cases**:
- Remote debugging without serial connection
- Log aggregation into external monitoring tools
- Quick error checking via browser

### 3. GET /api/logs/download

**Description**: Download logs as plain text file

**Query Parameters**:
- `level` - Filter by log level
- `format` - `text` (default) or `json`

**Response** (text/plain):
```
2025-12-03 10:30:15 I main: SNTP sync complete
2025-12-03 10:30:20 E weather: Failed to fetch weather: ESP_ERR_TIMEOUT
...
```

**Headers**:
```
Content-Type: text/plain
Content-Disposition: attachment; filename="greenwood-clock-logs-20251203-103025.txt"
```

**Use Cases**:
- Download logs for offline analysis
- Share logs with support/developers
- Archive logs periodically

### 4. GET /api/system

**Description**: Detailed system information

**Response** (JSON):
```json
{
  "device": {
    "name": "Greenwood Clock",
    "model": "ESP32-P4 Function EV Board",
    "chip": "ESP32-P4",
    "mac": "AA:BB:CC:DD:EE:FF",
    "firmware": {
      "version": "1.0.2",
      "date": "Dec  2 2025",
      "time": "22:05:15",
      "partition": "ota_0"
    }
  },
  "network": {
    "interface": "wifi",
    "connected": true,
    "ssid": "MyNetwork",
    "ip": "192.168.1.18",
    "netmask": "255.255.255.0",
    "gateway": "192.168.1.1",
    "rssi": -45,
    "channel": 6
  },
  "memory": {
    "heap": {
      "free": 27621332,
      "min": 26800000,
      "largest_block": 12000000
    },
    "flash": {
      "size": 16777216,
      "used": 4194304
    }
  },
  "time": {
    "local": "2025-12-03T10:30:25-05:00",
    "utc": "2025-12-03T15:30:25Z",
    "timezone": "EST5EDT,M3.2.0/2,M11.1.0/2",
    "ntp_synced": true
  },
  "uptime": {
    "seconds": 3625,
    "formatted": "1h 0m 25s"
  }
}
```

**Use Cases**:
- Comprehensive system diagnostics
- Remote troubleshooting
- Inventory management (MAC address, firmware version)

### 5. GET /api/settings

**Description**: Retrieve current device settings

**Response** (JSON):
```json
{
  "wifi": {
    "ssid": "MyNetwork",
    "hostname": "greenwood-clock"
  },
  "location": {
    "latitude": 43.366,
    "longitude": -85.851,
    "timezone": "EST5EDT,M3.2.0/2,M11.1.0/2"
  },
  "display": {
    "brightness": 10,
    "touch_enabled": true
  },
  "features": {
    "weather_enabled": true
  },
  "ota": {
    "server_url": "http://192.168.1.96:8000"
  }
}
```

**Note**: WiFi password is NOT included for security.

### 6. POST /api/settings (Future)

**Description**: Update device settings

**Request Body** (JSON):
```json
{
  "display": {
    "brightness": 50
  }
}
```

**Response** (JSON):
```json
{
  "status": "success",
  "message": "Settings updated",
  "restart_required": false
}
```

### 7. GET /files/*

**Description**: Browse and download files from SD card

**Examples**:
- `/files/` - List root directory
- `/files/logs/` - List logs directory
- `/files/logs/debug_20251203_103025.txt` - Download specific log file
- `/files/backgrounds/sunset.png` - Download background image

**Response** (for directory listing):
```json
{
  "path": "/logs/",
  "files": [
    {
      "name": "debug_20251203_103025.txt",
      "size": 12345,
      "modified": "2025-12-03T10:30:25Z",
      "type": "file"
    },
    {
      "name": "screenshots",
      "type": "directory"
    }
  ]
}
```

**Response** (for file download):
- Binary file content with appropriate `Content-Type`
- `Content-Disposition: attachment` header

### 8. POST /files/* (CRITICAL - Phase 1)

**Description**: Upload files to SD card (backgrounds, animations, configs)

**Examples**:
- `POST /files/backgrounds/sunset.png` - Upload background image
- `POST /files/animations/spinner.json` - Upload Lottie animation

**Request**:
- `Content-Type: multipart/form-data` or `application/octet-stream`
- File data in request body

**Response** (JSON):
```json
{
  "status": "success",
  "path": "/sdcard/backgrounds/sunset.png",
  "size": 417824,
  "message": "File uploaded successfully"
}
```

**Security**:
- Restrict to `/sdcard/` paths only
- Validate file extensions (png, jpg, json only)
- Size limits: 10 MB per file
- Create directories automatically if needed

**Use Cases**:
- **CRITICAL**: Upload splash.png to enable SPIFFS-based background loading
- **CRITICAL**: Upload Lottie animations without USB flashing
- Update backgrounds remotely
- Deploy new configurations

## Rolling Debug Logs (CRITICAL - Phase 1)

### Log File Management

**Storage Location**: `/sdcard/logs/`

**File Naming**: `debug_YYYYMMDD_HHMMSS.log`
- Example: `debug_20251203_143025.log`

**Rolling Policy**:
- **Max file size**: 10 MB per log file
- **Max files**: 5 log files total
- **Behavior**: When limit reached, delete oldest log file

**Log Format** (plain text):
```
2025-12-03 14:30:25.123 I [main] SNTP sync complete
2025-12-03 14:30:30.456 E [weather] Failed to fetch: ESP_ERR_TIMEOUT
2025-12-03 14:30:35.789 W [network] WiFi signal weak: RSSI=-75
```

### Implementation Strategy

**Component**: `components/debug_log/`

```c
// debug_log.h

#ifndef DEBUG_LOG_H
#define DEBUG_LOG_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize debug logging to SD card
 * @return ESP_OK on success
 */
esp_err_t debug_log_init(void);

/**
 * @brief Write log entry to SD card
 * @param level Log level (V, D, I, W, E)
 * @param tag Tag string
 * @param message Log message
 */
void debug_log_write(const char* level, const char* tag, const char* message);

/**
 * @brief Get current log file path
 * @return Path to current log file
 */
const char* debug_log_get_current_file(void);

/**
 * @brief List all log files
 * @param files Array to store file paths
 * @param max_files Maximum number of files to return
 * @return Number of files found
 */
int debug_log_list_files(char** files, int max_files);

/**
 * @brief Delete oldest log file
 * @return ESP_OK on success
 */
esp_err_t debug_log_rotate(void);

#ifdef __cplusplus
}
#endif

#endif // DEBUG_LOG_H
```

**Integration with ESP-IDF Logging**:

Use `esp_log_set_vprintf()` to intercept all ESP_LOGx() calls and write to SD card:

```c
// debug_log.c

static FILE* log_file = NULL;
static size_t current_log_size = 0;
static char current_log_path[128];

#define MAX_LOG_SIZE (10 * 1024 * 1024)  // 10 MB
#define MAX_LOG_FILES 5

static int custom_vprintf(const char* format, va_list args) {
    // Write to serial (original behavior)
    int ret = vprintf(format, args);

    // Also write to SD card
    if (log_file && sdcard_is_mounted()) {
        vfprintf(log_file, format, args);
        fflush(log_file);

        // Check file size and rotate if needed
        current_log_size = ftell(log_file);
        if (current_log_size >= MAX_LOG_SIZE) {
            debug_log_rotate();
        }
    }

    return ret;
}

esp_err_t debug_log_init(void) {
    if (!sdcard_is_mounted()) {
        ESP_LOGW(TAG, "SD card not mounted, debug logging disabled");
        return ESP_ERR_INVALID_STATE;
    }

    // Create logs directory
    mkdir("/sdcard/logs", 0755);

    // Generate log filename with timestamp
    time_t now;
    struct tm ti;
    time(&now);
    localtime_r(&now, &ti);

    snprintf(current_log_path, sizeof(current_log_path),
             "/sdcard/logs/debug_%04d%02d%02d_%02d%02d%02d.log",
             ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday,
             ti.tm_hour, ti.tm_min, ti.tm_sec);

    // Open log file
    log_file = fopen(current_log_path, "a");
    if (!log_file) {
        ESP_LOGE(TAG, "Failed to open log file: %s", current_log_path);
        return ESP_FAIL;
    }

    // Set custom printf handler
    esp_log_set_vprintf(custom_vprintf);

    ESP_LOGI(TAG, "Debug logging to SD card enabled: %s", current_log_path);
    return ESP_OK;
}

esp_err_t debug_log_rotate(void) {
    // Close current log file
    if (log_file) {
        fclose(log_file);
        log_file = NULL;
    }

    // Count existing log files
    DIR* dir = opendir("/sdcard/logs");
    if (!dir) return ESP_FAIL;

    struct dirent* entry;
    int log_count = 0;
    char oldest_log[128] = {0};
    time_t oldest_time = LONG_MAX;

    while ((entry = readdir(dir)) != NULL) {
        if (strstr(entry->d_name, "debug_") && strstr(entry->d_name, ".log")) {
            log_count++;

            // Track oldest file for deletion
            struct stat st;
            char path[256];
            snprintf(path, sizeof(path), "/sdcard/logs/%s", entry->d_name);
            if (stat(path, &st) == 0 && st.st_mtime < oldest_time) {
                oldest_time = st.st_mtime;
                strncpy(oldest_log, path, sizeof(oldest_log));
            }
        }
    }
    closedir(dir);

    // Delete oldest if we have too many
    if (log_count >= MAX_LOG_FILES && oldest_log[0]) {
        ESP_LOGI(TAG, "Deleting oldest log: %s", oldest_log);
        unlink(oldest_log);
    }

    // Start new log file
    return debug_log_init();
}
```

**HTTP Integration**:

```c
// GET /api/logs/download - Download current log file
static esp_err_t logs_download_handler(httpd_req_t *req) {
    const char* log_path = debug_log_get_current_file();

    FILE* f = fopen(log_path, "r");
    if (!f) {
        httpd_resp_send_404(req);
        return ESP_FAIL;
    }

    // Set headers
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_set_hdr(req, "Content-Disposition",
                       "attachment; filename=\"greenwood-clock-debug.log\"");

    // Stream file content
    char buf[512];
    size_t len;
    while ((len = fread(buf, 1, sizeof(buf), f)) > 0) {
        httpd_resp_send_chunk(req, buf, len);
    }

    fclose(f);
    httpd_resp_send_chunk(req, NULL, 0);  // End of chunks
    return ESP_OK;
}
```

## Implementation

### Phase 1: Core HTTP Server + Critical Features (2 days)

**http_api.h**:
```c
#ifndef HTTP_API_H
#define HTTP_API_H

#include "esp_err.h"
#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Start HTTP API server
 * @return ESP_OK on success
 */
esp_err_t http_api_start(void);

/**
 * @brief Stop HTTP API server
 * @return ESP_OK on success
 */
esp_err_t http_api_stop(void);

/**
 * @brief Check if HTTP API server is running
 * @return true if running
 */
bool http_api_is_running(void);

#ifdef __cplusplus
}
#endif

#endif // HTTP_API_H
```

**http_api.c**:
```c
#include "http_api.h"
#include "esp_log.h"
#include "esp_http_server.h"

static const char* TAG = "http_api";
static httpd_handle_t server = NULL;

// Forward declarations of handlers
static esp_err_t status_handler(httpd_req_t *req);
static esp_err_t logs_handler(httpd_req_t *req);
static esp_err_t system_handler(httpd_req_t *req);

static const httpd_uri_t uri_status = {
    .uri       = "/api/status",
    .method    = HTTP_GET,
    .handler   = status_handler,
    .user_ctx  = NULL
};

static const httpd_uri_t uri_logs = {
    .uri       = "/api/logs",
    .method    = HTTP_GET,
    .handler   = logs_handler,
    .user_ctx  = NULL
};

static const httpd_uri_t uri_system = {
    .uri       = "/api/system",
    .method    = HTTP_GET,
    .handler   = system_handler,
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

    esp_err_t ret = httpd_start(&server, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server: %s", esp_err_to_name(ret));
        return ret;
    }

    // Register URI handlers
    httpd_register_uri_handler(server, &uri_status);
    httpd_register_uri_handler(server, &uri_logs);
    httpd_register_uri_handler(server, &uri_system);

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

// Handler implementations
static esp_err_t status_handler(httpd_req_t *req) {
    // Build JSON response
    char response[512];
    snprintf(response, sizeof(response),
        "{\n"
        "  \"status\": \"ok\",\n"
        "  \"uptime\": %lu,\n"
        "  \"memory\": {\n"
        "    \"free_heap\": %lu\n"
        "  }\n"
        "}\n",
        (unsigned long)(esp_timer_get_time() / 1000000),
        (unsigned long)esp_get_free_heap_size()
    );

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t logs_handler(httpd_req_t *req) {
    // Integrate with debug_log component
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"logs\": [], \"total\": 0}", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

static esp_err_t system_handler(httpd_req_t *req) {
    // Integrate with system info
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"device\": {\"name\": \"Greenwood Clock\"}}", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}
```

### Phase 2: Endpoint Implementations (1 day)

Implement detailed handlers for each endpoint, integrating with existing components:
- `status_handler.c` - Device status from various components
- `logs_handler.c` - Integration with debug_log component
- `system_handler.c` - Comprehensive system information
- `settings_handler.c` - Integration with settings component

### Phase 3: SD Card File Browser (0.5 days)

Implement file browsing and download from SD card:
- Directory listing (JSON)
- File download (binary with appropriate MIME type)
- Security: Restrict to `/sdcard/` only (no access to root filesystem)

### Phase 4: Optional Enhancements (0.5 days)

- **Basic Authentication**: Protect endpoints with username/password
- **CORS Headers**: Allow web apps to access the API
- **Rate Limiting**: Prevent abuse
- **WebSocket Endpoint**: Real-time log streaming

## Integration with Main Application

```c
// main/main.cpp

#include "http_api.h"

extern "C" void app_main() {
    // ... existing init code ...

    // Start HTTP API server after WiFi connects
    if (network_is_connected()) {
        ESP_LOGI(TAG, "[7] Starting HTTP API server...");
        http_api_start();

        esp_netif_t* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        esp_netif_ip_info_t ip_info;
        if (netif && esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
            ESP_LOGI(TAG, "[7] HTTP API available at: http://" IPSTR "/api/status",
                     IP2STR(&ip_info.ip));
        }
    }

    // ... rest of init ...
}
```

## Usage Examples

### curl - Check Status
```bash
curl http://192.168.1.18/api/status
```

### curl - Download Logs
```bash
curl http://192.168.1.18/api/logs/download -o logs.txt
```

### Python - Monitor Device
```python
import requests
import json

response = requests.get('http://192.168.1.18/api/status')
status = response.json()

print(f"Device: {status['device']['name']}")
print(f"Uptime: {status['uptime']}s")
print(f"Free Heap: {status['memory']['free_heap']} bytes")
```

### Browser - View Logs
```
http://192.168.1.18/api/logs
```

### Home Assistant Integration
```yaml
sensor:
  - platform: rest
    name: Greenwood Clock Heap
    resource: http://192.168.1.18/api/status
    value_template: '{{ value_json.memory.free_heap }}'
    unit_of_measurement: 'bytes'
    scan_interval: 60
```

## Security Considerations

### Current (MVP)
- **No authentication** - Assumes trusted local network
- **Read-only APIs** - No ability to change settings (except future POST /api/settings)
- **Local network only** - Not exposed to internet

### Future Enhancements
- **Basic Authentication**: Username/password for all endpoints
- **HTTPS**: Encrypted communication (requires certificates)
- **API Keys**: Token-based authentication
- **IP Whitelist**: Restrict access to specific IPs
- **Disable via settings**: Allow user to turn off HTTP server

## Performance Impact

**Memory**:
- HTTP server: ~16 KB RAM
- Handler code: ~30 KB flash
- Stack per request: 8 KB

**CPU**:
- Minimal when idle
- ~5% CPU during active request

**Network**:
- Minimal bandwidth (<1 KB/s idle)
- Burst traffic during log downloads

## Testing Plan

### Unit Tests
- Each endpoint returns valid JSON
- Error handling (malformed requests, missing components)
- Query parameter parsing

### Integration Tests
1. **Status Endpoint**: Verify all fields populated correctly
2. **Logs Endpoint**: Retrieve logs, verify format
3. **System Endpoint**: Check all system info fields
4. **File Browser**: List directories, download files from SD card
5. **Concurrent Requests**: Multiple simultaneous API calls

### Manual Testing
- Access via browser from PC/phone
- Use curl to fetch data
- Test with WiFi disconnected (should fail gracefully)
- Test with SD card removed (file browser should show error)

## Success Metrics

- HTTP server starts successfully on boot
- All endpoints respond within 500ms
- Logs can be retrieved remotely without serial connection
- SD card files can be downloaded via browser
- No crashes or memory leaks under load
- Integration with monitoring tools works

## Future Enhancements

### Phase 5: Web Dashboard
- Serve HTML/CSS/JS files for a web UI
- Real-time log viewer with filtering
- Settings editor (brightness, timezone, etc.)
- Firmware upload for OTA updates

### Phase 6: WebSocket Support
- Real-time log streaming (no polling)
- Live system metrics updates
- Push notifications for errors

### Phase 7: REST API for Settings
- POST /api/settings - Update settings remotely
- POST /api/restart - Remote reboot
- POST /api/ota - Trigger OTA update

## Dependencies

- `esp_http_server` - ESP-IDF HTTP server component
- Existing components: `debug_log`, `settings`, `network`, `sdcard`
- Optional: `cJSON` for JSON building/parsing

## Risks and Mitigations

| Risk | Impact | Mitigation |
|------|--------|------------|
| Memory overhead | Increased RAM usage | Use 8KB stack, limit concurrent connections |
| Security vulnerability | Unauthorized access | Start with read-only, add auth later |
| Network congestion | Slow responses | Rate limiting, connection limits |
| Crash during request | Service unavailable | Proper error handling, watchdog timers |

## References

- [ESP-IDF HTTP Server](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/protocols/esp_http_server.html)
- [RESTful API Best Practices](https://restfulapi.net/)
- Related proposals: `debug-log-viewer.md`, `sd-card-integration.md`

---

**Next Steps After Approval:**
1. Implement basic HTTP server with `/api/status` endpoint
2. Add `/api/logs` integration with debug_log component
3. Implement `/api/system` with comprehensive device info
4. Add SD card file browser (`/files/*`)
5. Test on hardware with various tools (curl, browser, Python)
6. Document API endpoints for users
