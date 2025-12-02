# Greenwood Clock

ESP32-based smart clock with weather integration and LVGL UI.

## Overview

The Greenwood Clock is an embedded IoT device built on the ESP32-P4 platform that displays time, date, weather information, and other environmental data on a touchscreen display. The project uses the ESP-IDF framework with LVGL for graphics rendering.

## Hardware

- **Platform**: ESP32-P4 Function EV Development Kit
- **Display**: MIPI DSI touchscreen (exact resolution varies)
- **Connectivity**: Wi-Fi 802.11 b/g/n
- **Storage**: SPIFFS partition (4MB)

## Features

### ✅ Implemented
- **Time Display**: 12-hour format with AM/PM indicator
- **Date Display**: Full date with day of week
- **Weather Integration**: Current temperature, UV index via OpenWeatherMap API
- **Weather Icons**: Dynamic weather condition icons
- **Network Sync**: SNTP time synchronization
- **Timezone Support**: Configurable timezone (currently hardcoded)
- **UI Framework**: Responsive LVGL-based layout
- **Splash Screen**: Boot-time splash image

### 🚧 In Development
- **Astronomy Module**: Moon phase display (currently disabled due to stack issues)
- **Settings UI**: Configuration menu
- **Location Selection**: User-configurable location/timezone
- **Persistent Storage**: NVS-based settings persistence

## Architecture

### Project Structure

```
greenwood-clock/
├── components/              # Reusable ESP-IDF components
│   ├── astronomy/           # AstronomyAPI client (disabled)
│   ├── fonts/               # LVGL font definitions
│   ├── images/              # Image assets and converters
│   ├── lottie/              # Lottie animation support
│   ├── lvgl/                # LVGL graphics library
│   ├── network/             # Wi-Fi and SNTP client
│   ├── secrets/             # Centralized API key management
│   ├── time_sync/           # Timezone and time utilities
│   ├── ui/                  # Main UI rendering logic
│   └── weather/             # OpenWeatherMap API client
├── main/                    # Application entry point
│   └── main.cpp             # Initialization and main loop
├── spiffs/                  # SPIFFS filesystem content
├── CMakeLists.txt           # Top-level build configuration
├── sdkconfig                # ESP-IDF configuration
└── partitions.csv           # Flash partition table
```

### Component Descriptions

#### UI Component (`components/ui/`)
- LVGL-based responsive user interface
- Top section: Large time display (12-hour) with date
- Bottom section: Grid layout with weather icon, temperature, and UV index
- Update intervals: Clock (60s), Weather (30min)
- Custom fonts: Nunito (48pt, 128pt, 256pt, 512pt)

#### Weather Component (`components/weather/`)
- OpenWeatherMap API integration
- Fetches current conditions for configured lat/long
- Downloads and caches weather icons (PNG)
- Parses JSON responses with cJSON library
- Data includes: temperature, humidity, UV index, wind, pressure

#### Network Component (`components/network/`)
- Wi-Fi station mode initialization
- SNTP client for time synchronization
- Blocking wait for time sync on boot
- SNTP servers: pool.ntp.org

#### Time Sync Component (`components/time_sync/`)
- Timezone configuration via POSIX TZ strings
- Local time conversion utilities
- Currently configured for EST5EDT (Eastern Time)

#### Astronomy Component (`components/astronomy/`) - DISABLED
- AstronomyAPI integration for moon phase data
- Currently causes stack protection faults
- Requires memory optimization before re-enabling

#### Secrets Component (`components/secrets/`)
- Centralized API key and credential management
- Contains:
  - WEATHER_API_KEY (OpenWeatherMap)
  - ASTRONOMY_API_KEY and ASTRONOMY_APP_ID
  - GREENWOOD_LAT and GREENWOOD_LONG (location)
- Note: This file is gitignored in production

## Build Instructions

### Prerequisites

1. **ESP-IDF v5.5+**
   ```bash
   # If not installed, follow: https://docs.espressif.com/projects/esp-idf/en/latest/get-started/

   # Source ESP-IDF environment (adjust path as needed)
   source ~/esp/esp-idf/export.sh
   ```

2. **Python 3.10+** with ESP-IDF dependencies

### Building

```bash
# Configure project (first time only)
idf.py set-target esp32p4

# Build the project
idf.py build

# Flash to device
idf.py -p /dev/ttyUSB0 flash

# Monitor serial output
idf.py -p /dev/ttyUSB0 monitor
```

### Build Output

- Binary: `build/greenwood-clock.bin` (approx 3.6MB)
- Bootloader: `build/bootloader/bootloader.bin`
- Partition table: `build/partition_table/partition-table.bin`
- SPIFFS image: `build/storage.bin`

### Current Build Status

✅ **Build Status**: Passing (as of 2025-12-01)
- No compilation errors
- No warnings
- All components compile successfully

## Configuration

### Wi-Fi Credentials

Edit `main/main.cpp` line 110 to set your Wi-Fi credentials:
```cpp
network_init("YOUR_SSID", "YOUR_PASSWORD");
```

### Location

Edit `components/secrets/secrets.h` to set your location:
```c
#define GREENWOOD_LAT 43.366   // Your latitude
#define GREENWOOD_LONG -85.851 // Your longitude
```

### Timezone

Edit `main/main.cpp` line 119 to set your timezone:
```cpp
const char *tz = "EST5EDT,M3.2.0/2,M11.1.0/2";  // POSIX TZ string
```

### API Keys

Edit `components/secrets/secrets.h` to add your API keys:
```c
#define WEATHER_API_KEY "your_openweathermap_api_key"
#define ASTRONOMY_API_KEY "your_astronomyapi_key"  // Optional
#define ASTRONOMY_APP_ID "your_astronomyapi_app_id"  // Optional
```

