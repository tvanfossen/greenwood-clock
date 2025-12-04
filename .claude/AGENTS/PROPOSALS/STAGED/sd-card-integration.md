# SD Card Integration

**Status**: STAGED (Implemented, not actively used)
**Priority**: Medium
**Estimated Effort**: Medium (2-3 days)
**Created**: 2025-12-02
**Implemented**: 2025-12-02

## Implementation Summary

✅ **COMPLETED** - SD card integration has been successfully implemented using the BSP's built-in SD card support.

### What Was Implemented

**Files Created:**
- `components/sdcard/sdcard.h` - SD card API
- `components/sdcard/sdcard.c` - Implementation using BSP functions
- `components/sdcard/CMakeLists.txt` - Build configuration

**Integration Points:**
- `main/main.cpp` - Boot sequence integration (step 1.5)
- `main/CMakeLists.txt` - Added sdcard dependency

**Key Features:**
- ✅ SD card mount/unmount using BSP `bsp_sdcard_mount()`
- ✅ Card info retrieval (capacity, type, usage)
- ✅ Standard directory structure auto-creation
- ✅ Graceful fallback when SD card not present
- ✅ Status tracking and error handling

### Current Implementation

**Component Structure:**
```
components/sdcard/
├── CMakeLists.txt          # Dependencies: fatfs, sdmmc, vfs, esp32_p4_function_ev_board
├── sdcard.h                # Public API
└── sdcard.c                # Implementation (uses BSP functions)
```

**API Functions:**
```c
esp_err_t sdcard_init(void);
esp_err_t sdcard_mount(bool format_if_failed);
esp_err_t sdcard_unmount(void);
esp_err_t sdcard_get_info(sdcard_info_t* info);
bool sdcard_is_mounted(void);
esp_err_t sdcard_format(void);  // Not yet implemented via BSP
esp_err_t sdcard_create_directories(void);
```

**Directory Structure (Auto-created):**
```
/sdcard/
├── backgrounds/   # User background images
├── logs/          # Debug log exports (future)
├── settings/      # Settings backups (future)
├── screenshots/   # Screenshots (future)
└── firmware/      # OTA firmware staging (future)
```

**Boot Sequence Integration:**
```cpp
// main/main.cpp lines 128-151
sdcard_init();
err = sdcard_mount(false);  // Don't auto-format
if (err == ESP_OK) {
    sdcard_info_t info;
    sdcard_get_info(&info);
    ESP_LOGI(TAG, "SD card mounted: %s, %.2f GB", ...);
    sdcard_create_directories();
} else {
    ESP_LOGW(TAG, "Continuing without SD card support");
}
```

### Hardware Details

**ESP32-P4 Function EV Board SD Card:**
- **Interface**: SDMMC (managed by BSP)
- **Speed**: 40 MHz in 4-bit mode
- **Capacity**: Supports SD, SDHC, SDXC (tested with 16 GB SDHC)
- **Mount Point**: `/sdcard`

**Tested Configuration:**
- Card: 16 GB SDHC (SanDisk SC16G)
- Speed: 40.00 MHz
- Type: SDHC
- Status: Mounts successfully on boot

### Memory Impact

**Stack Size Adjustment:**
- Main task stack increased from 3584 to 8192 bytes
- SD card operations require additional stack space
- No heap issues (27+ MB free)

### What's NOT Implemented Yet

**Features Ready for Future Use:**
- ❌ Background image loading from SD card
- ❌ Debug log export to SD card
- ❌ Settings backup/restore to SD card
- ❌ Format functionality (BSP limitation)
- ❌ Hot-plug detection
- ❌ File browser UI

