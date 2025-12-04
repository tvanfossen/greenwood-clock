# WiFi Settings & Persistent Configuration Testing Guide

## Overview

This guide covers testing for the new dynamic WiFi configuration and persistent settings features.

## Prerequisites

1. Hardware modification completed (touch jumper wire - see TOUCH_HARDWARE_MOD.md)
2. **IMPORTANT**: Before first boot, ensure NO WiFi credentials are hardcoded
3. Serial monitor connected (115200 baud)
4. Multiple WiFi networks available for testing

## Feature Summary

### New Features Implemented
- **Persistent Settings**: All user configuration stored in NVS (Non-Volatile Storage)
- **Dynamic WiFi Configuration**: Scan, select, and connect to WiFi networks via touchscreen
- **Settings Persistence**: WiFi credentials and brightness saved across reboots
- **No Hardcoded Credentials**: Device works without network on first boot

## Test Plan

### Phase 1: First Boot (No WiFi Configured)

**Objective**: Verify device boots without hardcoded WiFi credentials

#### Test 1.1: First Boot Behavior

1. **Action**: Flash firmware to device
   ```bash
   idf.py -p /dev/ttyUSB0 flash
   ```

2. **Action**: Monitor serial output
   ```bash
   idf.py -p /dev/ttyUSB0 monitor
   ```

3. **Expected Serial Output**:
   ```
   I (xxx) main: [0] Settings initialized
   I (xxx) main: [0] Settings loaded
   I (xxx) settings: Settings namespace not found, using defaults
   I (xxx) main: [4] WiFi not configured! Please configure via touchscreen settings.
   I (xxx) main: [4] Continuing without network...
   ```

4. **Expected UI**:
   - Clock displays with time showing 1970 (no SNTP sync)
   - Weather shows no data (no network)
   - Touch should work

5. **Pass Criteria**: Device boots without errors, touch works

#### Test 1.2: Access Settings Menu

1. **Action**: Long press anywhere on screen (2+ seconds) OR swipe up
2. **Expected**: Settings menu appears
3. **Pass Criteria**: Settings menu shows three options

### Phase 2: WiFi Configuration

**Objective**: Configure WiFi credentials and verify connection

#### Test 2.1: Navigate to WiFi Settings

1. **Action**: Tap "WiFi Settings" in settings menu
2. **Expected Serial Output**:
   ```
   I (xxx) screen_mgr: Settings item clicked: 2
   I (xxx) screen_mgr: Navigating to screen 2
   ```
3. **Expected UI**:
   - Title: "WiFi Settings"
   - Status: "Not connected"
   - "Scan Networks" button visible
   - Empty network list
4. **Pass Criteria**: WiFi settings screen displays correctly

#### Test 2.2: Scan for Networks

1. **Action**: Tap "Scan Networks" button
2. **Expected Serial Output**:
   ```
   I (xxx) screen_mgr: WiFi scan button clicked
   I (xxx) network: Starting WiFi scan...
   I (xxx) network: Found X access points
   I (xxx) screen_mgr: Found X networks
   ```
3. **Expected UI**:
   - Status changes to "Scanning..."
   - Then shows "Found X networks"
   - Network list populated with SSIDs
   - Locked networks show "* " prefix
   - Signal strength shown (e.g., "-45 dBm")
4. **Pass Criteria**: All nearby networks appear in list

#### Test 2.3: Select Network

1. **Action**: Tap on your WiFi network in the list
2. **Expected Serial Output**:
   ```
   I (xxx) screen_mgr: Selected network: YourSSID
   ```
3. **Expected UI**:
   - Status changes to "Enter password for: YourSSID"
   - Password input field appears at bottom
   - On-screen keyboard appears
   - "Connect" button visible
4. **Pass Criteria**: Password input and keyboard displayed

#### Test 2.4: Enter Password

1. **Action**: Use on-screen keyboard to type WiFi password
2. **Expected UI**:
   - Password shows as dots (masked)
   - Characters appear as typed
3. **Action**: Tap "Connect" button
4. **Expected Serial Output**:
   ```
   I (xxx) screen_mgr: Connecting to YourSSID...
   I (xxx) network: Connecting to 'YourSSID'...
   I (xxx) network: Connection initiated
   I (xxx) settings: Saving settings to NVS
   I (xxx) settings: Settings saved successfully
   I (xxx) screen_mgr: WiFi credentials saved
   ```
