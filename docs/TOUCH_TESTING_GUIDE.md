# Touchscreen Functionality Testing Guide

## Overview

This guide provides comprehensive testing procedures for the touchscreen UI implementation on the Greenwood Clock ESP32-P4 project.

## Prerequisites

1. Hardware modification completed (see TOUCH_HARDWARE_MOD.md)
2. Firmware flashed with touch support enabled
3. Serial monitor connected (115200 baud) for log viewing
4. Device powered on and displaying clock screen

## Test Plan

### Phase 1: Basic Touch Detection

**Objective**: Verify touch input is being detected by the system

#### Test 1.1: Touch Press/Release Detection

1. **Action**: Tap anywhere on the screen
2. **Expected Serial Output**:
   ```
   I (xxxxx) ui: Touch: PRESSED
   I (xxxxx) ui: Touch: RELEASED
   I (xxxxx) ui: Touch: CLICKED
   ```
3. **Pass Criteria**: All three log messages appear in correct sequence
4. **Fail Actions**:
   - Check hardware jumper connection
   - Verify touch input enabled in main.cpp:130

#### Test 1.2: Long Press Detection

1. **Action**: Press and hold on the screen for 2+ seconds
2. **Expected Serial Output**:
   ```
   I (xxxxx) ui: Touch: PRESSED
   I (xxxxx) ui: Touch: LONG_PRESSED - Opening settings menu
   ```
3. **Expected UI**: Settings menu should appear
4. **Pass Criteria**: Settings menu opens after long press
5. **Fail Actions**: Check gesture thresholds in LVGL configuration

### Phase 2: Gesture Detection

**Objective**: Verify swipe gestures are recognized

#### Test 2.1: Swipe Up Gesture

1. **Starting State**: Clock screen displayed
2. **Action**: Swipe finger from bottom to top of screen (quick motion)
3. **Expected Serial Output**:
   ```
   I (xxxxx) ui: Gesture: SWIPE UP - Opening settings menu
   I (xxxxx) screen_mgr: Navigating to screen 1
   ```
4. **Expected UI**: Settings menu appears with slide-left animation
5. **Pass Criteria**: Settings menu opens smoothly
6. **Fail Actions**:
   - Try slower/faster swipe
   - Check gesture sensitivity in LVGL config

#### Test 2.2: Other Gestures (Optional)

1. **Action**: Try swipe down, left, right on clock screen
2. **Expected Serial Output**: Log messages for each gesture
3. **Expected UI**: No screen changes (gestures logged but not acted upon)
4. **Pass Criteria**: Gestures detected and logged

### Phase 3: Navigation Testing

**Objective**: Verify screen navigation system works correctly

#### Test 3.1: Navigate to Settings Menu

1. **Starting State**: Clock screen
2. **Action**: Long press OR swipe up
3. **Expected UI**:
   - Settings menu appears with title "Settings"
   - Back button visible in top-left
   - Three menu items visible:
     - "WiFi Settings"
     - "Brightness"
     - "About"
4. **Pass Criteria**: All UI elements visible and properly aligned

#### Test 3.2: Navigate to Sub-Screens

**For each menu item (WiFi Settings, Brightness, About):**

1. **Action**: Tap on menu item
2. **Expected Serial Output**:
   ```
   I (xxxxx) screen_mgr: Settings item clicked: X
   I (xxxxx) screen_mgr: Navigating to screen X
   ```
3. **Expected UI**:
   - Screen slides in from left
   - Title matches menu item
   - Back button present
4. **Pass Criteria**: Screen loads without visual glitches

#### Test 3.3: Back Navigation

1. **Starting State**: Any sub-screen (WiFi/Brightness/About)
2. **Action**: Tap "< Back" button
3. **Expected Serial Output**:
   ```
   I (xxxxx) screen_mgr: Back button clicked
   I (xxxxx) screen_mgr: Going back to screen X
   ```
4. **Expected UI**:
   - Previous screen slides in from right
   - All content restored
5. **Pass Criteria**: Navigation stack works correctly

#### Test 3.4: Return to Clock Screen

1. **Starting State**: Settings menu
2. **Action**: Tap "< Back" button
3. **Expected Serial Output**:
   ```
   I (xxxxx) screen_mgr: Going back to screen 0
   ```