**Technical Limitations:**
- Format function returns `ESP_ERR_NOT_SUPPORTED` (BSP doesn't expose format API)
- Card must be present at boot (no hot-plug detection)
- User must format SD card via PC (FAT32 recommended)

## Current Status

**Implementation**: ✅ Complete
**Testing**: ✅ Hardware tested, working
**Integration**: ⚠️ Integrated but not actively used
**Next Steps**: Waiting for features that utilize SD card storage

### Use Cases Waiting for Implementation

1. **Background Image Selector** (proposal exists)
   - Load custom backgrounds from `/sdcard/backgrounds/`
   - See: `background-image-selector.md`

2. **Debug Log Viewer** (proposal exists)
   - Export logs to `/sdcard/logs/`
   - See: `debug-log-viewer.md`

3. **Settings Backup** (future)
   - Export/import settings JSON to `/sdcard/settings/`

4. **OTA Firmware Staging** (future)
   - Download firmware to SD card first
   - Flash from SD without network

## Testing Results

**Hardware Test (2025-12-02):**
```
I (1850) sdcard: Mounting SD card using BSP...
Name: SC16G
Type: SDHC
Speed: 40.00 MHz
Size: 15193MB
I (1860) sdcard: SD card mounted at /sdcard
I (1870) sdcard: Creating directory: /sdcard/backgrounds
I (1880) sdcard: Creating directory: /sdcard/logs
I (1890) sdcard: Creating directory: /sdcard/settings
I (1900) sdcard: Creating directory: /sdcard/screenshots
I (1910) sdcard: Creating directory: /sdcard/firmware
```

**Success Metrics:**
- ✅ SD card mounts successfully on boot
- ✅ Card info retrieved correctly
- ✅ Directories created automatically
- ✅ No stack overflow (fixed with 8KB stack)
- ✅ Graceful fallback when card not present

## Technical Notes

### Using BSP Functions

The implementation uses the ESP32-P4 Function EV Board BSP instead of direct SDMMC driver calls:

**Advantages:**
- ✅ Correct pin configuration handled by BSP
- ✅ No need to manage SDMMC host initialization
- ✅ Simpler, more maintainable code
- ✅ Future BSP updates benefit us

**Disadvantages:**
- ❌ Format function not exposed by BSP
- ❌ Less control over low-level SDMMC settings
- ❌ Tied to BSP version

### Card Type Detection

Uses capacity-based detection (not OCR flags):
```c
uint64_t capacity_mb = (card->csd.capacity * card->csd.sector_size) / (1024 * 1024);
if (capacity_mb > 32 * 1024) {
    strcpy(info->card_type, "SDXC");
} else if (capacity_mb > 2 * 1024) {
    strcpy(info->card_type, "SDHC");
} else {
    strcpy(info->card_type, "SD");
}
```

## Integration Opportunities

### Ready to Use

The SD card component is ready for:

1. **File Storage APIs**:
   ```c
   FILE* f = fopen("/sdcard/logs/debug.txt", "w");
   fprintf(f, "Log entry\n");
   fclose(f);
   ```

2. **Directory Operations**:
   ```c
   DIR* dir = opendir("/sdcard/backgrounds");
   struct dirent* entry;
   while ((entry = readdir(dir)) != NULL) {
       // Process files
   }
   closedir(dir);
   ```

3. **Status Checking**:
   ```c
   if (sdcard_is_mounted()) {
       // Use SD card features
   }
   ```

### Example: Adding to About Screen

```c
// components/ui/screen_manager.c - create_about_screen()

if (sdcard_is_mounted()) {
    sdcard_info_t info;
    sdcard_get_info(&info);

    lv_obj_t* sd_label = lv_label_create(scr);
    lv_label_set_text_fmt(sd_label,
        "SD Card: %s (%.2f GB)",
        info.card_type,
        info.total_bytes / (1024.0 * 1024.0 * 1024.0));
    lv_obj_align(sd_label, LV_ALIGN_CENTER, 0, 100);
} else {
    lv_obj_t* sd_label = lv_label_create(scr);
    lv_label_set_text(sd_label, "SD Card: Not Present");
    lv_obj_align(sd_label, LV_ALIGN_CENTER, 0, 100);
}
```

## Future Work

See full proposal details below for:
- Background image loading from SD
- Log export functionality
- Settings backup/restore
- File browser UI
- Hot-plug detection
- Firmware staging

## References

- BSP Component: `managed_components/espressif__esp32_p4_function_ev_board/`
- BSP API: `bsp_sdcard_mount()`, `bsp_sdcard_unmount()`, `bsp_sdcard_get_handle()`
- Related Proposals: `background-image-selector.md`, `debug-log-viewer.md`

---

# Original Proposal (Pre-Implementation)

[Rest of original proposal content preserved below for reference...]

## Problem Statement

The Greenwood Clock currently relies solely on SPIFFS (3 MB) for file storage, which severely limits:
- Number of background images (5-15 images max)
- Log storage capacity (no persistent logs)
- Firmware backup capabilities
- User data storage (settings exports, screenshots)
- Asset storage (animations, fonts, themes)

The ESP32-P4 Function EV Board includes an SD card slot that is currently unused. This provides:
- **Storage Capacity**: 4 GB - 128 GB (vs 3 MB SPIFFS)
- **Removable Media**: Easy file transfer via PC
- **Non-volatile**: Survives firmware updates
- **User-friendly**: Standard SD card format

Users need expandable storage for personalization and data management.

[... rest of original proposal content ...]
