# Greenwood Clock — Project Instructions

ESP32-P4 smart clock. ESP-IDF v5.5, LVGL v9.3, 1024×600 MIPI DSI display, GT911 touchscreen.

## Key Files

| File | Purpose |
|---|---|
| `main/main.cpp` | Boot sequence, SPIFFS/LVGL init, idle loop |
| `sdkconfig.defaults` | All ESP-IDF + LVGL configuration |
| `partitions.csv` | Flash partition layout (factory + ota_0 + ota_1 + spiffs) |
| `components/secrets/secrets.h` | API keys + location (NOT in git) |
| `components/settings/settings.h` | `clock_settings_t` — NVS settings schema |
| `components/ui/ui.c` | Main clock screen |
| `components/ui/screen_manager.c` | Settings screens, navigation stack |
| `components/lvgl_mem_esp/lv_mem_esp.c` | Custom 64-byte aligned heap allocator |

## Component Map

```
components/
├── ui/               LVGL UI, screen manager, navigation
├── network/          Wi-Fi STA, SNTP (multi-server, exponential backoff)
├── settings/         NVS persistent config (clock_settings_t)
├── weather/          OpenWeatherMap HTTP client
├── ota/              OTA update client (HTTP)
├── sdcard/           SD card via BSP, graceful fallback
├── debug_log/        Mirrors ESP logs → /sdcard/logs/debug.log
├── http_api/         Local REST server on port 80
├── time_sync/        POSIX TZ string timezone handling
├── lvgl_mem_esp/     Custom 64-byte aligned heap allocator
├── secrets/          Compile-time API keys (not in git)
├── fonts/            Nunito 48/128/256/512pt LVGL fonts
├── images/           Splash and UI assets
└── lottie/           Lottie animation (UNVERIFIED)
```

## Filesystem Paths

```
LVGL A: → /sdcard/    (SD card)
LVGL B: → /spiffs/    (SPIFFS)

CORRECT:   "A:/file.gif"         → /sdcard/file.gif
CORRECT:   "B:/splash.png"       → /spiffs/splash.png
WRONG:     "A:/sdcard/file.gif"  → /sdcard/sdcard/file.gif  (doubled!)
```

## LVGL Critical Rules

```c
// MANDATORY: All LVGL object operations must be locked
lvgl_port_lock(0);
lv_label_set_text(label, "text");
lvgl_port_unlock();

// FORBIDDEN: Network ops inside LVGL lock — causes watchdog timeout/crash
lvgl_port_lock(0);
weather_fetch(...);   // ← CRASH: blocks LVGL task for seconds
lvgl_port_unlock();
```

## Stability Requirements

These patterns must be followed in all new code:

1. **LVGL lock discipline**: Lock only around UI operations, never around network/file I/O
2. **Error logging**: Every `esp_err_t` return must be logged before being ignored or returned
3. **Heap checks**: Any new feature allocating >1 KB should log heap before and after
4. **Watchdog safety**: Long operations (>1s) must call `esp_task_wdt_reset()` or run in a task
5. **OTA safety window**: `boot_await_launch()` in `main.cpp` is the OTA recovery boundary. **NEVER** spawn tasks or run code that processes external data (network fetches, JSON, LVGL, file parsing) before this call returns. A crash before this point bricks the device — requiring physical access to recover. All feature initialization goes AFTER this call.

## Settings Schema

```c
typedef struct {
    uint32_t version;
    char wifi_ssid[32];
    char wifi_password[64];
    bool wifi_configured;
    char timezone[64];        // POSIX TZ string
    uint8_t brightness;       // 0-100
    bool enable_touch;
    char background_image[128]; // LVGL path, e.g. "A:/file.gif"
} clock_settings_t;
```

## Boot Log Tags

All boot log lines use tag `[BOOT]`. Sequence is:

| Stage | Function | Key log line |
|---|---|---|
| Settings | `boot_settings_init()` | `[BOOT] settings_load: OK` |
| SD card | `boot_sdcard_init()` | `[BOOT] Reset reason: ...` |
| Network + OTA | `boot_network_early()` | `[BOOT] OTA valid: slot=...` ← rollback boundary |
| SNTP | `boot_sntp_wait()` | `[BOOT] SNTP sync complete` |
| Timezone | `boot_timezone()` | `[BOOT] TZ: ...` |
| Services | `boot_services()` | `[BOOT] HTTP API started on port 80` |
| Launch wait | `boot_await_launch()` | `[BOOT] Pre-launch heap: free=... / Awaiting launch` |
| SPIFFS | `boot_spiffs_mount()` | `[BOOT] SPIFFS: mounted` ← after 60s window |
| Display | `boot_display_init()` | `[BOOT] bsp_display_start... / Display ready` |
| Health | `boot_health_loop()` | `[HEALTH] Loop N: free=...` |

## OTA Workflow

```bash
idf.py build
python tools/ota_server.py          # serves build/greenwood-clock.bin on port 8000
# On device: Settings → Software Update → Check for Update
```

## Common Debugging Commands

```bash
idf.py -p /dev/ttyUSB0 monitor     # Serial monitor (Ctrl+] to exit)
idf.py fullclean && idf.py build    # Clean rebuild
idf.py -p /dev/ttyUSB0 erase-flash # Nuclear option — wipes NVS settings too

```

## Secrets

`components/secrets/secrets.h` is gitignored. It contains:
- Weather API key
- Device location (lat/long) — **treat as private**
- Astronomy API credentials (optional)

Never reference actual secret values in docs, commits, or logs.
