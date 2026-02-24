# OTA Firmware Updates

## Architecture

Firmware updates are initiated from the development desktop and pushed directly to the device.
No physical access or touchscreen interaction is required.

```
Dev Desktop                              Greenwood Clock (port 80)
───────────                              ─────────────────────────
idf.py build
python tools/ota_push.py ──POST /ota──▶  Validate bearer token
                                         Spawn OTA task
                         ◀── 200 ──────  {"status": "accepted"}
         poll loop:
         GET /ota/status ──────────────▶  Returns current OTA state
                         ◀── 200 ──────  {"status": "receiving", "progress": 46}
                         ◀── 200 ──────  {"status": "flashing"}
                         ◀── 200 ──────  {"status": "rebooting"}
         (device reboots — poll ends)    On next boot: mark valid
```

## Desktop Tool

```bash
# Auto-discover device via mDNS (greenwood-clock.local)
python tools/ota_push.py

# Explicit IP
python tools/ota_push.py --host 192.168.1.50

# Custom firmware path
python tools/ota_push.py --host 192.168.1.50 --firmware /path/to/firmware.bin
```

## Device Endpoints

### `POST /ota` — Trigger flash

```
POST /ota
Authorization: Bearer <OTA_API_TOKEN>
Content-Type: application/octet-stream
Body: raw firmware binary
```

| Code | Body | Meaning |
|---|---|---|
| 200 | `{"status": "accepted"}` | Accepted — OTA task spawned |
| 401 | `{"error": "unauthorized"}` | Missing or invalid token |
| 409 | `{"error": "busy"}` | OTA already in progress |
| 500 | `{"error": "ota failed: ..."}` | OTA operation error |

### `GET /ota/status` — Poll progress

No authentication required.

```json
{"status": "idle"}
{"status": "receiving",  "progress": 46, "bytes_received": 1234567, "bytes_total": 3800000}
{"status": "validating"}
{"status": "flashing"}
{"status": "rebooting"}
{"status": "error", "message": "..."}
```

`ota_push.py` polls this every 1 second, printing each transition to the terminal.

## Security

A static bearer token is shared between the desktop tool and the device firmware.
The token is defined in `components/secrets/secrets.h` (gitignored) and read at runtime.
Any request without a valid `Authorization: Bearer` header is rejected with HTTP 401.

This provides sufficient protection for a single-owner device on a trusted home network.

## Rollback

The device marks new firmware as valid automatically during `app_main()` at step `[5.5]`.
If the device fails to boot successfully three times, ESP-IDF automatically rolls back
to the previously running partition.

Manual rollback via USB:
```bash
idf.py -p /dev/ttyUSB0 flash
```

## Partition Layout

```
factory   4 MB   Factory firmware — permanent hard fallback, never touched by OTA
ota_0     4 MB   OTA slot 1
ota_1     4 MB   OTA slot 2
storage   3 MB   SPIFFS (assets)
```

OTA updates alternate between `ota_0` and `ota_1`. The running slot is logged at boot
(`[5.5]`). Current firmware binary is ~3.8 MB, leaving headroom within 4 MB slots.

## First Flash

The OTA-capable partition table must be written once via USB:

```bash
idf.py -p /dev/ttyUSB0 erase-flash flash
```

All subsequent updates are wireless.
