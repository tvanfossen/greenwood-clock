# PPA Hardware Acceleration — Investigation Status

**Last updated**: 2026-02-23
**Current status**: CONFIGURED ON / UNVERIFIED ON HARDWARE

## What is PPA?

PPA (Pixel Processing Accelerator) is ESP32-P4's dedicated hardware unit for:
- Rectangle fills — up to **9x faster** than software
- Image blending — ~**30% faster**
- Display rotation/mirroring — ~**40% faster**

Without PPA, all rendering is software on the CPU, causing frame drops, sluggish touch
response, and GIF playback stutter.

## Required Configuration

All of the following must be true simultaneously:

```
CONFIG_LV_USE_PPA=y                  # LVGL PPA backend
CONFIG_LV_USE_PPA_IMG=y              # PPA for image operations
CONFIG_LVGL_PORT_ENABLE_PPA=y        # BSP display driver uses PPA (critical!)
CONFIG_LV_DRAW_BUF_ALIGN=64          # Buffer address alignment
CONFIG_LV_DRAW_BUF_STRIDE_ALIGN=64   # Row stride alignment
CONFIG_LV_ATTRIBUTE_MEM_ALIGN_SIZE=64
CONFIG_CACHE_L1_CACHE_LINE_SIZE=64   # Hardcoded by ESP-IDF
CONFIG_CACHE_L2_CACHE_LINE_SIZE=64   # Must match L1
CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY=y
```

All of the above are set in `sdkconfig.defaults`.

## Current State

sdkconfig.defaults: **PPA enabled** ✅
Last verified on hardware: **Unknown — believed non-functional**

Boot log should show (when working):
```
I (xxx) main: [2] ✓ PPA hardware acceleration: ENABLED
I (xxx) main: [2] ✓ PPA image operations: ENABLED
I (xxx) main: [2] ✓ PPA in BSP display driver: ENABLED
```

Failure signature:
```
E (xxxxx) ppa_fill: out.buffer addr or out.buffer_size not aligned to cache line size
[Error] lv_draw_ppa_fill: PPA fill failed: 258
[Warn] lv_draw_buf_init: Data is not aligned, ignored
```

## Root Cause Analysis

### What works
- Buffer **address** alignment: handled by `lvgl_mem_esp` custom allocator (64-byte)
- Buffer **size** alignment: handled by `lvgl_mem_esp` (rounds up to 64-byte multiple)
- Buffer **stride** alignment: `CONFIG_LV_DRAW_BUF_STRIDE_ALIGN=64`
- BSP driver PPA flag: `CONFIG_LVGL_PORT_ENABLE_PPA=y`

### What fails
PPA requires the **pixel data pointer** (not just the buffer start) to be 64-byte aligned.
When LVGL decodes an image from a file, the pixel data starts after a variable-length header:

```
Allocated buffer (64-byte aligned start)
┌────────────────────────────────────┐
│ PNG/GIF header (N bytes)           │  ← header offset varies
├────────────────────────────────────┤
│ Pixel data starts here             │  ← NOT 64-byte aligned (offset by N)
└────────────────────────────────────┘
PPA checks the pixel data pointer → rejects → falls back to software render
```

This is a fundamental mismatch: LVGL's image decoders are not designed to guarantee
data-pointer alignment, only buffer-pointer alignment.

Additional failure cases beyond image decode:
- **LVGL layer buffers** — dynamically allocated during rendering, not guaranteed aligned
- **Font glyph bitmaps** — PPA may be invoked for text; glyph data is not 64-byte aligned

## Fix Approaches

### Option A: Image data realignment (preferred for PPA)

After decode, copy pixel data into a fresh aligned buffer before handing to PPA.

**Location**: `components/lvgl/src/draw/lv_image_decoder.c`

```c
static void ensure_ppa_alignment(lv_draw_buf_t *draw_buf) {
    const size_t alignment = 64;
    if (((uintptr_t)draw_buf->data) % alignment != 0) {
        size_t aligned_size = (draw_buf->data_size + alignment - 1) & ~(alignment - 1);
        void *aligned = lv_malloc(aligned_size);
        if (!aligned) return;
        lv_memcpy(aligned, draw_buf->data, draw_buf->data_size);
        lv_free(draw_buf->unaligned_data);
        draw_buf->data = aligned;
        draw_buf->unaligned_data = aligned;
        draw_buf->data_size = aligned_size;
    }
}
```

**Trade-off**: Memory doubles briefly during image load. Acceptable given 8 MB SPIRAM.

### Option B: DMA2D (lower bar, acceptable fallback)

ESP32-P4 DMA2D has relaxed alignment requirements. Less alignment-sensitive than PPA.

```
CONFIG_LV_USE_PPA=n
CONFIG_LV_USE_DRAW_DMA2D=y        (LVGL >= 9.x, ESP-IDF >= 5.x)
CONFIG_BSP_DISPLAY_LVGL_AVOID_TEAR=y
```

**Note**: Prior attempts to enable DMA2D failed due to STM32-specific headers being
included. Verify this is resolved in current ESP-IDF version before attempting.

Expected improvement over pure software: 30-50%.

### Option C: Software rendering with reduced GIF load

If hardware acceleration proves too difficult:
- Reduce GIF to 800×450 or lower
- Reduce GIF frame rate (20 FPS instead of 60)
- Use static PNG backgrounds instead of GIFs

## Verification Checklist

When testing PPA:

```bash
# After build, verify config is present:
grep "CONFIG_LV_USE_PPA" build/config/sdkconfig.h          # must be 1
grep "CONFIG_LVGL_PORT_ENABLE_PPA" build/config/sdkconfig.h # must be 1
grep "LV_USE_PPA" components/lvgl/lv_conf.h                 # must be 1

# At runtime, watch for:
grep "ppa_fill" /sdcard/logs/debug.log     # any errors = PPA failing
grep "\[2\] ✓ PPA" /sdcard/logs/debug.log  # all three must appear
```

Runtime success criteria:
- [ ] No `ppa_fill` errors in logs
- [ ] All three `[2] ✓ PPA` boot messages present
- [ ] GIF playback smooth at ~60 FPS
- [ ] Touch input latency <100ms
- [ ] Screen transitions <300ms

## History

| Date | Event |
|---|---|
| 2025-12-05 | PPA enabled in config; IRAM placement disabled to fix linker errors |
| 2025-12-05 | Custom allocator (`lvgl_mem_esp`) added for address+size alignment |
| 2025-12-06 | `CONFIG_LVGL_PORT_ENABLE_PPA=y` added (was the missing BSP flag) |
| 2026-02-23 | Status unknown — not verified on hardware since config changes |
