# Background Image Selector

**Status**: Backlog
**Priority**: Medium
**Estimated Effort**: Medium (2-3 days)
**Created**: 2025-12-02

## Problem Statement

The Greenwood Clock currently uses a hardcoded splash image as the background for the clock face. Users have no ability to:
- Choose different background images
- Upload custom images
- Preview backgrounds before applying
- Manage their background image collection

Users want to personalize their clock with custom backgrounds that match their aesthetic preferences, room decor, or mood.

## User Feedback

From hardware testing session (2025-12-02):
> "I also want to add an option to the settings page that allows the background image to be set (new proposal)"

## Proposed Solution

### 1. Background Image Storage

**SPIFFS Directory Structure:**
```
/spiffs/
├── backgrounds/          ← NEW directory
│   ├── default.png       ← Current splash image (1024x600)
│   ├── nature.png        ← Pre-loaded background
│   ├── space.png         ← Pre-loaded background
│   ├── abstract.png      ← Pre-loaded background
│   └── user_001.png      ← User-uploaded image
├── animations/           ← Existing lottie animations
└── fonts/                ← Existing font files
```

**Storage Capacity:**
- SPIFFS partition: 3 MB
- Current usage: ~1.5 MB (animations + fonts)
- Available: ~1.5 MB for backgrounds
- Each background: ~100-300 KB (PNG, 1024x600)
- **Capacity**: 5-15 background images

### 2. Settings Integration

Extend `clock_settings_t` (from `advanced-ui-customization.md`):

```c
typedef struct {
    // ... existing fields ...

    struct {
        // ... color/font settings from advanced-ui-customization.md ...

        // Background settings
        ui_background_type_t bg_type;       // BG_IMAGE or BG_SOLID_COLOR
        char bg_image_path[64];             // e.g., "/spiffs/backgrounds/nature.png"
        uint32_t bg_color;                  // Fallback solid color
        uint8_t bg_opacity;                 // 0-100%, default 100
    } ui;
} clock_settings_t;
```

### 3. Background Selector UI

**Settings Menu Navigation:**
```
Settings
└── Appearance
    └── Background ← NEW
        ├── [Image Gallery]
        │   ├── Default (thumbnail)
        │   ├── Nature (thumbnail)
        │   ├── Space (thumbnail)
        │   ├── Abstract (thumbnail)
        │   └── [+ Upload Image] (future)
        ├── Solid Color
        │   └── [Color Picker]
        └── Opacity
            └── [Slider 0-100%]
```

**Background Selector Screen Design:**

```
┌─────────────────────────────────────────┐
│ < Back           Backgrounds             │
├─────────────────────────────────────────┤
│                                          │
│  ┌────────┐  ┌────────┐  ┌────────┐    │
│  │ Default│  │ Nature │  │ Space  │    │
│  │  [✓]   │  │        │  │        │    │
│  └────────┘  └────────┘  └────────┘    │
│                                          │
│  ┌────────┐  ┌────────┐  ┌────────┐    │
│  │Abstract│  │ Custom │  │  + Add │    │
│  │        │  │        │  │ Upload │    │
│  └────────┘  └────────┘  └────────┘    │
│                                          │
│  ────────────────────────────────────   │
│                                          │
│  Opacity:  [========|---]  80%          │
│                                          │
│  [ Apply ]                               │
└─────────────────────────────────────────┘
```

**Features:**
- Grid layout of thumbnail previews (200x120px)
- Checkmark on currently selected background
- Tap to preview full-size
- Opacity slider affects background only (not text)
- "Apply" button commits changes

### 4. Image Preview System

**Preview Workflow:**
1. User taps a background thumbnail
2. Full-screen preview appears with semi-transparent overlay
3. Clock elements visible on top (time, date, weather)
4. User sees exactly how it will look
5. "Apply" button in preview overlay
6. "Cancel" returns to selector

