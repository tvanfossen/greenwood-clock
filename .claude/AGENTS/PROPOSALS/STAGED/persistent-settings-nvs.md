---
proposal_id: "persistent-settings-nvs"
title: "Greenwood Clock - Persistent Settings with NVS"
github_issue: null
created: "2025-12-01"
updated: "2025-12-01"

status: "STAGED"
priority: "high"
complexity: "medium"

category: ["feature", "infrastructure"]
tags: ["esp32", "nvs", "settings", "configuration"]

estimated_hours: 8
actual_hours: 6
progress_percent: 100

depends_on: ["greenwood-clock-maintenance-handoff"]
blocks: ["touchscreen-support"]

commits: []
branches: []
pull_requests: []

agent_notes: []

stall_reason: null
unblock_requirements: []

completion_date: null
verification_status: "pending"
---

# Greenwood Clock - Persistent Settings with NVS

## Problem Statement

The greenwood-clock currently has all configuration hardcoded:
- Wi-Fi credentials in `main.cpp`
- Location (lat/long) in `secrets.h`
- Timezone hardcoded in `main.cpp`
- API keys in source code

This requires recompiling and reflashing the device for any configuration change. Users cannot easily:
- Change Wi-Fi networks
- Update location for weather/timezone
- Adjust display preferences
- Configure update intervals

## Proposed Solution

Implement persistent configuration storage using ESP32's Non-Volatile Storage (NVS) API:
- Store user-configurable settings in flash
- Load settings on boot with sensible defaults
- Provide API for reading/writing settings
- Enable future settings UI integration
- Support factory reset functionality

## Current State

### Configuration Status
- **Wi-Fi**: Hardcoded SSID/password in main.cpp:110
- **Location**: `#define` in secrets.h (GREENWOOD_LAT, GREENWOOD_LONG)
- **Timezone**: String literal in main.cpp:119
- **Update Intervals**: Hardcoded in ui.c:212-213
- **API Keys**: `#define` in secrets.h (should remain compile-time)

### NVS Support
- ESP-IDF NVS library available
- Flash partition allocated (nvs partition in partitions.csv)
- Not currently used for settings

## Implementation Plan

### Phase 1: NVS Infrastructure
- [ ] Create settings component (`components/settings/`)
- [ ] Define settings structure (config_t)
- [ ] Implement NVS initialization and partition mounting
- [ ] Add default settings fallback
- [ ] Implement factory reset function

### Phase 2: Settings Schema
- [ ] Wi-Fi configuration (SSID, password, hostname)
- [ ] Location settings (latitude, longitude, timezone)
- [ ] Display settings (brightness, theme, update intervals)
- [ ] Feature flags (enable weather, enable moon phase, etc.)
- [ ] Network settings (NTP servers, timeouts)

### Phase 3: Settings API
- [ ] `settings_init()` - load from NVS or defaults
- [ ] `settings_get_*()` - getters for each setting
- [ ] `settings_set_*()` - setters with validation
- [ ] `settings_save()` - persist to NVS
- [ ] `settings_reset()` - factory reset
- [ ] Settings version/migration support

### Phase 4: Integration
- [ ] Update `main.cpp` to use settings API
- [ ] Update `weather.c` to use location from settings
- [ ] Update `time_sync.c` to use timezone from settings
- [ ] Update `network.c` to use Wi-Fi from settings
- [ ] Add settings dump command for debugging

### Phase 5: Provisioning
- [ ] SmartConfig/BLE provisioning for Wi-Fi (optional)
- [ ] Web-based configuration portal (optional)
- [ ] Serial console configuration interface
- [ ] Document provisioning workflow

## Acceptance Criteria

- [ ] All user-configurable settings stored in NVS
- [ ] Settings persist across reboots
- [ ] Factory reset restores defaults
- [ ] Settings validated on load (reject invalid values)
- [ ] API keys remain compile-time (not in NVS)
- [ ] NVS namespace properly initialized
- [ ] Settings migration handles version changes
- [ ] Documentation for provisioning workflow

## Technical Details

### Settings Structure
```c
// components/settings/settings.h

typedef struct {
    uint32_t version;  // Settings schema version

    // Wi-Fi
    char wifi_ssid[32];
    char wifi_password[64];
    char hostname[32];

    // Location
    float latitude;
    float longitude;
    char timezone[64];  // POSIX TZ string

    // Display
    uint8_t brightness;
    uint32_t clock_update_ms;
    uint32_t weather_update_ms;

    // Features
    bool enable_weather;
    bool enable_moon;
    bool enable_touch;

    // Network
    char ntp_servers[3][64];
    uint32_t ntp_timeout_ms;

} clock_settings_t;

esp_err_t settings_init(void);
esp_err_t settings_load(clock_settings_t* out);
esp_err_t settings_save(const clock_settings_t* in);
esp_err_t settings_reset(void);
```

