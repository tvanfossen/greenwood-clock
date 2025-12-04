---
proposal_id: "local-network-ota"
title: "Greenwood Clock - Local Network OTA Updates"
github_issue: null
created: "2025-12-02"
updated: "2025-12-02"

status: "STAGED"
priority: "medium"
complexity: "medium"

category: ["feature", "infrastructure"]
tags: ["esp32", "ota", "firmware", "updates", "http"]

estimated_hours: 8
actual_hours: 6
progress_percent: 100

depends_on: []
blocks: []

commits: []
branches: []
pull_requests: []

agent_notes: []

stall_reason: null
unblock_requirements: []

completion_date: null
verification_status: "pending"
---

# Greenwood Clock - Local Network OTA Updates

## Problem Statement

Currently, firmware updates require physical access to the device to flash via USB/UART connection. This is inconvenient during development and impractical for deployed devices. Each firmware update requires:

1. Physical access to device
2. USB cable connection
3. Running `idf.py flash` command
4. Device downtime during flashing

For a clock device that may be wall-mounted or in a difficult-to-access location, this becomes a significant maintenance burden.

## Proposed Solution

Implement Over-The-Air (OTA) firmware updates over the local network:

- **Development Workflow**: Flash new firmware from dev machine to device over WiFi
- **HTTP Server**: Simple HTTP server on dev machine serves firmware binary
- **ESP32 OTA Client**: Device downloads and flashes firmware from local server
- **Rollback Support**: Automatic rollback to previous firmware if new version fails
- **Security**: Basic authentication and firmware verification
- **UI Integration**: Trigger OTA updates via touchscreen settings menu

## Current State

### OTA Partition Layout
The project uses ESP-IDF's default partition scheme which may or may not include OTA partitions. Need to verify:
- Check `partitions.csv` for OTA partition definitions
- Typically needs two OTA partitions (factory, ota_0, ota_1)
- Current partition table may need modification

### ESP-IDF OTA Support
- ESP-IDF provides `esp_https_ota` component
- Support for HTTP/HTTPS firmware downloads
- Built-in rollback mechanism
- Partition switching after successful update

## Implementation Plan

### Phase 1: Partition Table Setup
- [ ] Review current partition table (`partitions.csv`)
- [ ] Create OTA partition layout:
  ```
  # Name,   Type, SubType, Offset,  Size, Flags
  nvs,      data, nvs,     0x9000,  0x6000,
  phy_init, data, phy,     0xf000,  0x1000,
  factory,  app,  factory, 0x10000, 0x400000,
  ota_0,    app,  ota_0,   ,        0x400000,
  ota_1,    app,  ota_1,   ,        0x400000,
  storage,  data, spiffs,  ,        0x400000,
  ```
- [ ] Verify sufficient flash space (16MB available on ESP32-P4)
- [ ] Test partition table with `idf.py partition-table`

### Phase 2: OTA Client Implementation
- [ ] Create OTA component (`components/ota/`)
- [ ] Implement HTTP OTA download function
- [ ] Add firmware version checking
- [ ] Implement rollback mechanism
- [ ] Add OTA progress callback for UI
- [ ] Handle OTA errors and recovery

### Phase 3: Development Server
- [ ] Create Python HTTP server script for dev machine
- [ ] Serve firmware binary at known URL
- [ ] Add basic authentication (optional)
- [ ] Auto-detect firmware version
- [ ] Support for multiple devices

### Phase 4: UI Integration
- [ ] Add "Software Update" screen to settings menu
- [ ] Display current firmware version
- [ ] "Check for Updates" button
- [ ] Progress bar during download/flash
- [ ] Success/failure notification
- [ ] Reboot prompt after update

### Phase 5: Safety & Verification
- [ ] Implement firmware signature verification
- [ ] Add checksum validation (MD5/SHA256)
- [ ] Test rollback on failed boot
- [ ] Add OTA lock during critical operations
- [ ] Prevent OTA during low battery (future)