**Implementation:**
```c
static void show_background_preview(const char* image_path) {
    lvgl_port_lock(0);

    // Create preview screen
    lv_obj_t* preview_scr = lv_obj_create(NULL);

    // Load background image
    lv_obj_t* img = lv_img_create(preview_scr);
    lv_img_set_src(img, image_path);
    lv_obj_align(img, LV_ALIGN_CENTER, 0, 0);

    // Apply opacity
    lv_obj_set_style_opa(img, LV_OPA_80, 0);  // 80% opacity

    // Add semi-transparent overlay with buttons
    lv_obj_t* overlay = lv_obj_create(preview_scr);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_50, 0);

    // "Apply" and "Cancel" buttons
    // ...

    lv_scr_load(preview_scr);
    lvgl_port_unlock();
}
```

### 5. Pre-loaded Background Images

**Default Collection (4 images):**

1. **Default** - Current splash image
   - Style: Abstract geometric
   - Colors: Blue/purple gradient
   - File: `default.png`

2. **Nature** - Peaceful forest/mountain scene
   - Style: Photography
   - Colors: Green/blue earth tones
   - File: `nature.png`

3. **Space** - Starfield or nebula
   - Style: Cosmic photography
   - Colors: Deep blue/purple with stars
   - File: `space.png`

4. **Abstract** - Modern geometric pattern
   - Style: Minimalist design
   - Colors: Black/white/accent
   - File: `abstract.png`

**Image Requirements:**
- Resolution: 1024x600 pixels (exact display size)
- Format: PNG with transparency support
- Size: <300 KB each (compressed)
- Color space: RGB888
- License: Public domain or CC0

### 6. Background Application Logic

**UI Update Function:**
```c
void ui_set_background_image(const char* image_path, uint8_t opacity) {
    ESP_LOGI(TAG, "Setting background: %s (opacity: %d%%)", image_path, opacity);

    lvgl_port_lock(0);

    // Remove old background if exists
    if (bg_image_obj != NULL) {
        lv_obj_del(bg_image_obj);
    }

    // Create new background image
    bg_image_obj = lv_img_create(screen_clock);
    lv_img_set_src(bg_image_obj, image_path);
    lv_obj_align(bg_image_obj, LV_ALIGN_CENTER, 0, 0);

    // Set opacity (0-255, where 255 = 100%)
    uint8_t lvgl_opa = (opacity * 255) / 100;
    lv_obj_set_style_opa(bg_image_obj, lvgl_opa, 0);

    // Move to back so clock elements are on top
    lv_obj_move_background(bg_image_obj);

    lvgl_port_unlock();

    ESP_LOGI(TAG, "Background applied successfully");
}
```

**Settings Persistence:**
```c
// Save to NVS
esp_err_t settings_save(const clock_settings_t* cfg) {
    // ... existing code ...

    // Save background settings
    nvs_set_str(handle, "bg_image", cfg->ui.bg_image_path);
    nvs_set_u8(handle, "bg_opacity", cfg->ui.bg_opacity);

    // ...
}

// Load from NVS
esp_err_t settings_load(clock_settings_t* cfg) {
    // ... existing code ...

    // Load background settings with defaults
    size_t len = sizeof(cfg->ui.bg_image_path);
    if (nvs_get_str(handle, "bg_image", cfg->ui.bg_image_path, &len) != ESP_OK) {
        strcpy(cfg->ui.bg_image_path, "/spiffs/backgrounds/default.png");
    }

    if (nvs_get_u8(handle, "bg_opacity", &cfg->ui.bg_opacity) != ESP_OK) {
        cfg->ui.bg_opacity = 100;  // Default: fully opaque
    }

    // ...
}
```

### 7. Image Upload Feature (Future Phase)

**Upload Methods:**

1. **WiFi HTTP Upload** (Preferred)
   - Simple web interface on device
   - User accesses `http://[device-ip]/upload`
   - Drag & drop or file picker
   - Image validated and saved to SPIFFS

2. **USB Serial Upload** (Alternative)
   - Use `esptool.py` or custom tool
   - Upload to SPIFFS partition directly
   - Requires USB connection

