# Advanced UI Customization Settings

**Status**: Backlog
**Priority**: Medium
**Estimated Effort**: Medium (3-5 days)
**Created**: 2025-12-02

## Problem Statement

The Greenwood Clock currently has limited user customization options. Users can adjust brightness and connect to WiFi, but cannot customize the visual appearance of the clock face itself. Power users want more control over:
- Text colors for time, date, and weather information
- Font selection for different UI elements
- Background images or colors
- Layout preferences
- Display themes (light/dark mode concepts)

This proposal aims to expose more UI customization options through the settings screen, allowing users to personalize their clock's appearance.

## User Feedback

From hardware testing session (2025-12-02):
> "Brightness controls work (New proposal, lets expose more things in this settings screen that can be controlled (text color, font, see below, background image)"

## Proposed Solution

### 1. Settings Data Structure Extension

Extend `clock_settings_t` in `components/settings/settings.h` to include UI customization options:

```c
typedef struct {
    // Existing settings
    bool wifi_configured;
    char wifi_ssid[33];
    char wifi_password[64];
    uint8_t brightness;
    bool enable_touch;
    char timezone[64];

    // NEW: UI Customization
    struct {
        // Text colors (RGB888)
        uint32_t time_color;      // Default: white (0xFFFFFF)
        uint32_t date_color;      // Default: white (0xFFFFFF)
        uint32_t weather_color;   // Default: white (0xFFFFFF)

        // Font selection (enum)
        ui_font_style_t time_font;     // Default: FONT_STYLE_NUNITO
        ui_font_style_t info_font;     // Default: FONT_STYLE_NUNITO

        // Background
        ui_background_type_t bg_type;  // Default: BG_IMAGE
        uint32_t bg_color;             // Solid color fallback
        char bg_image_path[64];        // Path to background image

        // Layout options
        bool show_seconds;             // Default: false
        bool show_weather_icon;        // Default: true
        bool show_uv_index;            // Default: true
        uint8_t time_size_scale;       // 80-120%, default 100
    } ui;
} clock_settings_t;
```

### 2. Font Style Enumeration

```c
typedef enum {
    FONT_STYLE_NUNITO,      // Current default (rounded, friendly)
    FONT_STYLE_MONTSERRAT,  // Modern sans-serif
    FONT_STYLE_ROBOTO,      // Clean, readable
    FONT_STYLE_MAX
} ui_font_style_t;

typedef enum {
    BG_SOLID_COLOR,   // Single color background
    BG_IMAGE,         // Custom image (from SPIFFS)
    BG_GRADIENT,      // Vertical gradient (future)
    BG_MAX
} ui_background_type_t;
```

### 3. Settings Screen Organization

Create a new "Appearance" submenu in settings with the following sections:

**Settings Menu Structure:**
```
Settings
├── WiFi Settings
├── Brightness
├── Appearance ← NEW
│   ├── Colors
│   │   ├── Time Color
│   │   ├── Date Color
│   │   └── Weather Color
│   ├── Fonts
│   │   ├── Time Font
│   │   └── Info Font
│   ├── Background
│   │   └── (see background-image-selector.md)
│   └── Layout
│       ├── Show Seconds
│       ├── Show Weather Icon
│       ├── Show UV Index
│       └── Time Size
├── Software Update
└── About
```

### 4. Color Picker UI Component

Create a simple color picker for selecting text colors:

**Color Picker Design:**
- Preset color swatches (8-12 common colors):
  - White, Light Gray, Yellow, Orange, Red
  - Pink, Purple, Blue, Cyan, Green
- RGB slider for custom colors (advanced)
- Live preview of selected color on clock face

**Implementation:**
```c
static lv_obj_t* create_color_picker(lv_obj_t* parent,
                                      const char* title,
                                      uint32_t current_color,
                                      void (*callback)(uint32_t color));
```

### 5. Font Selector UI Component

**Font Selector Design:**
- List of available fonts with sample preview
- Each item shows: "Nunito" with actual font rendering
- Live preview on clock face when selected

**Implementation:**
```c
static lv_obj_t* create_font_selector(lv_obj_t* parent,
                                       const char* title,
                                       ui_font_style_t current_font,
                                       void (*callback)(ui_font_style_t font));
```

### 6. UI Update Logic

Modify `components/ui/ui.c` to apply customization settings:

