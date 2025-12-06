# Greenwood Clock Filesystem Configuration

**Last Updated**: 2025-12-05
**Purpose**: Document LVGL filesystem drive mappings and correct path usage

---

## Drive Letter Mappings

The Greenwood Clock uses LVGL's filesystem abstraction with two drives:

| Drive | Mount Point | Purpose | Example Files |
|-------|-------------|---------|---------------|
| **A:** | `/sdcard` | SD card user content | `A:/christmas_village.gif` |
| **B:** | `/spiffs` | Built-in SPIFFS assets | `B:/splash.png` |

## How LVGL Path Resolution Works

When you use a drive letter in LVGL (e.g., `A:/file.gif`):

1. **User/code provides**: `A:/christmas_village.gif`
2. **LVGL strips drive letter**: `/christmas_village.gif`
3. **Driver prepends base path**: `/sdcard/christmas_village.gif`
4. **POSIX open()** opens the final path

### ✓ Correct Path Format

```c
// SD card files (A: drive)
"A:/animations/hummingbird.gif"        → /sdcard/animations/hummingbird.gif
"A:/christmas_village.gif"             → /sdcard/christmas_village.gif
"A:/backgrounds/winter.png"            → /sdcard/backgrounds/winter.png

// SPIFFS files (B: drive)
"B:/splash.png"                        → /spiffs/splash.png
"B:/fonts/nunito.bin"                  → /spiffs/fonts/nunito.bin
```

### ✗ Wrong Path Format

```c
// DO NOT include the mount point in the path!
"A:/sdcard/file.gif"                   → /sdcard/sdcard/file.gif ✗ (doubled!)
"B:/spiffs/splash.png"                 → /spiffs/spiffs/splash.png ✗ (doubled!)
```

## Implementation Details

### A: Drive (SD Card)

Registered via `lv_fs_posix_init()` in `main.cpp`:

```cpp
// Configured in sdkconfig.defaults
CONFIG_LV_FS_POSIX_PATH="/sdcard"
CONFIG_LV_FS_POSIX_LETTER='A'

// Initialized in main.cpp
lv_fs_posix_init();  // Registers A: → /sdcard
```

**Source**: `components/lvgl/src/libs/fsdrv/lv_fs_posix.c`

### B: Drive (SPIFFS)

Custom driver registered in `main.cpp`:

```cpp
// Custom SPIFFS filesystem driver
static lv_fs_drv_t spiffs_fs_drv;
static const char* spiffs_base_path = "/spiffs";

static void lv_fs_spiffs_init(void) {
    lv_fs_drv_init(&spiffs_fs_drv);
    spiffs_fs_drv.letter = 'B';
    spiffs_fs_drv.open_cb = spiffs_fs_open;  // Prepends /spiffs to paths
    // ... other callbacks
    lv_fs_drv_register(&spiffs_fs_drv);
}
```

**Source**: `main/main.cpp:118-137`

## Usage in Code

### Boot Splash Screen

```c
// components/ui/ui.c
lv_obj_t* img = lv_img_create(scr);
lv_img_set_src(img, "B:/splash.png");  // Loads /spiffs/splash.png
```

### User Background Image

```c
// From NVS settings (clock_settings_t.background_image)
"A:/christmas_village.gif"  // Loads /sdcard/christmas_village.gif

// Default setting in settings.c
.background_image = "A:/splash.png"  // /sdcard/splash.png
```

### GIF Animation

```c
// components/ui/ui.c
lv_obj_t* gif = lv_gif_create(parent);
lv_gif_set_src(gif, "A:/animations/hummingbird.gif");  // /sdcard/animations/hummingbird.gif
```

## Directory Structure

### SD Card (`/sdcard`, accessed via `A:`)

```
/sdcard/
├── animations/
│   ├── hummingbird.gif
│   └── christmas_village.gif
├── backgrounds/
│   ├── winter.png
│   └── summer.jpg
└── splash.png (optional fallback)
```

### SPIFFS (`/spiffs`, accessed via `B:`)