5. **Expected UI**:
   - Status changes to "Connecting..."
   - Then "Connected! Saved to settings."
   - Keyboard and password field hide
6. **Pass Criteria**: Device connects and saves credentials

#### Test 2.5: Verify SNTP Sync

1. **Action**: Wait 10-20 seconds after connection
2. **Expected Serial Output**:
   ```
   I (xxx) network: Got IP, starting SNTP
   I (xxx) network: SNTP synchronized: 2025-12-02 ...
   ```
3. **Action**: Return to clock screen (tap Back twice)
4. **Expected UI**:
   - Time shows current time (not 1970)
   - Weather data loads (if API key configured)
5. **Pass Criteria**: Time and weather update

### Phase 3: Persistence Testing

**Objective**: Verify settings persist across reboots

#### Test 3.1: Reboot with Saved WiFi

1. **Action**: Power cycle the device (or press reset)
2. **Expected Serial Output**:
   ```
   I (xxx) settings: Settings loaded from NVS
   I (xxx) settings:   WiFi configured: yes
   I (xxx) settings:   SSID: YourSSID
   I (xxx) main: [4] Using WiFi credentials from settings
   I (xxx) main: [4] network_init: OK
   I (xxx) network: Got IP, starting SNTP
   ```
3. **Expected Behavior**: Device automatically connects to WiFi
4. **Pass Criteria**: No WiFi configuration needed, auto-connects

#### Test 3.2: Brightness Persistence

1. **Action**: Navigate to Settings → Brightness
2. **Action**: Adjust slider to 75%
3. **Expected Serial Output**:
   ```
   I (xxx) screen_mgr: Brightness changed to: 75%
   I (xxx) settings: Brightness setting saved
   ```
4. **Action**: Reboot device
5. **Expected**: Screen brightness is 75% after reboot
6. **Pass Criteria**: Brightness setting persists

### Phase 4: WiFi Error Handling

**Objective**: Test error scenarios

#### Test 4.1: Wrong Password

1. **Action**: Scan networks and select your network
2. **Action**: Enter incorrect password
3. **Action**: Tap Connect
4. **Expected**: Connection fails, status shows "Connection failed"
5. **Pass Criteria**: Device doesn't crash, can retry

#### Test 4.2: Out of Range Network

1. **Action**: Connect to network, verify connection
2. **Action**: Move device out of WiFi range or disable AP
3. **Expected Serial Output**:
   ```
   I (xxx) network: Wi-Fi disconnected, retrying...
   ```
4. **Expected**: Device attempts reconnection
5. **Pass Criteria**: Device handles disconnection gracefully

#### Test 4.3: Change WiFi Network

1. **Action**: Connect to Network A
2. **Action**: Navigate to WiFi settings
3. **Action**: Scan and select Network B
4. **Action**: Enter password for Network B
5. **Action**: Tap Connect
6. **Expected**:
   - Device disconnects from Network A
   - Connects to Network B
   - New credentials saved to NVS
7. **Expected Serial Output**:
   ```
   I (xxx) network: Connecting to 'NetworkB'...
   I (xxx) settings: Settings saved successfully
   ```
8. **Action**: Reboot device
9. **Expected**: Device connects to Network B (not A)
10. **Pass Criteria**: Network change persists across reboots

### Phase 5: Settings Management

**Objective**: Test settings access and factory reset

#### Test 5.1: View Current Settings (Via Serial)

1. **Action**: Connect serial monitor
2. **Expected Serial Output** (on boot):
   ```
   I (xxx) settings: Settings loaded from NVS
   I (xxx) settings:   WiFi configured: yes
   I (xxx) settings:   SSID: YourSSID
   I (xxx) settings:   Brightness: 75%
   I (xxx) settings:   Location: 43.366, -85.851
   ```
3. **Pass Criteria**: All settings visible in logs

#### Test 5.2: Factory Reset (NVS Erase)

1. **Action**: Erase NVS partition via serial
   ```bash
   idf.py -p /dev/ttyUSB0 erase-flash
   idf.py -p /dev/ttyUSB0 flash
   ```

2. **Expected**: Device boots as if first time
   - No WiFi configured
   - Default brightness (50%)
   - Default location

3. **Pass Criteria**: All settings reset to defaults

### Phase 6: Integration Testing

**Objective**: Test full workflow end-to-end

#### Test 6.1: Complete Setup Flow