### NVS Implementation
```c
// components/settings/settings.c

#include "nvs_flash.h"
#include "nvs.h"

#define SETTINGS_NAMESPACE "clock_cfg"
#define SETTINGS_KEY "settings"
#define SETTINGS_VERSION 1

static const clock_settings_t default_settings = {
    .version = SETTINGS_VERSION,
    .wifi_ssid = "YourSSID",
    .wifi_password = "",
    .hostname = "greenwood-clock",
    .latitude = 43.366,
    .longitude = -85.851,
    .timezone = "EST5EDT,M3.2.0/2,M11.1.0/2",
    .brightness = 50,
    .clock_update_ms = 60000,
    .weather_update_ms = 1800000,
    .enable_weather = true,
    .enable_moon = false,  // Disabled until fixed
    .enable_touch = false,
    // ... rest of defaults
};

esp_err_t settings_load(clock_settings_t* out) {
    nvs_handle_t handle;
    esp_err_t err;

    err = nvs_open(SETTINGS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "NVS not initialized, using defaults");
        memcpy(out, &default_settings, sizeof(clock_settings_t));
        return ESP_OK;
    }

    size_t size = sizeof(clock_settings_t);
    err = nvs_get_blob(handle, SETTINGS_KEY, out, &size);
    nvs_close(handle);

    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Settings not found, using defaults");
        memcpy(out, &default_settings, sizeof(clock_settings_t));
        return ESP_OK;
    }

    // Validate version and migrate if needed
    if (out->version != SETTINGS_VERSION) {
        ESP_LOGW(TAG, "Settings version mismatch, migrating...");
        settings_migrate(out);
    }

    return ESP_OK;
}

esp_err_t settings_save(const clock_settings_t* in) {
    nvs_handle_t handle;
    esp_err_t err;

    err = nvs_open(SETTINGS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;

    err = nvs_set_blob(handle, SETTINGS_KEY, in, sizeof(clock_settings_t));
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }

    nvs_close(handle);
    return err;
}
```

### Usage Example
```c
// main.cpp

#include "settings.h"

void app_main() {
    clock_settings_t cfg;

    // Initialize NVS
    nvs_flash_init();

    // Load settings
    settings_init();
    settings_load(&cfg);

    // Use settings
    network_init(cfg.wifi_ssid, cfg.wifi_password);
    time_sync_setup(cfg.timezone);
    weather_init(cfg.latitude, cfg.longitude);
    bsp_display_brightness_set(cfg.brightness);

    // ...
}
```

## Risks & Mitigations

| Risk | Impact | Likelihood | Mitigation |
|------|--------|------------|------------|
| NVS corruption | High | Low | Use NVS blob integrity checks, factory reset |
| Flash wear-out | Medium | Low | Limit write frequency, use wear leveling |
| Invalid settings bricking device | High | Low | Validate all inputs, always have defaults |
| Migration failures | Medium | Low | Thorough version testing, fallback to defaults |

## Success Metrics

- Settings survive 10,000 power cycles
- Factory reset always recovers to working state
- Invalid settings rejected with clear error messages
- Zero NVS-related crashes over 30 days
- Settings load time <100ms

## Related Work

- ESP-IDF NVS Documentation: https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/storage/nvs_flash.html
- Wi-Fi Provisioning: https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/provisioning/wifi_provisioning.html

## Notes

### Settings Not Stored in NVS

Keep these as compile-time:
- API keys (WEATHER_API_KEY, ASTRONOMY_API_KEY)
  - Security: Don't store secrets in flash
  - Can be read out via serial/JTAG
- Hardware configuration (display type, pins)
  - Rarely changes, set at compile time

### Factory Reset Trigger

Implement factory reset via:
1. Hold button during boot
2. Serial console command
3. Settings UI option
4. Delete NVS partition via esptool

### Provisioning Workflow

1. **First Boot**:
   - Device creates AP "Greenwood-Clock-XXXX"
   - User connects and configures via web interface
   - Settings saved to NVS, device reboots

2. **Normal Operation**:
   - Load settings from NVS
   - Connect to configured Wi-Fi

3. **Reconfiguration**:
   - Button triggers AP mode
   - Or settings UI menu

---

**Repository**: https://github.com/tvanfossen/greenwood-clock

**Created**: 2025-12-01

**Status**: Backlog (medium priority, blocks touchscreen support)