```c
void ui_apply_settings(const clock_settings_t* settings) {
    lvgl_port_lock(0);

    // Apply text colors
    lv_obj_set_style_text_color(lbl_time,
        lv_color_hex(settings->ui.time_color), 0);
    lv_obj_set_style_text_color(lbl_date,
        lv_color_hex(settings->ui.date_color), 0);
    lv_obj_set_style_text_color(lbl_temp,
        lv_color_hex(settings->ui.weather_color), 0);

    // Apply fonts
    const lv_font_t* time_font = get_font_for_style(
        settings->ui.time_font, FONT_SIZE_LARGE);
    lv_obj_set_style_text_font(lbl_time, time_font, 0);

    // Apply background (if image)
    if (settings->ui.bg_type == BG_IMAGE) {
        ui_set_background_image(settings->ui.bg_image_path);
    } else {
        ui_set_background_color(settings->ui.bg_color);
    }

    // Apply layout options
    lv_obj_set_hidden(lbl_weather_icon, !settings->ui.show_weather_icon);
    lv_obj_set_hidden(lbl_uv, !settings->ui.show_uv_index);

    lvgl_port_unlock();
}
```

### 7. Default Values and Migration

**Default Settings (first boot):**
```c
.ui = {
    .time_color = 0xFFFFFF,      // White
    .date_color = 0xFFFFFF,      // White
    .weather_color = 0xFFFFFF,   // White
    .time_font = FONT_STYLE_NUNITO,
    .info_font = FONT_STYLE_NUNITO,
    .bg_type = BG_IMAGE,
    .bg_color = 0x000000,        // Black
    .bg_image_path = "/spiffs/splash.png",
    .show_seconds = false,
    .show_weather_icon = true,
    .show_uv_index = true,
    .time_size_scale = 100,
}
```

**NVS Migration:**
- Check NVS version field
- If old version detected, apply defaults for new fields
- Preserve existing settings (WiFi, brightness, etc.)

## Implementation Phases

### Phase 1: Core Infrastructure (2 days)
- Extend `clock_settings_t` structure
- Add UI settings to NVS storage/retrieval
- Create `ui_apply_settings()` function
- Add migration logic for existing NVS data

### Phase 2: Color Customization (1 day)
- Create color picker component
- Add "Colors" submenu to Appearance settings
- Implement live preview on clock face
- Test color persistence and application

### Phase 3: Font Selection (1 day)
- Create font selector component
- Add additional font assets (Montserrat, Roboto)
- Add "Fonts" submenu to Appearance settings
- Implement font switching logic

### Phase 4: Layout Options (1 day)
- Create toggle switches for show/hide options
- Add time size slider (80-120%)
- Add "Layout" submenu to Appearance settings
- Test layout changes

## Technical Considerations

### Memory Impact
- Additional NVS storage: ~200 bytes for UI settings
- Font assets: ~500KB per additional font style
- Background images: handled separately (see background-image-selector.md)

**Current Memory Status:**
- Heap free: ~27MB (plenty of room)
- Flash usage: 40% (60% free)
- Additional fonts will increase flash usage to ~45-50%

### Performance Impact
- Color changes: Negligible (simple style updates)
- Font changes: Negligible (already using custom fonts)
- Layout changes: Minor (hiding/showing objects)

### Font Asset Requirements

**Additional Fonts Needed:**
1. **Montserrat**: Modern, geometric sans-serif
   - 256pt for time display
   - 48pt for date/weather

2. **Roboto**: Android system font, highly readable
   - 256pt for time display
   - 48pt for date/weather

**Font Generation:**
- Use LVGL font converter: https://lvgl.io/tools/fontconverter
- Generate C arrays for inclusion in firmware
- Store in `components/fonts/` directory

### LVGL API Usage

**Color Conversion:**
```c
lv_color_t color = lv_color_hex(0xFF5733);
lv_obj_set_style_text_color(obj, color, 0);
```

**Font Switching:**
```c
LV_FONT_DECLARE(montserrat_256);
lv_obj_set_style_text_font(obj, &montserrat_256, 0);
```

## Testing Plan

### Unit Testing
- NVS storage/retrieval of UI settings
- Color conversion (RGB888 ↔ LVGL color)
- Font enumeration and selection
- Settings migration from old versions

