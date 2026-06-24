# Hardware Reference

## Board: ESP32-P4 Function EV Board

| Spec | Value |
|---|---|
| MCU | ESP32-P4 dual-core Xtensa LX9, up to 360 MHz |
| RAM | ~512 KB internal + 8 MB SPIRAM (PSRAM, 200 MHz) |
| Flash | 16 MB |
| Display | 1024×600 MIPI DSI (connected to onboard LCD) |
| Touch | GT911 capacitive touch controller (I2C) |
| Wi-Fi | Via ESP32-C6 co-processor (SDIO or SPI) |
| SD Card | SDMMC 4-bit, up to 40 MHz (managed by BSP) |
| USB | USB-C for power and serial (CH340 or CP2102) |

Reference: [ESP32-P4 Function EV Board Docs](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32p4/esp32-p4-function-ev-board/index.html)

## Touch Hardware Modification

**The GT911 touch interrupt line requires a jumper wire to function.**
Without this mod, touch input is electrically disconnected even though the driver initializes.

### Steps

1. Power off the board completely.
2. Locate the touch panel FPC connector on the board.
3. Identify the `INT` / `TP_INT` pin on the connector.
4. Run a short jumper wire from `TP_INT` to the designated GPIO (consult board schematic for
   your board revision — check `managed_components/espressif__esp32_p4_function_ev_board/`).
5. Secure the wire with a small dab of hot glue or electrical tape.
6. Power on and check serial output for:
   ```
   I (xxx) main: [2] Touch input enabled
   ```

### Verification

Tap the screen — serial should log:
```
I (xxx) ui: Touch: PRESSED
I (xxx) ui: Touch: RELEASED
I (xxx) ui: Touch: CLICKED
```

If nothing logs: check the jumper wire seating.
If position is offset: touch calibration may be needed (not yet implemented).

## Display

| Spec | Value |
|---|---|
| Resolution | 1024 × 600 px |
| Interface | MIPI DSI |
| Color depth | 16-bit RGB565 |
| Refresh | ~60 FPS target |
| Backlight | PWM-controlled via BSP |

LVGL draw buffers are full-screen (1024×600×2 bytes each), double-buffered, allocated in
SPIRAM. Direct framebuffer mode enabled (`BSP_DISPLAY_LVGL_DIRECT_MODE`).

## Wi-Fi Coprocessor (ESP32-C6)

| Spec | Value |
|---|---|
| Chip | ESP32-C6 |
| Interface | SDIO 4-bit, 40 MHz |
| Firmware | ESP-Hosted 2.12.0 slave |
| Protocols | Wi-Fi 6 (802.11ax), BLE 5.0 |

### SDIO Pin Mapping

| Signal | GPIO |
|---|---|
| CLK | 18 |
| CMD | 19 |
| D0 | 14 |
| D1 | 15 |
| D2 | 16 |
| D3 | 17 |
| Reset | 54 |

### Coprocessor Firmware

The ESP32-C6 runs separate firmware from the main ESP32-P4. It must be built and flashed
independently using the ESP-Hosted slave template:

```bash
# Build from the managed component template
cd /tmp
idf.py create-project-from-example "espressif/esp_hosted:slave"
cd slave
idf.py set-target esp32c6
cp sdkconfig.defaults.esp32c6 sdkconfig.defaults
idf.py build

# Flash via direct UART to C6 (NOT the P4's USB-UART bridge)
idf.py -p /dev/ttyUSB_C6 flash
```

Verify after flash — serial log should show:
```
Co-processor version: 2.12.0
```

If version shows `0.0.0`, the C6 firmware is not sending the version TLV during SDIO INIT
handshake. This version mismatch can cause RPC timeouts and SDIO crashes.

## SD Card

| Spec | Value |
|---|---|
| Interface | SDMMC 4-bit |
| Speed | 40 MHz |
| Capacity tested | 16 GB SDHC (SanDisk SC16G) |
| Format required | FAT32 (format via PC — BSP does not expose format API) |
| Mount point | `/sdcard` |