4. **Expected UI**:
   - Clock screen appears
   - Time/date/weather still updating
5. **Pass Criteria**: Clock functionality resumed

### Phase 4: Brightness Control Testing

**Objective**: Verify brightness slider functionality

#### Test 4.1: Brightness Slider Interaction

1. **Action**: Navigate to Settings → Brightness
2. **Expected UI**:
   - Slider visible in center
   - Current brightness label showing "Brightness: 50%"
3. **Action**: Drag slider to different positions
4. **Expected Serial Output** (for each change):
   ```
   I (xxxxx) screen_mgr: Brightness changed to: XX%
   ```
5. **Expected Behavior**:
   - Screen brightness changes immediately
   - Label updates to show new percentage
6. **Pass Criteria**:
   - Brightness changes smoothly
   - No flickering or lag
   - Label updates correctly

#### Test 4.2: Brightness Range Testing

1. **Action**: Set slider to minimum (10%)
2. **Expected**: Screen dim but still visible
3. **Action**: Set slider to maximum (100%)
4. **Expected**: Screen at full brightness
5. **Pass Criteria**: Full range functional, no dead zones

### Phase 5: About Screen Testing

**Objective**: Verify system information is displayed correctly

#### Test 5.1: System Info Display

1. **Action**: Navigate to Settings → About
2. **Expected UI Elements**:
   - "Greenwood Clock" title
   - "Version: 1.0.0"
   - "Platform: ESP32-P4"
   - "Board: Function EV Board"
   - MAC address (6 hex octets)
   - IP address (if connected)
   - Free Heap (in KB)
   - Min Heap (in KB)
3. **Pass Criteria**: All fields populated with valid data

#### Test 5.2: Network Info Validation

1. **Prerequisite**: Device connected to WiFi
2. **Check**: IP address shows valid IP (not "N/A")
3. **Expected Format**: "192.168.X.X" or similar
4. **Pass Criteria**: Valid IP address displayed

### Phase 6: Stress Testing

**Objective**: Ensure system stability under heavy interaction

#### Test 6.1: Rapid Navigation

1. **Action**: Rapidly navigate between screens (open/close settings multiple times)
2. **Duration**: 30 seconds
3. **Expected**: No crashes, freezes, or visual glitches
4. **Monitor Serial**: Watch for memory issues or errors
5. **Pass Criteria**: System remains responsive

#### Test 6.2: Heap Monitoring During Use

1. **Action**: Use touchscreen for 5 minutes (navigate all screens)
2. **Monitor Serial**: Check heap monitoring logs
3. **Expected Serial Output** (every 60 seconds):
   ```
   I (xxxxx) main: [HEALTH] Loop X: Heap free=XXXXX, min=XXXXX, delta=XX
   ```
4. **Pass Criteria**:
   - Free heap remains above 20KB
   - No continuous downward trend (memory leak)
   - Delta changes small (<5KB)

#### Test 6.3: Touch During Updates

1. **Starting State**: Clock screen showing
2. **Action**: Wait for weather update (every 30 minutes) or clock update (every minute)
3. **Action**: Interact with touch during update
4. **Expected**: Touch remains responsive
5. **Pass Criteria**: No freezing or lag during background updates

### Phase 7: WiFi Settings Screen Testing

**Objective**: Verify WiFi screen displays current status

#### Test 7.1: Connection Status Display

1. **Action**: Navigate to Settings → WiFi Settings
2. **Expected UI**:
   - Current SSID: "Dudeybear"
   - Status: "Connected"
   - Note about hardcoded credentials
3. **Pass Criteria**: Information accurate and readable

### Phase 8: Edge Cases and Error Handling

**Objective**: Test unusual scenarios

#### Test 8.1: Multi-Touch Prevention

1. **Action**: Try touching two areas simultaneously
2. **Expected**: System handles gracefully (may only register one touch)
3. **Pass Criteria**: No crash or undefined behavior

#### Test 8.2: Touch While Screen Animating

1. **Action**: Tap back button while screen is sliding
2. **Expected**: Touch ignored until animation completes, OR queued and processed after
3. **Pass Criteria**: No crash or visual glitches

#### Test 8.3: Screen Stack Depth