```
/spiffs/
├── splash.png (boot splash, always present)
├── fonts/
│   └── nunito.bin
└── (other built-in assets)
```

## Common Errors and Solutions

### Error: "Could not open file: /sdcard/sdcard/file.gif"

**Cause**: Path includes mount point prefix
**Wrong**: `"A:/sdcard/file.gif"`
**Correct**: `"A:/file.gif"`

### Error: "unknown driver letter"

**Cause**: Used direct POSIX path instead of drive letter
**Wrong**: `"/spiffs/splash.png"` or `"/sdcard/file.gif"`
**Correct**: `"B:/splash.png"` or `"A:/file.gif"`

### Error: File not found (errno: 2)

**Possible causes**:
1. File doesn't exist on SD card/SPIFFS
2. Path includes mount point (doubled path)
3. Wrong drive letter used

**Debugging**:
```c
// Check actual file path being opened
// Look for "Could not open file: /actual/path" in logs
ESP_LOGI(TAG, "Attempting to load: %s", path);
```

## Configuration Files

### Modified Files

1. **`main/main.cpp`**
   - Line 58: `#define DEFAULT_MOUNT_POINT "/spiffs"`
   - Line 118-137: Custom B: drive implementation
   - Line 316: `lv_fs_spiffs_init()` call

2. **`components/ui/ui.c`**
   - Line 397: `lv_img_set_src(img, "B:/splash.png")`

3. **`components/settings/settings.c`**
   - Line 26: `background_image = "A:/splash.png"` (default)

4. **`sdkconfig.defaults`**
   - `CONFIG_LV_FS_POSIX_PATH="/sdcard"` (A: drive base path)

### NVS Settings Migration

If existing NVS settings have the old format (`A:/sdcard/...`), the user should:

1. **Option 1**: Reset settings to defaults
   ```c
   settings_reset();
   esp_restart();
   ```

2. **Option 2**: Update via settings UI to new format

3. **Option 3**: Flash with `idf.py erase-flash` (nuclear option)

---

## Image Decoder Configuration

### PNG Support (Required!)

**CRITICAL**: LVGL must have PNG decoder enabled to load `.png` files:

```
CONFIG_LV_USE_LODEPNG=y
```

Without this, PNG files will open successfully but fail with:
```
[Warn] lv_image_set_src: failed to get image info: B:/splash.png
```

**Why LODEPNG?**
- Pure C implementation (no external dependencies)
- Lightweight (~50KB)
- Good performance on ESP32-P4
- Supports all PNG color types

**Alternative**: `CONFIG_LV_USE_LIBPNG=y` (requires libpng library, more features but larger)

### Other Decoders

```
CONFIG_LV_USE_GIF=y              # GIF animations (already enabled)
CONFIG_LV_USE_BMP=y              # BMP images (optional)
CONFIG_LV_USE_TJPGD=y            # JPEG decoder (optional)
```

## Summary

- **A: drive** = SD card (`/sdcard`)
- **B: drive** = SPIFFS (`/spiffs`)
- **PNG decoder** = Must be enabled (`CONFIG_LV_USE_LODEPNG=y`)
- **Never** include mount point in LVGL paths
- **Always** use drive letters for LVGL filesystem operations
- Direct POSIX paths work for `open()`, `fopen()`, etc., but **not** for LVGL APIs

**Quick Reference**:
```c
✓ "A:/file.gif"        → /sdcard/file.gif
✓ "B:/splash.png"      → /spiffs/splash.png
✗ "A:/sdcard/file.gif" → /sdcard/sdcard/file.gif (wrong!)
✗ "/spiffs/file.png"   → unknown driver letter (wrong!)
```

**Decoder Check**:
- PNG files? → Need `CONFIG_LV_USE_LODEPNG=y`
- GIF files? → Need `CONFIG_LV_USE_GIF=y` ✓ (already enabled)
- JPEG files? → Need `CONFIG_LV_USE_TJPGD=y` or `CONFIG_LV_USE_LIBJPEG_TURBO=y`