**Web Upload Server (Future):**
```c
// Simple HTTP server for image uploads
httpd_handle_t start_upload_server(void) {
    httpd_uri_t upload_uri = {
        .uri       = "/upload",
        .method    = HTTP_POST,
        .handler   = upload_handler,
        .user_ctx  = NULL
    };

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    httpd_handle_t server = NULL;

    httpd_start(&server, &config);
    httpd_register_uri_handler(server, &upload_uri);

    return server;
}
```

**Upload Workflow:**
1. User enables "Upload Mode" in settings
2. Device starts HTTP server on port 8080
3. Display shows: "Upload at http://192.168.1.X:8080"
4. User opens URL in browser
5. Uploads PNG file (max 500 KB)
6. Server validates image (size, format)
7. Saves to `/spiffs/backgrounds/user_XXX.png`
8. Shows confirmation
9. User disables "Upload Mode"

## Implementation Phases

### Phase 1: Core Infrastructure (1 day)
- Create `/spiffs/backgrounds/` directory structure
- Extend settings structure for background settings
- Add NVS storage/retrieval for background path and opacity
- Implement `ui_set_background_image()` function

### Phase 2: Image Gallery UI (1 day)
- Create background selector screen
- Implement thumbnail grid layout
- Add image selection logic
- Add opacity slider
- Integrate with settings menu

### Phase 3: Pre-loaded Backgrounds (0.5 days)
- Source/create 4 background images
- Optimize images for size (PNG compression)
- Add to SPIFFS via build process
- Test on hardware

### Phase 4: Preview System (0.5 days)
- Implement full-screen preview
- Add overlay with Apply/Cancel buttons
- Test preview with clock elements visible

### Phase 5 (Future): Image Upload
- Implement HTTP upload server
- Add web interface for uploads
- Add image validation and size checks
- Add "Upload Mode" toggle in settings

## Technical Considerations

### Memory Impact

**SPIFFS Usage:**
```
Current: 1.5 MB (fonts + animations)
4 backgrounds @ 250 KB each: 1.0 MB
User uploads (5 max): 1.5 MB
Total: 4.0 MB (exceeds 3 MB partition!)
```

**Solution:**
- Limit total backgrounds to 8 (including pre-loaded)
- Enforce 200 KB max per image
- Total: 1.6 MB for backgrounds (safe)

**RAM Usage:**
- Decoded PNG in RAM: 1024 × 600 × 3 bytes = ~1.8 MB
- Use LVGL image caching (already in SPIRAM)
- No additional RAM pressure

### Image Format Support

**Supported:**
- PNG (with transparency)
- BMP (no transparency)
- JPG (smaller files, no transparency)

**Recommended: PNG**
- Transparency allows for creative overlays
- Good compression for graphics
- LVGL has excellent PNG decoder

**Conversion Pipeline:**
```bash
# Optimize PNG for size
pngquant --quality=80-95 --speed 1 input.png -o output.png
optipng -o7 output.png

# Expected results:
# Original: 1.8 MB
# Optimized: 200-300 KB (85% reduction)
```

### LVGL Image Caching

**Current Behavior:**
- LVGL caches decoded images in SPIRAM
- Cache size: Configured in LVGL settings
- Automatic LRU eviction

**Optimization:**
```c
// Increase image cache for background images
lv_img_cache_set_size(8);  // Cache up to 8 images
```

### Performance Considerations

**Image Loading Time:**
- PNG decode: ~100-200ms (depends on complexity)
- Decode happens once, cached after
- No impact on clock update (1 FPS)

**Transition Smoothness:**
- Background change during settings navigation
- Use fade animation for smooth transition
- 200ms fade (matches screen transitions)

## Testing Plan

### Unit Testing
- NVS storage/retrieval of background path
- Image path validation
- Opacity conversion (0-100% → 0-255)

### Integration Testing
1. **Default Background**: Boots with default image
2. **Image Selection**:
   - Select "Nature", verify applied
   - Reboot, verify persists
3. **Opacity Changes**:
   - Set 50% opacity, verify translucency
   - Set 100%, verify fully opaque
4. **Preview System**:
   - Preview each background
   - Cancel, verify no change
   - Apply, verify change persists
5. **SPIFFS Limits**:
   - Add 8 images, verify all accessible
   - Try to add 9th, verify graceful handling

