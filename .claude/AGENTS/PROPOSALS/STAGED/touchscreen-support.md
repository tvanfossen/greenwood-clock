---
proposal_id: "touchscreen-support"
title: "Greenwood Clock - Touchscreen UI & Settings Menu"
github_issue: null
created: "2025-12-01"
updated: "2025-12-01"

status: "STAGED"
priority: "medium"
complexity: "high"

category: ["feature", "ui"]
tags: ["esp32", "lvgl", "touchscreen", "settings-ui"]

estimated_hours: 12
actual_hours: 10
progress_percent: 100

depends_on: ["persistent-settings-nvs"]
blocks: []

commits: []
branches: []
pull_requests: []

agent_notes:
  - timestamp: "2025-12-01T06:30:00Z"
    agent: "gh_verify"
    note: "Moved to ACTIVE_WORK per architect request. Note: depends on persistent-settings-nvs which is still in BACKLOG. Consider implementing basic touch UI first, then integrate with settings later."
    action: "started"

stall_reason: null
unblock_requirements: []

completion_date: null
verification_status: "pending"
---

# Greenwood Clock - Touchscreen UI & Settings Menu

## Problem Statement

The greenwood-clock hardware includes a touchscreen display, but touch input is currently disabled (see main.cpp:99). Users have no way to interact with the device to:
- Adjust display brightness
- Configure Wi-Fi and location
- Toggle features on/off
- View additional weather details
- Trigger manual updates

A settings UI is needed to make the device user-friendly and eliminate the need for recompiling/reflashing for configuration changes.

## Proposed Solution

Implement a comprehensive touchscreen-based settings UI using LVGL:
- Enable touch input driver
- Design settings menu navigation
- Create configuration screens for all user-adjustable settings
- Implement save/cancel/reset functionality
- Add visual feedback and animations
- Ensure UI is responsive and intuitive

## Current State

### Touch Input
- **Status**: Disabled in main.cpp:99 (`lv_indev_enable(t, false)`)
- **Hardware**: Touchscreen present on ESP32-P4 EV board
- **Driver**: BSP provides touch input device
- **Reason for disable**: Unknown (possibly to prevent accidental touches)

### Current UI
- **Clock Screen**: Time, date, weather, UV index
- **Interaction**: None (display only)
- **Screens**: Single screen, no navigation
- **LVGL Version**: 8.x

## Implementation Plan

### Phase 1: Enable Touch Input
- [ ] Remove touch disable code from main.cpp
- [ ] Configure touch calibration if needed
- [ ] Test touch input responsiveness
- [ ] Implement touch event logging
- [ ] Add debouncing/filtering

### Phase 2: Navigation Framework
- [ ] Design screen hierarchy (Main → Settings → Sub-menus)
- [ ] Implement screen manager (push/pop stack)
- [ ] Create navigation gestures (swipe, tap)
- [ ] Add back button / home button
- [ ] Implement screen transitions/animations

### Phase 3: Settings Menu
- [ ] **Main Settings Screen**
  - List of setting categories
  - Icons for each category
  - Scrollable list

- [ ] **Wi-Fi Settings**
  - SSID selection (scan networks)
  - Password input (keyboard)
  - Connection status display
  - Forget network option

- [ ] **Location Settings**
  - Latitude/longitude input (keyboard)
  - Timezone selection (dropdown/picker)
  - City search (future: geocoding API)

- [ ] **Display Settings**
  - Brightness slider
  - Theme selection (future)
  - Update interval configuration

- [ ] **Feature Toggles**
  - Enable/disable weather
  - Enable/disable moon phase
  - Enable/disable touch (lock mode)

- [ ] **About/Info Screen**
  - Firmware version
  - IP address, MAC address
  - Uptime, free memory
  - Factory reset button

### Phase 4: Input Widgets
- [ ] Implement LVGL keyboard for text input
- [ ] Create number input widget (lat/long)
- [ ] Create dropdown menus (timezone)
- [ ] Create sliders (brightness)
- [ ] Create toggles/switches (features)
- [ ] Add input validation and error messages

### Phase 5: Save/Cancel/Reset
- [ ] Implement save button (persist to NVS)
- [ ] Implement cancel button (revert changes)
- [ ] Implement factory reset with confirmation dialog
- [ ] Add "Apply" for settings requiring restart
- [ ] Show success/error notifications

### Phase 6: Polish & UX
- [ ] Add loading spinners for network operations
- [ ] Implement smooth animations (slide, fade)
- [ ] Add haptic feedback (if hardware supports)
- [ ] Optimize for touch targets (minimum 44x44px)
- [ ] Add visual states (pressed, disabled)
- [ ] Implement screen timeout (return to clock)

## Acceptance Criteria

- [ ] Touch input responsive (<100ms latency)
- [ ] Settings menu accessible via touch gesture
- [ ] All settings configurable without recompiling
- [ ] Changes persist to NVS on save
- [ ] Factory reset accessible and functional
- [ ] UI intuitive (no documentation needed for basic use)
- [ ] No crashes or freezes from UI interactions
- [ ] Screen timeout returns to clock after 60s inactivity

## Technical Details