## Acceptance Criteria

- [ ] OTA partitions properly configured in partition table
- [ ] Firmware updates work over local WiFi
- [ ] Update takes <2 minutes for 4MB firmware
- [ ] Automatic rollback on failed boot
- [ ] UI shows update progress
- [ ] No data loss (NVS, SPIFFS preserved)
- [ ] Multiple consecutive updates work correctly
- [ ] Update survives power loss during download (rollback)

## Technical Details

### Partition Table Example
```csv
# Name,   Type, SubType, Offset,  Size,     Flags
nvs,      data, nvs,     0x9000,  0x6000,
phy_init, data, phy,     0xf000,  0x1000,
factory,  app,  factory, 0x10000, 0x400000,
ota_0,    app,  ota_0,   ,        0x400000,
ota_1,    app,  ota_1,   ,        0x400000,
storage,  data, spiffs,  ,        0x400000,
```

**Flash Layout (16MB total):**
- Factory app: 4MB (initial firmware, fallback)
- OTA_0: 4MB (first update slot)
- OTA_1: 4MB (second update slot)
- SPIFFS: 4MB (assets, animations)
- NVS: 24KB (settings)
- PHY: 4KB (WiFi calibration)

### OTA Component API
```c
// components/ota/ota.h

typedef enum {
    OTA_STATE_IDLE,
    OTA_STATE_CHECKING,
    OTA_STATE_DOWNLOADING,
    OTA_STATE_VERIFYING,
    OTA_STATE_FLASHING,
    OTA_STATE_SUCCESS,
    OTA_STATE_ERROR
} ota_state_t;

typedef struct {
    ota_state_t state;
    int progress_percent;
    char error_msg[128];
} ota_status_t;

typedef void (*ota_progress_cb_t)(ota_status_t* status);

/**
 * @brief Check for firmware update from local server
 *
 * @param server_url URL of firmware server (e.g., "http://192.168.1.100:8000")
 * @return ESP_OK if update available, ESP_ERR_NOT_FOUND if up-to-date
 */
esp_err_t ota_check_update(const char* server_url);

/**
 * @brief Perform OTA update from server
 *
 * @param server_url URL of firmware server
 * @param progress_cb Callback for progress updates (optional)
 * @return ESP_OK on success
 */
esp_err_t ota_perform_update(const char* server_url, ota_progress_cb_t progress_cb);

/**
 * @brief Get current firmware version
 *
 * @return Version string (from app descriptor)
 */
const char* ota_get_current_version(void);

/**
 * @brief Mark current firmware as valid (prevents rollback)
 *
 * Call this after verifying new firmware works correctly
 */
esp_err_t ota_mark_valid(void);
```

### OTA Implementation Example
```c
// components/ota/ota.c

#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "esp_https_ota.h"

esp_err_t ota_perform_update(const char* server_url, ota_progress_cb_t progress_cb) {
    char url[256];
    snprintf(url, sizeof(url), "%s/greenwood-clock.bin", server_url);

    esp_http_client_config_t config = {
        .url = url,
        .timeout_ms = 30000,
        .keep_alive_enable = true,
    };

    esp_https_ota_config_t ota_config = {
        .http_config = &config,
    };

    esp_https_ota_handle_t ota_handle = NULL;
    esp_err_t err = esp_https_ota_begin(&ota_config, &ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OTA begin failed: %s", esp_err_to_name(err));
        return err;
    }

    // Download and flash in chunks
    while (1) {
        err = esp_https_ota_perform(ota_handle);
        if (err != ESP_ERR_HTTPS_OTA_IN_PROGRESS) {
            break;
        }

        // Report progress
        if (progress_cb) {
            int downloaded = esp_https_ota_get_image_len_read(ota_handle);
            int total = esp_https_ota_get_image_size(ota_handle);
            ota_status_t status = {
                .state = OTA_STATE_DOWNLOADING,
                .progress_percent = (downloaded * 100) / total
            };
            progress_cb(&status);
        }
    }

    if (err == ESP_OK) {
        err = esp_https_ota_finish(ota_handle);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "OTA successful, rebooting...");
            vTaskDelay(pdMS_TO_TICKS(1000));
            esp_restart();
        }
    } else {
        esp_https_ota_abort(ota_handle);
    }

    return err;
}
```

