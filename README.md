# Greenwood Clock

ESP32-P4 smart clock with touchscreen UI, weather integration, and OTA firmware updates.

## Hardware

| Item | Spec |
|---|---|
| Platform | ESP32-P4 Function EV Board |
| Display | 1024×600 MIPI DSI |
| Touch | GT911 (I2C, requires hardware jumper — see `docs/HARDWARE.md`) |
| Storage | SPIFFS (built-in assets) + SD card (user content) |
| Connectivity | Wi-Fi 802.11 b/g/n |

## Quick Start

```bash
# Source ESP-IDF v5.5+
source ~/esp/esp-idf/export.sh

# Target and build
idf.py set-target esp32p4
idf.py build

# Flash (first time — also flashes partition table)
idf.py -p /dev/ttyUSB0 erase-flash flash

# Monitor
idf.py -p /dev/ttyUSB0 monitor
```

Subsequent updates can be done via OTA (see [OTA Updates](#ota-updates)).

## Configuration

All runtime configuration (Wi-Fi, brightness, timezone) is stored in NVS and set via the
touchscreen settings menu. No recompile required for runtime settings.

**Compile-time secrets** are stored in `components/secrets/secrets.h` (not committed to git).
Copy `components/secrets/secrets.h.example` and fill in your values before building.

Default timezone: `EST5EDT,M3.2.0/2,M11.1.0/2` (Eastern). Overridable via NVS settings.

## Boot Sequence

```
[0]   Settings init (NVS)
[1]   SPIFFS mount
[1.5] SD card mount + debug log start
[2]   Display init + LVGL filesystem drivers (A: SD, B: SPIFFS)
[2]   Touch enable (from settings)
[3]   Splash screen
[4]   Wi-Fi connect + SNTP sync (if configured)
[5]   Timezone setup
[5.5] OTA init + mark firmware valid
[6]   Weather init + HTTP API server (port 80)
[7]   Start screen (tap to launch clock)
→     Idle loop: heap health check every 60s
```

## Components

| Component | Purpose | Status |
|---|---|---|
| `ui` | LVGL screens, screen manager, navigation | ✅ Working |
| `network` | Wi-Fi STA, SNTP with retry/fallback | ✅ Working |
| `settings` | NVS-based persistent config | ✅ Working |
| `weather` | OpenWeatherMap API client | ✅ Working |
| `ota` | OTA firmware update (HTTP, port 8000) | ✅ Working |
| `sdcard` | SD card mount via BSP, graceful fallback | ✅ Working |
| `debug_log` | Mirrors ESP logs to `/sdcard/logs/debug.log` | ✅ Working |
| `http_api` | Local HTTP REST server on port 80 | ✅ Working |
| `time_sync` | POSIX TZ string timezone handling | ✅ Working |
| `lvgl_mem_esp` | Custom 64-byte aligned allocator (PPA prep) | ✅ Working |
| `secrets` | Compile-time API keys (not in git) | ✅ Working |
| `fonts` | Nunito font at 48/128/256/512pt | ✅ Working |
| `images` | Splash and UI assets | ✅ Working |
| `lottie` | Lottie animation support | ⚠️ Unverified |

## Filesystem Layout

```
LVGL A: → /sdcard/          (SD card — user content)
LVGL B: → /spiffs/          (SPIFFS — built-in assets)

/sdcard/
├── backgrounds/             # User background images/GIFs
├── logs/                    # debug.log written here on boot
├── settings/                # Settings backups (future)
├── screenshots/             # (future)
└── firmware/                # OTA staging (future)

/spiffs/
└── splash.png               # Boot splash image
```

## OTA Updates

```bash
# 1. Build
idf.py build

# 2. Start server on dev machine
python tools/ota_server.py

# 3. On device: Settings → Software Update → Check for Update
```

Partition layout: factory (4 MB) + ota_0 (4 MB) + ota_1 (4 MB) + storage/SPIFFS (3 MB).
Automatic rollback if new firmware fails to boot.

## Observability

- **Serial health log** — heap free/min/delta every 60s tagged `[HEALTH]`
- **SD card log** — full ESP log output mirrored to `/sdcard/logs/debug.log`
- **HTTP API** — REST endpoint on port 80 for file/log access over network
- **LVGL assert handler** — logs heap state, resets device instead of hanging
- **Panic handler** — logs task list and heap on any crash

## Known Issues

| Issue | Severity | Status |
|---|---|---|
| PPA hardware acceleration alignment errors | High | ⚠️ Configured ON, unverified on hardware |
| OTA uses HTTP (no cert verification) | Medium | Acceptable for local dev network |
| Lottie animation integration | Low | ⚠️ Unverified |

**PPA is the primary outstanding performance issue.** See `docs/PPA_STATUS.md`.

## Development Guidelines

- ESP-IDF naming: `component_function_name()`
- All logging via `ESP_LOGx(TAG, ...)` — never truncate log content
- All LVGL operations must be wrapped with `lvgl_port_lock(0)` / `lvgl_port_unlock()`
- Network operations must NOT be called while holding the LVGL lock
- Heap allocations >4 KB prefer SPIRAM via `lvgl_mem_esp` custom allocator

## Docs

- `docs/ARCHITECTURE.md` — system architecture and component relationships
- `docs/HARDWARE.md` — board-specific setup, touch jumper mod
- `docs/PPA_STATUS.md` — PPA hardware acceleration investigation and status
- `docs/OTA.md` — OTA workflow and security considerations
- API docs: run `doxygen Doxyfile` → output in `docs/api/`

---

**Last verified build**: 2025-12-06 | **ESP-IDF**: v5.5 | **LVGL**: v9.3