### Screen Hierarchy
```
Clock Screen (Main)
  └─ Swipe Up → Settings Menu
        ├─ Wi-Fi Settings
        │    ├─ Scan Networks
        │    ├─ Enter Password (Keyboard)
        │    └─ Connect
        ├─ Location Settings
        │    ├─ Enter Latitude
        │    ├─ Enter Longitude
        │    └─ Select Timezone
        ├─ Display Settings
        │    ├─ Brightness Slider
        │    └─ Update Intervals
        ├─ Features
        │    ├─ Toggle Weather
        │    ├─ Toggle Moon Phase
        │    └─ Lock Screen
        └─ About
             ├─ System Info
             └─ Factory Reset
```

### LVGL Screen Manager
```c
// components/ui/screen_manager.h

typedef enum {
    SCREEN_CLOCK,
    SCREEN_SETTINGS_MENU,
    SCREEN_WIFI_SETTINGS,
    SCREEN_LOCATION_SETTINGS,
    SCREEN_DISPLAY_SETTINGS,
    SCREEN_FEATURES,
    SCREEN_ABOUT,
} screen_id_t;

void screen_manager_init(void);
void screen_manager_push(screen_id_t screen);
void screen_manager_pop(void);
void screen_manager_home(void);
```

### Touch Gesture Example
```c
// Swipe up to open settings
static void clock_screen_gesture_cb(lv_event_t* e) {
    lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());

    if (dir == LV_DIR_TOP) {
        ESP_LOGI(TAG, "Swipe up detected, opening settings");
        screen_manager_push(SCREEN_SETTINGS_MENU);
    }
}
```

### Settings UI Example
```c
// Wi-Fi settings screen
lv_obj_t* wifi_settings_create(void) {
    lv_obj_t* screen = lv_obj_create(NULL);

    // Title
    lv_obj_t* title = lv_label_create(screen);
    lv_label_set_text(title, "Wi-Fi Settings");

    // SSID input
    lv_obj_t* ssid_input = lv_textarea_create(screen);
    lv_textarea_set_placeholder_text(ssid_input, "SSID");

    // Password input
    lv_obj_t* pass_input = lv_textarea_create(screen);
    lv_textarea_set_placeholder_text(pass_input, "Password");
    lv_textarea_set_password_mode(pass_input, true);

    // Connect button
    lv_obj_t* btn = lv_btn_create(screen);
    lv_obj_t* btn_label = lv_label_create(btn);
    lv_label_set_text(btn_label, "Connect");
    lv_obj_add_event_cb(btn, wifi_connect_cb, LV_EVENT_CLICKED, NULL);

    return screen;
}
```

### Keyboard Integration
```c
// Show keyboard for text input
void show_keyboard_for_input(lv_obj_t* textarea) {
    lv_obj_t* kb = lv_keyboard_create(lv_scr_act());
    lv_keyboard_set_textarea(kb, textarea);
    lv_keyboard_set_mode(kb, LV_KEYBOARD_MODE_TEXT_UPPER);
}
```

## Risks & Mitigations

| Risk | Impact | Likelihood | Mitigation |
|------|--------|------------|------------|
| Touch calibration issues | High | Medium | Add calibration screen, test on multiple units |
| Accidental touches | Medium | High | Add touch lock mode, require swipe gestures |
| UI complexity overwhelming | Medium | Medium | Keep settings organized, use progressive disclosure |
| Performance degradation | Medium | Low | Profile LVGL rendering, optimize widget count |
| Invalid input crashes | High | Low | Validate all inputs, sanitize strings |

## Success Metrics

- Touch response latency: <100ms
- Settings accessible within 3 taps
- Configuration time: <2 minutes for full setup
- User can reset to defaults within 10 seconds
- Zero crashes from UI interaction over 30 days
- UI remains responsive during network operations

## Related Work

- LVGL Documentation: https://docs.lvgl.io/
- ESP32-P4 BSP Touch Input: Check esp-dev-kits repository
- LVGL Examples: https://github.com/lvgl/lvgl/tree/master/examples

## Notes

### UI Design Considerations

1. **Touch Targets**: Minimum 44x44px for reliable touch
2. **Feedback**: Visual feedback on every touch (color change, animation)
3. **Accessibility**: High contrast, readable fonts (minimum 16pt)
4. **Error Handling**: Clear error messages, no cryptic codes
5. **Reversibility**: Easy to cancel/undo changes

### Future Enhancements

- Voice control (if microphone available)
- Gesture customization (user-defined gestures)
- Themes (dark mode, light mode, custom colors)
- Widgets (additional data displays)
- Shortcuts (quick access to common settings)
- Multi-language support

### Testing Checklist

- [ ] Test all touch gestures (tap, swipe, long-press)
- [ ] Test keyboard input (letters, numbers, symbols)
- [ ] Test dropdown scrolling and selection
- [ ] Test slider responsiveness
- [ ] Test screen transitions (no visual glitches)
- [ ] Test with different screen sizes (if applicable)
- [ ] Test screen timeout and wake
- [ ] Test settings persistence (reboot after save)

---

**Repository**: https://github.com/tvanfossen/greenwood-clock

**Created**: 2025-12-01

**Status**: Backlog (low priority, depends on persistent-settings-nvs)