### Development Server Script
```python
#!/usr/bin/env python3
# tools/ota_server.py

import http.server
import socketserver
import os
import sys

PORT = 8000
FIRMWARE_PATH = "build/greenwood-clock.bin"

class OTAHandler(http.server.SimpleHTTPRequestHandler):
    def do_GET(self):
        if self.path == '/greenwood-clock.bin':
            if os.path.exists(FIRMWARE_PATH):
                self.send_response(200)
                self.send_header('Content-type', 'application/octet-stream')
                self.send_header('Content-Length', os.path.getsize(FIRMWARE_PATH))
                self.end_headers()

                with open(FIRMWARE_PATH, 'rb') as f:
                    self.wfile.write(f.read())

                print(f"Served firmware to {self.client_address[0]}")
            else:
                self.send_error(404, "Firmware not found")
        else:
            self.send_error(404)

if __name__ == "__main__":
    os.chdir(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

    if not os.path.exists(FIRMWARE_PATH):
        print(f"Error: {FIRMWARE_PATH} not found. Run 'idf.py build' first.")
        sys.exit(1)

    with socketserver.TCPServer(("", PORT), OTAHandler) as httpd:
        print(f"OTA Server running on port {PORT}")
        print(f"Serving: {FIRMWARE_PATH}")
        print(f"Device should connect to: http://{{YOUR_IP}}:{PORT}")
        httpd.serve_forever()
```

### UI Integration Example
```c
// In screen_manager.c - Software Update screen

static void create_update_screen(void) {
    lv_obj_t* screen = lv_obj_create(NULL);

    // Current version label
    lv_obj_t* version_label = lv_label_create(screen);
    lv_label_set_text_fmt(version_label, "Current Version: %s",
                          ota_get_current_version());

    // Check for updates button
    lv_obj_t* check_btn = lv_btn_create(screen);
    lv_obj_t* check_label = lv_label_create(check_btn);
    lv_label_set_text(check_label, "Check for Updates");
    lv_obj_add_event_cb(check_btn, check_update_cb, LV_EVENT_CLICKED, NULL);

    // Progress bar (hidden initially)
    lv_obj_t* progress = lv_bar_create(screen);
    lv_obj_add_flag(progress, LV_OBJ_FLAG_HIDDEN);

    // Status label
    lv_obj_t* status_label = lv_label_create(screen);
    lv_label_set_text(status_label, "");
}

static void check_update_cb(lv_event_t* e) {
    // Get server URL from settings or hardcoded for dev
    const char* server_url = "http://192.168.1.100:8000";

    lvgl_port_unlock();
    esp_err_t err = ota_check_update(server_url);
    lvgl_port_lock(0);

    if (err == ESP_OK) {
        // Update available - show "Install Update" button
        show_install_button();
    } else {
        // Already up to date
        show_status("Already up to date");
    }
}
```

### Rollback Mechanism

ESP-IDF provides automatic rollback:

1. **First Boot After OTA**: Partition marked as "pending verification"
2. **App Validation**: App must call `esp_ota_mark_app_valid_cancel_rollback()`
3. **Next Boot**: If validation not called, rolls back to previous partition
4. **Implementation**: Call validation after successful boot + basic health check

```c
// In main.cpp after successful initialization

void app_main() {
    // ... initialization ...

    // Mark OTA as valid if we got this far
    esp_ota_img_states_t ota_state;
    const esp_partition_t* running = esp_ota_get_running_partition();

    if (esp_ota_get_state_partition(running, &ota_state) == ESP_OK) {
        if (ota_state == ESP_OTA_IMG_PENDING_VERIFY) {
            ESP_LOGI(TAG, "New firmware detected, marking as valid");
            esp_ota_mark_app_valid_cancel_rollback();
        }
    }

    // ... rest of main ...
}
```

