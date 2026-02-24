# Greenwood Clock — System Architecture

## Overview

The Greenwood Clock is a single-device embedded system. There is no cloud backend.
All logic runs on the ESP32-P4. External dependencies are weather API (HTTP) and NTP.

```
┌─────────────────────────────────────────────────────────┐
│                    ESP32-P4 (360 MHz)                   │
│                                                         │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐             │
│  │   LVGL   │  │ FreeRTOS │  │ ESP-IDF  │             │
│  │  Task    │  │  Tasks   │  │ Drivers  │             │
│  └──────────┘  └──────────┘  └──────────┘             │
│                                                         │
│  SPIRAM (8MB external)  ←→  LVGL draw buffers          │
│  Flash (16MB)           ←→  SPIFFS + OTA partitions    │
└───────────┬─────────────────────────────┬───────────────┘
            │ MIPI DSI                    │ I2C
            ▼                             ▼
     ┌─────────────┐               ┌─────────────┐
     │ 1024×600    │               │ GT911 Touch │
     │ DSI Display │               │ Controller  │
     └─────────────┘               └─────────────┘
```

## Component Dependency Graph

```
main.cpp
├── settings        (NVS load on boot — provides cfg to everyone)
├── network         (Wi-Fi STA, SNTP)
│   └── time_sync   (POSIX TZ)
├── weather         (HTTP → OpenWeatherMap)
│   └── cjson       (JSON parse)
├── ui              (LVGL screens)
│   ├── fonts
│   ├── images
│   └── lottie      (⚠️ unverified)
├── sdcard          (BSP SD mount)
│   └── debug_log   (log mirror → SD)
├── ota             (OTA client)
├── http_api        (REST server port 80)
├── lvgl_mem_esp    (custom aligned allocator)
└── secrets         (compile-time keys, not in git)

```

## Boot Sequence

```
app_main()
    │
    ├─[0] settings_init() + settings_load()        ← NVS or defaults
    │
    ├─[1] bsp_spiffs_init_default()                ← /spiffs mount
    │     Verify splash.png exists
    │
    ├─[1.5] sdcard_init() + sdcard_mount()         ← /sdcard mount (optional)
    │       └─ debug_log_init()                    ← log → /sdcard/logs/debug.log
    │
    ├─[2] bsp_display_start_with_config()          ← LVGL + display init
    │     lv_fs_posix_init()                       ← A: drive → /sdcard
    │     lv_fs_spiffs_init()                      ← B: drive → /spiffs
    │     lv_indev_enable(cfg.enable_touch)        ← touch on/off from settings
    │
    ├─[3] ui_show_splash()                         ← splash from B:/splash.png
    │
    ├─[4] network_init() + network_wait_for_time() ← Wi-Fi + SNTP
    │     (skipped if wifi_configured == false)
    │
    ├─[5] time_sync_setup(cfg.timezone)            ← POSIX TZ string
    │
    ├─[5.5] ota_init() + ota_mark_app_valid()      ← OTA rollback guard
    │
    ├─[6] weather_init()                           ← weather polling timer
    │     http_api_start()                         ← REST server port 80
    │
    ├─[7] ui_show_start_screen()                   ← tap-to-start screen
    │
    └─[∞] idle loop: heap monitoring every 60s
```

## Filesystem Architecture

Two LVGL virtual drives backed by real POSIX filesystems:

```
LVGL path    →    POSIX path         Content
─────────────────────────────────────────────────────
A:/          →    /sdcard/           User content (SD card, optional)
B:/          →    /spiffs/           Built-in assets (always present)

Examples:
  B:/splash.png      →  /spiffs/splash.png       (boot splash)
  A:/backgrounds/x   →  /sdcard/backgrounds/x    (user GIFs)
```

**Rule**: Never embed the mount point in LVGL paths. `A:/sdcard/x` → double-path bug.

## UI Screen Hierarchy

```
Start Screen  (tap to launch)
    │
    ▼
Clock Screen  (main view — time, date, weather)
    │
    └─ Swipe Up / Long Press → Settings Menu
            ├── Wi-Fi Settings
            │       └── Network scan → password entry → connect
            ├── Brightness
            │       └── Slider → immediate apply + NVS save
            ├── Software Update (OTA)
            │       └── Check → progress bar → reboot
            └── About
                    └── System info (IP, MAC, heap, version)
```

Screen transitions use a push/pop stack (`screen_manager.c`).
Back navigation: tap back button → pop stack → animate right.

## Partition Layout

```
Flash (16 MB total)
┌──────────────┬────────┬──────────────────────────────┐
│ nvs          │  24 KB │ NVS (settings, Wi-Fi creds)  │
│ phy_init     │   4 KB │ Wi-Fi PHY calibration        │
│ factory      │   4 MB │ Factory firmware (fallback)  │
│ ota_0        │   4 MB │ OTA update slot 1            │
│ ota_1        │   4 MB │ OTA update slot 2            │
│ storage      │   3 MB │ SPIFFS (assets)              │
└──────────────┴────────┴──────────────────────────────┘
```

OTA alternates between `ota_0` and `ota_1`. Factory partition is the hard fallback.
New firmware is marked valid via `ota_mark_app_valid()` after successful init.

## Concurrency Model

Single LVGL task (FreeRTOS, priority 5). All other work is timer or event driven.

```
LVGL Task (priority 5)
    └── Renders UI, handles touch events, fires timers

Weather Timer (60s / 30min)
    ├── Releases LVGL lock
    ├── Fetches HTTP (blocking, can take 2-4s)
    └── Re-acquires LVGL lock to update UI

Network Events (FreeRTOS event group)
    └── SNTP sync fires on IP_EVENT_STA_GOT_IP

HTTP API Server (httpd task, ESP-IDF managed)
    └── Handles REST requests independently of LVGL
```

**Critical**: Network I/O must never happen inside `lvgl_port_lock()`.
Violating this caused the historical blue-screen crash (fixed in commit `ccb9dd6`).

## Memory Layout

| Region | Size | Use |
|---|---|---|
| Internal SRAM | ~512 KB | FreeRTOS stacks, small allocations |
| SPIRAM | 8 MB | LVGL draw buffers (~3 MB), heap overflow |
| SPIFFS | 3 MB | Built-in assets |
| SD card | Up to 32+ GB | User content, logs |

LVGL double-buffer: 1024×600×2 bytes × 2 buffers = ~2.4 MB in SPIRAM.
Custom allocator (`lvgl_mem_esp`) routes allocations >4 KB to SPIRAM and ensures
64-byte alignment for PPA compatibility.

## PPA Hardware Acceleration

See `docs/PPA_STATUS.md` for detailed investigation.

```
Goal (when working):
  CPU → GIF decode → DMA2D → PPA fill/blend → Display framebuffer
  CPU freed for: touch, network, watchdog

Current state:
  CPU → GIF decode → Software render → Display framebuffer
  (PPA configured ON but rejected due to image data alignment)
```

Required config for PPA:
```
CONFIG_LV_USE_PPA=y
CONFIG_LV_USE_PPA_IMG=y
CONFIG_LVGL_PORT_ENABLE_PPA=y
CONFIG_LV_DRAW_BUF_ALIGN=64
CONFIG_LV_DRAW_BUF_STRIDE_ALIGN=64
CONFIG_CACHE_L2_CACHE_LINE_SIZE=64   (must match L1)
```