1. Flash fresh firmware (erase NVS first)
2. Complete hardware modification
3. Power on device
4. Configure WiFi via touchscreen
5. Adjust brightness
6. Reboot device
7. Verify all settings persist
8. Use device for 30 minutes
9. Check for stability (no crashes)

**Pass Criteria**: Complete workflow works smoothly

## Performance Metrics

### WiFi Scan Speed
- **Target**: <10 seconds for scan completion
- **Measurement**: Time from "Scan Networks" tap to results displayed
- **Pass Criteria**: Scan completes within target time

### Connection Time
- **Target**: <15 seconds to obtain IP address
- **Measurement**: Time from "Connect" tap to "Connected!" message
- **Pass Criteria**: Connection completes within target time

### Settings Save Speed
- **Target**: <500ms to save to NVS
- **Measurement**: Check serial logs for save operation time
- **Pass Criteria**: No noticeable lag when changing settings

### Memory Usage
- **Target**: Free heap remains above 20KB during WiFi operations
- **Measurement**: Monitor "[HEALTH]" logs
- **Pass Criteria**: No memory leaks, heap stable

## Common Issues and Solutions

### Issue: "WiFi scan failed"

**Symptoms**: Scan button does nothing or returns no networks

**Solutions**:
1. Check WiFi is initialized (serial logs)
2. Ensure WiFi adapter is working
3. Try moving closer to access points
4. Check antenna connection on board

### Issue: "Settings not persisting"

**Symptoms**: WiFi credentials don't survive reboot

**Solutions**:
1. Check NVS partition is flashed correctly
2. Verify serial logs show "Settings saved successfully"
3. Check NVS partition not corrupted (erase and reflash)

### Issue: Keyboard doesn't appear

**Symptoms**: Password field shows but no keyboard

**Solutions**:
1. Check LVGL heap usage (may be out of memory)
2. Try rebooting device
3. Check serial logs for LVGL errors

### Issue: Cannot connect despite correct password

**Symptoms**: Connection always fails

**Solutions**:
1. Verify network is 2.4GHz (ESP32 doesn't support 5GHz)
2. Check network security type is supported
3. Try connecting to open network first
4. Check WiFi adapter firmware

### Issue: Touch not working

**Symptoms**: Cannot access settings menu

**Solutions**:
1. Verify hardware modification (jumper wire)
2. Check serial logs for "Touch input enabled"
3. Test with simple tap (should see "Touch: PRESSED" logs)
4. Refer to TOUCH_HARDWARE_MOD.md

## Test Results Template

```
=== WiFi & Persistent Settings Test Results ===
Date: YYYY-MM-DD
Tester: [Name]
Firmware Version: [commit hash]

Phase 1: First Boot
- Test 1.1: [PASS/FAIL]
- Test 1.2: [PASS/FAIL]

Phase 2: WiFi Configuration
- Test 2.1: [PASS/FAIL]
- Test 2.2: [PASS/FAIL]
- Test 2.3: [PASS/FAIL]
- Test 2.4: [PASS/FAIL]
- Test 2.5: [PASS/FAIL]

Phase 3: Persistence Testing
- Test 3.1: [PASS/FAIL]
- Test 3.2: [PASS/FAIL]

Phase 4: Error Handling
- Test 4.1: [PASS/FAIL]
- Test 4.2: [PASS/FAIL]
- Test 4.3: [PASS/FAIL]

Phase 5: Settings Management
- Test 5.1: [PASS/FAIL]
- Test 5.2: [PASS/FAIL]

Phase 6: Integration
- Test 6.1: [PASS/FAIL]

Performance Metrics:
- WiFi Scan Speed: [X seconds]
- Connection Time: [X seconds]
- Settings Save Speed: [X ms]
- Memory Stability: [Stable / Unstable]

Issues Encountered:
[List any problems]

Notes:
[Additional observations]
```

## Quick Start Test (5 Minutes)

For a quick verification:

1. Flash firmware
2. Long press to open settings
3. Tap WiFi Settings → Scan Networks
4. Select your network, enter password, connect
5. Wait for "Connected!" message
6. Return to clock, verify time updates
7. Reboot device
8. Verify auto-connects on boot

**Expected Result**: Device connects and time shows correctly after reboot

---

**Created**: 2025-12-02
**Last Updated**: 2025-12-02
**Project**: Greenwood Clock
**Platform**: ESP32-P4
