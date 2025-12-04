# OTA (Over-The-Air) Firmware Updates - Quick Start Guide

## Overview

The Greenwood Clock now supports wireless firmware updates over your local network. No need to connect USB cables or physically access the device!

## Prerequisites

- Device and development computer on the same WiFi network
- Python 3 installed on development computer
- Firmware built successfully (`idf.py build`)

## Quick Start (3 Steps)

### Step 1: Build Firmware

```bash
idf.py build
```

This creates `build/greenwood-clock.bin` (~3.8 MB).

### Step 2: Start OTA Server

On your development computer:

```bash
python tools/ota_server.py
```

You'll see output like:
```
======================================================================
Greenwood Clock OTA Server
======================================================================
Firmware: /path/to/build/greenwood-clock.bin
Size:     3,784,128 bytes (3.80 MB)
Port:     8000
Local IP: 192.168.1.100

OTA URL:  http://192.168.1.100:8000
======================================================================

Waiting for devices to connect...
```

**Note the IP address** - you'll need it for the device.

### Step 3: Update Device

On the Greenwood Clock touchscreen:

1. **Open Settings**: Long press (2+ seconds) or swipe up
2. **Tap "Software Update"**
3. **Verify Server URL**: Should show `http://192.168.1.100:8000` (your computer's IP)
4. **Tap "Check for Update"**
5. **Wait**: Progress bar shows download/flash status
6. **Auto-Reboot**: Device reboots with new firmware

That's it! The device is now running your new firmware.

## OTA Partition Layout

The device uses a dual-partition OTA scheme:

| Partition | Size | Purpose |
|-----------|------|---------|
| factory   | 4 MB | Initial firmware (fallback) |
| ota_0     | 4 MB | Update slot 1 |
| ota_1     | 4 MB | Update slot 2 |
| storage   | 3 MB | SPIFFS (assets, animations) |

**How it works:**
- First boot: Runs from `factory` partition
- After OTA update: Alternates between `ota_0` and `ota_1`
- Each update goes to the unused partition
- Automatic rollback if new firmware fails to boot

## Using the New Partition Table

⚠️ **Important**: The first time you enable OTA, you must flash with the new partition table:

```bash
# Erase old partitions (one time only!)
idf.py -p /dev/ttyUSB0 erase-flash

# Flash with new OTA partition table
idf.py -p /dev/ttyUSB0 flash
```

After this initial flash, all future updates can be done wirelessly!

## Advanced Usage

### Custom Server Port

```bash
python tools/ota_server.py --port 9000
```

Then update the device's server URL (currently hardcoded, will add UI config later).

### Custom Firmware Path

```bash
python tools/ota_server.py --firmware /path/to/custom.bin
```

### Multiple Devices

The server can handle multiple devices updating simultaneously.

## Troubleshooting

### "Connection failed" on device

1. **Check WiFi**: Device and computer on same network?
2. **Check Server**: Is `ota_server.py` running?
3. **Check IP**: Does server URL match your computer's IP?
4. **Check Firewall**: Port 8000 may be blocked

```bash
# Test from another computer on same network
curl http://192.168.1.100:8000/greenwood-clock.bin
```

### "Update failed" during download

1. **Check WiFi signal**: Move device closer to router
2. **Check server logs**: Look for errors in server output
3. **Retry**: Some network hiccups are temporary

### Device stuck in boot loop after update

**Automatic Rollback:**
The device automatically rolls back to previous firmware if:
- New firmware fails to boot 3 times
- New firmware doesn't mark itself as valid

Just wait ~30 seconds, and it will revert to the previous version.

**Manual Rollback (USB):**
If needed, reflash via USB:
```bash
idf.py -p /dev/ttyUSB0 flash
```

### Binary too large for OTA partition

Current limit: **4 MB per partition**

If your firmware exceeds this:
1. Reduce binary size (remove debug symbols, unused features)
2. Increase partition size in `partitions_ota.csv` (requires reflash via USB)

Current firmware size: ~3.8 MB ✅ (fits comfortably)

## Security Considerations

### For Development (Current Setup)

- **HTTP (not HTTPS)**: Acceptable on trusted local network
- **No Authentication**: Anyone on network can trigger updates
- **No Signature Verification**: Trusts any firmware from server

This is fine for:
- Local development
- Trusted home/office networks
- Devices not exposed to internet

### For Production (Future Enhancements)

Consider adding:
- HTTPS with certificate validation
- Firmware signing and verification
- Update authentication (password/API key)
- Encrypted firmware images

See `local-network-ota.md` proposal for security roadmap.

## Monitoring OTA Updates

### On Device (Serial Monitor)

```bash
idf.py -p /dev/ttyUSB0 monitor
```

You'll see:
```
I (xxx) ota: Starting OTA update from: http://192.168.1.100:8000/greenwood-clock.bin
I (xxx) ota: OTA image size: 3784128 bytes
I (xxx) ota: Downloaded 1000000 bytes (26%)
I (xxx) ota: Downloaded 2000000 bytes (52%)
I (xxx) ota: Downloaded 3000000 bytes (79%)
I (xxx) ota: OTA update successful!
I (xxx) main: Rebooting in 2 seconds...
```

### On Server

The server logs each request:
```
[INFO] Served firmware (3,784,128 bytes) to 192.168.1.50
```

## OTA Workflow Example

**Scenario**: You fixed a bug and want to test on the device.

```bash
# 1. Make code changes
vim components/ui/screen_manager.c

# 2. Build
idf.py build

# 3. Start server (in new terminal)
python tools/ota_server.py

# 4. On device touchscreen:
#    Settings → Software Update → Check for Update

# 5. Watch progress on device and server logs

# 6. Device reboots with new firmware

# 7. Test your changes

# 8. Repeat!
```

**Time saved**: ~2 minutes per update cycle (no USB cable fumbling!)

## Version Tracking

The device displays:
- **Current Version**: From `esp_app_desc_t` (set via idf.py)
- **Running Partition**: Shows which partition is active

Example on OTA Settings screen:
```
Current Version: v1.0.0
Partition: ota_0
```

## Partition Information

Check partition status:

```bash
idf.py partition-table
```

Output shows OTA partitions:
```
# Name,     Type, SubType,  Offset,   Size
factory,    app,  factory,  0x010000, 0x400000
ota_0,      app,  ota_0,    0x410000, 0x400000
ota_1,      app,  ota_1,    0x810000, 0x400000
storage,    data, spiffs,   0xc10000, 0x300000
```

## Best Practices

### DO:
✅ Test firmware locally before OTA update
✅ Keep device plugged in during update (don't rely on battery)
✅ Update during low-usage times
✅ Keep old firmware binary as backup
✅ Monitor serial output during first OTA

### DON'T:
❌ Power off device during update
❌ Change WiFi network during update
❌ Update critical devices without testing first
❌ Ignore failed update warnings

## Next Steps

- **Add Server URL Configuration**: UI to change OTA server address
- **Update Notifications**: Check for updates automatically
- **Firmware Versioning**: Compare versions before updating
- **Rollback UI**: Manual rollback button in settings
- **Update Scheduling**: Schedule updates for specific times

See `local-network-ota.md` proposal for full feature roadmap.

---

**Created**: 2025-12-02
**Updated**: 2025-12-02
**Status**: Ready for testing

For issues or questions, see the main project README.
