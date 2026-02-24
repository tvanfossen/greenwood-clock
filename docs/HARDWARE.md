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