SD card is optional. The device boots and operates without it (SPIFFS-only mode).
Hot-plug is not supported — card must be inserted before boot.

## Flash Partitions

```
Offset      Size    Name        Purpose
0x9000      24 KB   nvs         NVS (settings, Wi-Fi credentials)
0xf000       4 KB   phy_init    Wi-Fi PHY calibration
0x10000      4 MB   factory     Factory firmware (OTA fallback)
0x410000     4 MB   ota_0       OTA update slot 1
0x810000     4 MB   ota_1       OTA update slot 2
0xc10000     3 MB   storage     SPIFFS (built-in assets)
```

Total: ~15 MB used of 16 MB flash.

## CPU and Performance Configuration

```
CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_360=y    # Maximum 360 MHz
CONFIG_COMPILER_OPTIMIZATION_PERF=y      # Speed-optimized compilation
CONFIG_SPIRAM_SPEED_200M=y               # Maximum PSRAM bandwidth
```

## Serial Monitor

Default baud rate: **115200**

```bash
idf.py -p /dev/ttyUSB0 monitor
# Ctrl+] to exit
```

All structured boot log tags are prefixed `[N]`. Health reports are prefixed `[HEALTH]`.

## INA219 voltage / current monitor — *planned, not yet installed*

A small board-mounted INA219 sensor would let us catch brownout-class events
before they damage the panel/GT911 (the suspected failure mode behind the
2026-05-27 panel issue).

### Wiring

| INA219 pin | EV Board pin | Notes |
|---|---|---|
| VCC | 3.3 V | shares power with GT911 |
| GND | GND |  |
| SDA | GPIO 7 (BSP_I2C_SDA) | shares I2C bus with GT911 — different I2C address (default `0x40`) |
| SCL | GPIO 8 (BSP_I2C_SCL) | same |
| Vin+ | board 5 V rail (or USB +5 V) | the rail you want to monitor |
| Vin- | downstream of shunt to load |  |

The INA219 needs no additional pull-ups (GT911 mod already provides them on
the shared bus).

### Expected ranges (to baseline once installed)

| State | Current draw |
|---|---|
| Idle, display backlight off | ~150–250 mA estimate |
| Display on at 50% backlight | ~350–500 mA estimate |
| Display on + Wi-Fi TX burst | ~700 mA–1 A estimate, sub-second |

These are estimates — actual ranges need to be captured for ≥24 h to set
real alarm thresholds.

### Alarm thresholds (initial)

| Signal | Threshold | Indicates |
|---|---|---|
| Bus voltage drops below 4.5 V | brownout in progress | log + persist marker |
| Bus voltage drops below 4.0 V for > 10 ms | severe brownout — likely damage event | log + force restart on next opportunity |
| Current > 1.2 A for > 100 ms | runaway / latched short | log + safe-mode (kill backlight + warn) |

### What a brownout signature looks like in the logs

A retroactive view of the 2026-05-27 panel damage event would have looked
like (hypothesised):

```
[HEALTH] Loop N: free=... tsens=42.1C (lo=22 hi=44)
[ina219] V=4.92 I=380 mA — nominal
... long hang ...
[ina219] V=4.21 I=110 mA — BROWNOUT detected, latching marker
... display hang ...
[wdog] DISPLAY HANG DETECTED
[BOOT] last reset: BROWNOUT (history:#42)
```

i.e. INA219 catches the *cause*, not the symptom; gives us the upstream
event before the hang/wdt/panel-damage cascade.

### When this is implemented

1. Build the INA219 driver (Adafruit-style I2C reads, ~100 lines)
2. Add a polling task at 10 Hz reading bus voltage + current
3. Threshold detection → log + RTC-persistent marker for next-boot diagnosis
4. Expose latest readings via `/api/status`