## Development Workflow

### Step 1: Build New Firmware
```bash
idf.py build
```

### Step 2: Start OTA Server
```bash
python tools/ota_server.py
```

### Step 3: Trigger Update on Device
**Via Touchscreen:**
1. Settings → Software Update
2. Tap "Check for Updates"
3. Tap "Install Update" if available
4. Wait for progress bar
5. Device reboots automatically

**Via Serial Console (for testing):**
```c
// Add console command
ota_perform_update("http://192.168.1.100:8000", NULL);
```

### Step 4: Verify Update
- Check version on UI
- Test basic functionality
- Monitor serial logs for errors

## Risks & Mitigations

| Risk | Impact | Likelihood | Mitigation |
|------|--------|------------|------------|
| Brick device with bad firmware | High | Low | Rollback mechanism, factory partition |
| Network interruption during OTA | Medium | Medium | Resume support, timeout handling |
| Insufficient flash space | High | Low | Verify partition layout, optimize binary size |
| Corrupt firmware download | High | Low | Checksum verification, signature validation |
| OTA during critical operation | Medium | Low | Lock mechanism, user confirmation |

## Success Metrics

- OTA update success rate: >95%
- Update time: <2 minutes for 4MB firmware
- Zero bricks from OTA failures
- Rollback works 100% of the time
- Network interruptions handled gracefully
- UI clearly indicates update status

## Security Considerations

### For Development (Current Scope)
- Local network only (no internet exposure)
- HTTP acceptable (trusted network)
- Optional basic authentication
- Firmware checksum validation

### For Production (Future)
- HTTPS with certificate validation
- Firmware signing with private key
- Version number verification
- Secure boot integration
- Rate limiting on update server

## Testing Plan

### Unit Tests
- [ ] Partition table validation
- [ ] Firmware download with simulated errors
- [ ] Rollback trigger conditions
- [ ] Progress callback functionality

### Integration Tests
- [ ] Full OTA update cycle
- [ ] Network interruption during download
- [ ] Power loss simulation (rollback)
- [ ] Multiple consecutive updates
- [ ] Update with invalid firmware (rollback)
- [ ] Update while other operations running

### Performance Tests
- [ ] Measure update time for various firmware sizes
- [ ] Memory usage during OTA
- [ ] Network bandwidth utilization
- [ ] Flash write speed

## Future Enhancements

- **Cloud OTA**: Updates from GitHub releases or custom server
- **Differential Updates**: Only flash changed sections
- **Multi-Device Updates**: Push updates to multiple clocks
- **Scheduled Updates**: Auto-update during off-hours
- **Update Channels**: Stable vs. beta firmware
- **Compressed Firmware**: Reduce download size
- **Resume Support**: Continue interrupted downloads

## Related Work

- ESP-IDF OTA Documentation: https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/ota.html
- ESP HTTPS OTA: https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/esp_https_ota.html
- Partition Tables: https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/partition-tables.html

## Notes

### Why Local Network OTA First?

**Advantages:**
- Faster development iteration
- No internet dependency
- Full control over update server
- Easier debugging
- No cloud infrastructure needed

**Limitations:**
- Requires device and dev machine on same network
- Not suitable for production deployment
- Manual server startup required

### Current Partition Table

Check `partitions.csv` to see current layout. May need to:
- Add OTA partitions
- Reduce SPIFFS size if needed
- Ensure factory partition for fallback

### Firmware Size Considerations

Current binary: ~3.76 MB
- Should fit in 4MB OTA partitions
- Monitor size growth
- Consider compression for future

---

**Repository**: https://github.com/tvanfossen/greenwood-clock

**Created**: 2025-12-02

**Status**: Backlog (medium priority, useful for development workflow)