1. **Action**: Navigate Settings → WiFi → Back → Brightness → Back → About → Back → Back
2. **Expected Serial Output**: Stack properly manages push/pop operations
3. **Pass Criteria**: Return to clock screen without errors

## Performance Metrics

### Touch Latency

- **Target**: <100ms from touch to visual feedback
- **Measurement**: Observe responsiveness subjectively
- **Pass Criteria**: Touch feels immediate and responsive

### Animation Smoothness

- **Target**: 30+ FPS during screen transitions
- **Measurement**: Visual observation of slide animations
- **Pass Criteria**: No stuttering or frame drops

### Memory Usage

- **Target**: Free heap remains above 20KB during operation
- **Measurement**: Monitor serial logs
- **Pass Criteria**: No memory leaks over extended use

## Common Issues and Solutions

### Issue: Touch Not Detected

**Symptoms**: No serial logs when touching screen

**Solutions**:
1. Check hardware jumper wire connection
2. Verify `lv_indev_enable(t, true)` in main.cpp:130
3. Check BSP touch driver initialization

### Issue: Touch Detected But Inaccurate

**Symptoms**: Touch position offset from actual touch location

**Solutions**:
1. May require touch calibration (not currently implemented)
2. Check touch panel resolution configuration

### Issue: Gestures Not Recognized

**Symptoms**: Taps work but swipes don't trigger

**Solutions**:
1. Swipe faster/more deliberately
2. Check LVGL gesture threshold configuration

### Issue: UI Freezes During Touch

**Symptoms**: Screen becomes unresponsive after touch

**Solutions**:
1. Check for LVGL lock issues (network ops during lock)
2. Monitor heap - may be out of memory
3. Check serial logs for crash/assert

### Issue: Screen Doesn't Return to Clock

**Symptoms**: Stuck in settings menu

**Solutions**:
1. Check screen stack implementation
2. Verify clock screen reference properly set
3. Check for navigation logic errors

## Regression Testing

After any code changes, run this abbreviated test suite:

1. **Basic Touch**: Tap screen, verify PRESSED/RELEASED logs
2. **Settings Access**: Long press, verify settings opens
3. **Navigation**: Test all screens and back navigation
4. **Brightness**: Slide and verify brightness changes
5. **Stability**: Use for 5 minutes, check heap

## Test Results Template

```
=== Touchscreen Test Results ===
Date: YYYY-MM-DD
Tester: [Name]
Firmware Version: [commit hash]

Phase 1: Basic Touch Detection
- Test 1.1: [PASS/FAIL]
- Test 1.2: [PASS/FAIL]

Phase 2: Gesture Detection
- Test 2.1: [PASS/FAIL]
- Test 2.2: [PASS/FAIL]

Phase 3: Navigation Testing
- Test 3.1: [PASS/FAIL]
- Test 3.2: [PASS/FAIL]
- Test 3.3: [PASS/FAIL]
- Test 3.4: [PASS/FAIL]

Phase 4: Brightness Control
- Test 4.1: [PASS/FAIL]
- Test 4.2: [PASS/FAIL]

Phase 5: About Screen
- Test 5.1: [PASS/FAIL]
- Test 5.2: [PASS/FAIL]

Phase 6: Stress Testing
- Test 6.1: [PASS/FAIL]
- Test 6.2: [PASS/FAIL]
- Test 6.3: [PASS/FAIL]

Phase 7: WiFi Settings
- Test 7.1: [PASS/FAIL]

Phase 8: Edge Cases
- Test 8.1: [PASS/FAIL]
- Test 8.2: [PASS/FAIL]
- Test 8.3: [PASS/FAIL]

Performance Metrics:
- Touch Latency: [<100ms / >100ms]
- Animation Smoothness: [Smooth / Stuttering]
- Memory Stability: [Stable / Leaking]

Notes:
[Any observations or issues encountered]
```

## Automated Testing (Future)

Currently, testing is manual. Future improvements could include:

- Automated touch injection via test framework
- Screenshot comparison for UI verification
- Memory profiling automation
- Performance benchmarking tools

---

**Created**: 2025-12-02
**Last Updated**: 2025-12-02
**Project**: Greenwood Clock
**Platform**: ESP32-P4