Get API keys from:
- Weather: https://openweathermap.org/api
- Astronomy: https://astronomyapi.com/

## Known Issues

### Critical
1. **Astronomy Module Stack Faults**
   - Status: Disabled in code
   - Cause: Excessive stack usage causes protection faults
   - Impact: Moon phase display unavailable
   - Fix: Requires memory profiling and optimization

### Medium
2. **SNTP Sync Reliability**
   - Status: Active investigation
   - Symptoms: Intermittent failures, occasional fallback to epoch (1970)
   - Workaround: Retry on boot
   - Fix: Add fallback NTP servers, implement retry logic

3. **Hardcoded Configuration**
   - Wi-Fi credentials in source code
   - Location hardcoded
   - No runtime configuration UI
   - Fix: Implement NVS-based settings + configuration menu

### Low
4. **HTTP Error Handling**
   - Some edge cases in weather API response parsing not handled
   - Missing timeout configuration
   - Fix: Add comprehensive error handling and retries

5. **No Unit Tests**
   - No automated testing framework
   - Manual testing only
   - Fix: Integrate Unity test framework

## Dependencies

### ESP-IDF Components
- `esp_http_client` - HTTP/HTTPS client with TLS
- `esp_wifi` - Wi-Fi driver
- `lwip` - TCP/IP stack with SNTP
- `mbedtls` - TLS/SSL library
- `nvs_flash` - Non-volatile storage
- `spiffs` - SPI Flash File System

### External Libraries
- **LVGL v8.x**: Graphics library
- **cJSON**: JSON parser
- **Unity**: Unit testing framework (planned)

### Build Dependencies
- CMake 3.5+
- Ninja build system
- Python 3.10+ with pip packages:
  - `esptool`
  - `esp-idf` requirements

## Development Guidelines

### Code Style
- C99 for C files, C++11 for C++ files
- 4-space indentation (no tabs)
- ESP-IDF naming conventions: `component_function_name()`
- Use ESP_LOGx macros for logging

### Logging
```c
static const char* TAG = "component_name";
ESP_LOGI(TAG, "Informational message");
ESP_LOGW(TAG, "Warning message");
ESP_LOGE(TAG, "Error message");
```

### Memory Management
- Always check malloc() return values
- Free allocated memory in error paths
- Profile heap usage for new features
- Avoid stack allocations >2KB

### Commit Messages
Format: `component: Brief description`
```
ui: Add moon phase display widget

- Created moon_widget.c with LVGL container
- Integrated with astronomy API
- Update every 24 hours

Related: #issue-number
```

## Performance Metrics

### Memory Usage (ESP32-P4)
- Heap: ~280KB used / ~512KB available (55%)
- Stack: Per-task, monitored via `uxTaskGetStackHighWaterMark()`
- Flash: 3.6MB / 9MB partition (40%)

### Update Intervals
- Clock: Every 60 seconds
- Weather: Every 30 minutes
- Moon phase: Every 24 hours (when enabled)

### Network Performance
- Wi-Fi connect: ~2-5 seconds
- SNTP sync: ~1-3 seconds
- Weather API call: ~2-4 seconds
- Icon download: ~1-2 seconds (varies by icon size)

## Maintenance

### Updating ESP-IDF
```bash
cd ~/esp/esp-idf
git pull
git submodule update --init --recursive
./install.sh
```

### Cleaning Build
```bash
idf.py fullclean
rm -rf build/
idf.py build
```

### Monitoring Heap
```bash
idf.py monitor
# Press Ctrl+] to exit monitor
```

In code:
```c
ESP_LOGI(TAG, "Free heap: %lu bytes", esp_get_free_heap_size());
```

## Troubleshooting

### Build Fails
1. Ensure ESP-IDF environment is sourced: `source ~/esp/esp-idf/export.sh`
2. Clean build: `idf.py fullclean && idf.py build`
3. Check ESP-IDF version: `idf.py --version` (should be v5.5+)

### Device Won't Connect to Wi-Fi
1. Verify SSID and password in `main/main.cpp`
2. Check router supports 2.4GHz (ESP32 doesn't support 5GHz)
3. Monitor output: `idf.py monitor` and look for Wi-Fi error messages

### Time Shows 1970
1. SNTP sync failed - check internet connectivity
2. Verify NTP servers are reachable
3. Check firewall isn't blocking UDP port 123

### Weather Not Updating
1. Verify API key is valid at https://openweathermap.org/
2. Check API rate limits (free tier: 60 calls/minute)
3. Monitor HTTP response codes in serial output

## License

See [LICENSE](LICENSE) file for details.

## Contributing

This project is maintained by Claude AI agents. For bugs or feature requests:
1. Check existing issues and proposals in `.claude/AGENTS/PROPOSALS/`
2. Create detailed bug reports with serial monitor output
3. For features, describe the use case and acceptance criteria

## Roadmap

See `.claude/AGENTS/PROPOSALS/ACTIVE_WORK/` for current development priorities.

### Upcoming Features
- [ ] Re-enable astronomy module with memory fixes
- [ ] Settings UI menu
- [ ] Persistent configuration (NVS)
- [ ] Air quality index display
- [ ] Sunrise/sunset times
- [ ] Multiple timezone support
- [ ] OTA (Over-The-Air) updates
- [ ] Comprehensive test suite (>80% coverage)
- [ ] CI/CD pipeline

---

**Last Updated**: 2025-12-01
**Maintainer**: Claude AI (greenwood-clock-maintenance-handoff)
**Status**: Active Development