### Integration Testing
1. **First Boot**: Verify defaults applied correctly
2. **Color Changes**:
   - Change time color to red, verify persistence
   - Change all colors, reboot, verify all persist
3. **Font Changes**:
   - Switch to Montserrat, verify rendering
   - Switch back to Nunito, verify no issues
4. **Layout Changes**:
   - Hide weather icon, verify layout adjusts
   - Show seconds, verify time format changes
5. **Migration**:
   - Flash old firmware, configure WiFi/brightness
   - Flash new firmware, verify old settings preserved
   - Verify new UI settings have defaults

### Manual Testing on Hardware
- Color picker responsiveness on touchscreen
- Font readability at different sizes
- Layout changes don't break positioning
- Settings persist across power cycles
- Settings persist across OTA updates

## User Experience

### Workflow: Changing Time Color

1. User swipes up from clock screen
2. Taps "Appearance"
3. Taps "Colors"
4. Taps "Time Color"
5. Color picker opens with current color highlighted
6. User taps a preset color (e.g., cyan)
7. Preview shows on clock screen (background fade)
8. User taps "Apply"
9. Color updates immediately
10. Settings auto-saved to NVS
11. User taps back to return to clock

**Estimated Time:** 10-15 seconds

### Workflow: Changing Font

1. User swipes up from clock screen
2. Taps "Appearance"
3. Taps "Fonts"
4. Taps "Time Font"
5. Font selector shows 3 options with previews
6. User taps "Montserrat"
7. Preview shows on clock screen
8. User taps "Apply"
9. Font updates immediately
10. Settings auto-saved to NVS

**Estimated Time:** 8-12 seconds

## Success Metrics

- Users can customize time color (3+ color options working)
- Users can customize date/weather colors independently
- Users can switch between 3 font styles
- Users can toggle weather icon visibility
- All settings persist across reboots and OTA updates
- No performance degradation (transitions still smooth)
- No memory leaks (heap stable after multiple changes)

## Future Enhancements

### Phase 5 (Future)
- **Theme Presets**: "Classic", "Colorful", "Minimal", "Retro"
- **Gradient Backgrounds**: Two-color vertical gradients
- **Animation Customization**: Transition speed, animation style
- **Clock Face Layouts**: Different time/info arrangements
- **Auto Dark Mode**: Based on time of day

### Advanced Features
- **Import/Export Settings**: Share customizations via JSON
- **Community Themes**: Download themes from repository
- **Dynamic Colors**: Auto-adjust based on weather/time

## Dependencies

- Existing settings component (NVS storage)
- LVGL v8.x (color picker, font support)
- Screen manager (navigation)
- Font assets (Montserrat, Roboto)

## Risks and Mitigations

| Risk | Impact | Mitigation |
|------|--------|------------|
| Font assets too large | Flash overflow | Start with 2 fonts, make optional |
| Color picker too complex | Poor UX | Use simple preset swatches first |
| Settings migration fails | Lost settings | Add rollback mechanism |
| Performance impact | Laggy UI | Profile and optimize critical paths |
| NVS corruption | Bricked device | Add settings validation and defaults |

## References

- Related proposal: `background-image-selector.md`
- LVGL color picker: https://docs.lvgl.io/8/widgets/colorwheel.html
- LVGL font converter: https://lvgl.io/tools/fontconverter
- Material Design colors: https://m2.material.io/design/color/

## Open Questions

1. Should we support custom RGB values or just presets?
   - **Recommendation**: Start with presets, add RGB sliders in Phase 5

2. How many fonts should we include initially?
   - **Recommendation**: 3 total (Nunito + 2 new), expandable later

3. Should color changes apply immediately or require "Apply" button?
   - **Recommendation**: Live preview + explicit "Apply" for consistency

4. Should we support per-element font sizes?
   - **Recommendation**: Start with scale factor (80-120%), add granular control later

## Approval Checklist

- [ ] Design reviewed and approved
- [ ] Implementation phases agreed upon
- [ ] Testing plan accepted
- [ ] Resources allocated (fonts, development time)
- [ ] Dependencies identified and available
- [ ] Migration strategy validated
- [ ] User workflows validated on hardware

---

**Next Steps After Approval:**
1. Generate font assets (Montserrat, Roboto)
2. Extend `clock_settings_t` structure
3. Implement color picker component
4. Add Appearance submenu to settings
5. Test on hardware with real user feedback