### Manual Testing on Hardware
- Thumbnail rendering performance
- Preview display quality
- Background doesn't obscure clock text
- Opacity slider responsiveness
- Settings persistence across reboots
- Settings persistence across OTA updates

## User Experience

### Workflow: Changing Background

1. User swipes up from clock screen
2. Taps "Appearance"
3. Taps "Background"
4. Background selector appears with thumbnails
5. User taps "Nature" thumbnail
6. Full-screen preview shows with clock overlay
7. User sees time/date/weather on new background
8. User taps "Apply"
9. Preview closes, background updated on clock
10. Settings auto-saved to NVS
11. User taps back to return to clock

**Estimated Time:** 12-18 seconds

### Workflow: Adjusting Opacity

1. In background selector
2. User drags opacity slider left (50%)
3. Preview updates in real-time (background fades)
4. User finds desired opacity
5. Taps "Apply"
6. Background updates with new opacity
7. Settings auto-saved

**Estimated Time:** 5-8 seconds

## Success Metrics

- Users can select from 4 pre-loaded backgrounds
- Users can adjust background opacity (0-100%)
- Background changes apply immediately
- All settings persist across reboots and OTA updates
- Thumbnail grid loads in <500ms
- Full preview loads in <300ms
- No impact on clock update performance (1 FPS maintained)
- No memory leaks (heap stable after multiple changes)

## Future Enhancements

### Phase 5: Image Upload
- HTTP server for WiFi uploads
- Web interface for image management
- Image validation and optimization
- User can upload custom images

### Phase 6: Advanced Features
- **Dynamic Backgrounds**: Change based on time of day
- **Weather-Synced Backgrounds**: Match current weather
- **Slideshow Mode**: Rotate backgrounds every N minutes
- **Image Editor**: Crop, rotate, adjust brightness
- **Cloud Sync**: Sync backgrounds across devices

### Phase 7: Community
- **Background Store**: Browse/download from repository
- **Sharing**: Export/import background packs
- **Ratings**: Community votes on best backgrounds

## Dependencies

- SPIFFS filesystem (already present)
- LVGL image decoder (PNG support)
- Settings component (NVS storage)
- Screen manager (navigation)
- Pre-loaded background images (assets)

## Risks and Mitigations

| Risk | Impact | Mitigation |
|------|--------|------------|
| SPIFFS overflow | Can't add images | Limit total images to 8, enforce 200 KB max |
| PNG decode slow | Laggy UI | Cache decoded images, show loading indicator |
| Large images crash | Out of memory | Validate size before loading, reject >500 KB |
| Opacity too low | Text unreadable | Default to 100%, warn at <40% |
| Background too bright | Text invisible | Suggest darker backgrounds, add text shadow |

## References

- Related proposal: `advanced-ui-customization.md`
- LVGL image decoder: https://docs.lvgl.io/8/overview/image.html
- PNG optimization: https://pngquant.org/
- ESP32 SPIFFS: https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/storage/spiffs.html

## Open Questions

1. Should we support animated backgrounds (GIF/APNG)?
   - **Recommendation**: No, too memory intensive

2. Should we allow video backgrounds?
   - **Recommendation**: No, beyond scope

3. What's the minimum recommended opacity for text readability?
   - **Recommendation**: 60% minimum, warn below that

4. Should we add automatic text color adjustment based on background?
   - **Recommendation**: Phase 7 enhancement, not MVP

5. How should we handle corrupted/missing images?
   - **Recommendation**: Fall back to default, log error

## Approval Checklist

- [ ] Design reviewed and approved
- [ ] SPIFFS capacity validated (3 MB sufficient)
- [ ] Background image assets sourced/created
- [ ] Implementation phases agreed upon
- [ ] Testing plan accepted
- [ ] User workflows validated
- [ ] Performance targets set (load times)

---

**Next Steps After Approval:**
1. Create `/spiffs/backgrounds/` directory
2. Source/create 4 background images
3. Optimize images for size (<200 KB each)
4. Implement background selector UI
5. Add opacity slider control
6. Test on hardware with real images
