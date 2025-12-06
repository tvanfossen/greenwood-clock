# Fix PPA Hardware Acceleration for ESP32-P4

**Status**: BACKLOG
**Priority**: HIGH (critical for performance)
**Complexity**: HIGH
**Estimated Effort**: 3-5 days
**Dependencies**: LVGL v9.3, ESP-IDF v5.5

---

## Problem Statement

PPA (Pixel Processing Accelerator) hardware acceleration on ESP32-P4 is currently non-functional, causing severe performance degradation across the entire UI:

- **Touch input**: Laggy, requires slow deliberate movements
- **Screen navigation**: Extremely slow transitions
- **Settings screens**: Unresponsive, barely usable
- **GIF playback**: Stuttering, frame drops
- **Overall UX**: Unusable

### Root Cause

PPA hardware requires **strict 64-byte alignment** for:
1. Buffer **address**
2. Buffer **size**
3. Buffer **stride** (row width)
4. **Data pointer** (actual pixel data, not just buffer start)

While we've achieved 1-3, **image data from files is inherently unaligned** due to file format headers:

```
┌─────────────────────────────────┐
│ PNG Header (33 bytes)           │  ← Allocated buffer (64-byte aligned)
├─────────────────────────────────┤
│ IHDR chunk (25 bytes)           │
├─────────────────────────────────┤
│ Image data starts here          │  ← Offset by 58 bytes = NOT 64-byte aligned!
│ (actual pixels PPA needs)       │
└─────────────────────────────────┘
```

LVGL loads images directly from files/SD card without realigning the data. When PPA tries to use this data:

```
E (32996) ppa_fill: out.buffer addr or out.buffer_size not aligned to cache line size
[Error] lv_draw_ppa_fill: PPA fill failed: 258
```

PPA **rejects** the operation → falls back to **software rendering** → entire system becomes laggy.

---

## Current State

### What Works ✓
- Custom memory allocator with 64-byte alignment (address + size)
- `CONFIG_LV_DRAW_BUF_STRIDE_ALIGN=64` (stride alignment)
- `CONFIG_LV_DRAW_BUF_ALIGN=64` (buffer alignment)
- Main display buffers properly aligned

### What Fails ✗
- Image decoder output (PNG, GIF, JPEG)
- Dynamically loaded images from SD card
- LVGL layer buffers created during rendering
- Font glyph bitmaps

### Evidence from Logs
```
[Warn] lv_draw_buf_init: Data is not aligned, ignored lv_draw_buf.c:298
E (32996) ppa_fill: ppa_do_fill(104): out.pic_w/h mismatch with out.buffer_size
E (138792) ppa_fill: ppa_do_fill(97): out.buffer addr or out.buffer_size not aligned
```

---

## Proposed Solution

### Phase 1: Image Data Realignment (Core Fix)

Modify LVGL's image decoder to **copy and realign** image data after decoding:

**File**: `components/lvgl/src/draw/lv_image_decoder.c`

```c
// After image decode, before returning buffer to PPA
static void ensure_ppa_alignment(lv_draw_buf_t * draw_buf) {
    const size_t alignment = 64;

    // Check if data is aligned
    if (((uintptr_t)draw_buf->data) % alignment != 0) {
        // Calculate aligned size
        size_t aligned_size = (draw_buf->data_size + alignment - 1) & ~(alignment - 1);

        // Allocate aligned buffer
        void *aligned_data = lv_malloc(aligned_size);
        if (!aligned_data) return; // Fall back to unaligned

        // Copy data to aligned buffer
        lv_memcpy(aligned_data, draw_buf->data, draw_buf->data_size);

        // Free old buffer
        lv_free(draw_buf->unaligned_data);

        // Update buffer
        draw_buf->data = aligned_data;
        draw_buf->unaligned_data = aligned_data;
        draw_buf->data_size = aligned_size;
    }
}
```

**Pros**:
- Guaranteed PPA compatibility
- Works with all image formats
- No changes to file loading logic

**Cons**:
- Memory overhead (2x during copy)
- CPU overhead for memcpy
- Modifies LVGL core (harder to maintain)

---

### Phase 2: Layer Buffer Alignment

Ensure dynamically allocated layer buffers are aligned:

**File**: `components/lvgl/src/draw/lv_draw.c`

```c
// In lv_draw_layer_alloc_buf
static void * layer_buf_alloc(size_t size) {
    const size_t alignment = 64;

    // Round size up to 64-byte multiple
    size_t aligned_size = (size + alignment - 1) & ~(alignment - 1);

    // Allocate aligned (uses our custom allocator)
    return lv_malloc(aligned_size);
}
```

**Verification**: Add logging to confirm alignment:
```c
ESP_LOGI(TAG, "Layer buffer: addr=0x%08x, size=%u, aligned=%s",
         (unsigned)buf, size,
         ((uintptr_t)buf % 64 == 0 && size % 64 == 0) ? "YES" : "NO");
```

---

### Phase 3: Font Glyph Alignment

Font bitmaps are also used by PPA:

**File**: `components/lvgl/src/font/lv_font.c`

Options:
1. **Pre-align fonts** during build (modify font converter)
2. **Runtime alignment** (copy glyph bitmaps to aligned buffers)
3. **Disable PPA for text** (use software rendering for fonts only)

Recommendation: Option 3 (text is small, software rendering acceptable)

---

### Phase 4: Configuration Hardening

Add compile-time checks to prevent misconfig:

**File**: `components/lvgl/lv_conf.h` or `sdkconfig.defaults`

```c
#if CONFIG_LV_USE_PPA
    #if CONFIG_LV_DRAW_BUF_STRIDE_ALIGN != 64
        #error "PPA requires CONFIG_LV_DRAW_BUF_STRIDE_ALIGN=64"
    #endif
    #if CONFIG_LV_DRAW_BUF_ALIGN != 64
        #error "PPA requires CONFIG_LV_DRAW_BUF_ALIGN=64"
    #endif
    #if !defined(CONFIG_LV_USE_CUSTOM_MALLOC) || !CONFIG_LV_USE_CUSTOM_MALLOC
        #error "PPA requires custom allocator with 64-byte alignment"
    #endif
#endif
```

---

## Alternative Approaches

### Option A: DMA2D Instead of PPA

ESP32-P4 also has DMA2D (2D Direct Memory Access):

**Pros**:
- Doesn't require strict alignment (works with any buffer)
- Still hardware accelerated
- Good for image rotation/blending

**Cons**:
- Not as fast as PPA for fills
- Different API, requires integration

**Configuration**:
```
CONFIG_LV_USE_PPA=n
CONFIG_LV_USE_DRAW_DMA2D=y
CONFIG_BSP_DISPLAY_LVGL_AVOID_TEAR=y
```

**Expected improvement**: 30-50% faster than pure software rendering

### Option B: Software Rendering Optimizations

If hardware acceleration proves too difficult:

1. **Reduce GIF complexity**:
   - Lower resolution (e.g., 800x600 instead of 1024x600)
   - Reduce color depth (256 colors instead of true color)
   - Lower frame rate (20 FPS instead of 60 FPS)

2. **Optimize LVGL settings**:
   ```
   CONFIG_LV_DRAW_SW_COMPLEX=n  # Disable complex gradients
   CONFIG_LV_DRAW_SW_SHADOW_CACHE_SIZE=0  # Disable shadow cache
   ```

3. **Use static backgrounds** instead of animated GIFs

4. **Increase CPU frequency** (already at 360MHz max)

**Expected improvement**: 10-20% faster, but still noticeably slower than hardware

### Option C: Upstream LVGL Patch

Submit patch to LVGL to add PPA alignment support natively:

**Pros**:
- Proper long-term solution
- Benefits entire ESP32-P4 community
- No local maintenance burden

**Cons**:
- Slow (weeks/months for review/merge)
- May be rejected (niche hardware requirement)
- Requires LVGL expertise

---

## Implementation Plan

### Immediate (Disable PPA)
1. Set `CONFIG_LV_USE_PPA=n`
2. System becomes usable (software rendering)
3. Document performance baseline

### Short-Term (Try DMA2D)
1. Enable `CONFIG_LV_USE_DRAW_DMA2D=y`
2. Test performance improvement
3. If sufficient, stop here

### Medium-Term (Fix PPA - if DMA2D insufficient)
1. Implement Phase 1 (image realignment)
2. Test with PNG backgrounds
3. Implement Phase 2 (layer buffers)
4. Test with UI navigation
5. Implement Phase 3 (font fallback)
6. Full system test

### Long-Term (Upstream)
1. Clean up implementation
2. Submit PR to LVGL
3. Monitor for acceptance/feedback

---

## Testing Strategy

### Performance Metrics

Measure before/after for each phase:

| Metric | Software | DMA2D | PPA (target) |
|--------|----------|-------|--------------|
| GIF FPS | 15-20 | 30-40 | 60 |
| Touch latency | 200ms | 100ms | 50ms |
| Screen transition | 800ms | 400ms | 200ms |
| Settings navigation | Laggy | Acceptable | Smooth |

### Test Cases

1. **Background Loading**:
   - Load PNG from SD card
   - Verify no alignment warnings
   - Verify PPA used (check logs for no errors)

2. **GIF Playback**:
   - Full-screen GIF at 60 FPS
   - Monitor CPU usage (<50%)
   - Check for frame drops

3. **UI Navigation**:
   - Rapidly switch between settings screens
   - Touch multiple buttons in succession
   - Verify smooth transitions (no lag)

4. **Text Rendering**:
   - Clock display update
   - Settings labels
   - Verify no PPA errors for fonts

---

## Risks and Mitigations

### Risk 1: Memory Overhead
**Impact**: Realignment doubles memory during decode
**Mitigation**: Only realign if needed (check alignment first)
**Fallback**: DMA2D (no realignment needed)

### Risk 2: LVGL Maintenance Burden
**Impact**: Local patches break on LVGL updates
**Mitigation**: Isolate changes to separate files where possible
**Fallback**: Fork LVGL, maintain separately

### Risk 3: Performance Still Insufficient
**Impact**: Even with PPA, GIF is too heavy
**Mitigation**: Use lower-resolution/simpler GIFs
**Fallback**: Static backgrounds only

---

## Success Criteria

- [ ] No PPA alignment errors in logs
- [ ] Touch input responsive (<100ms latency)
- [ ] Screen transitions smooth (<300ms)
- [ ] Full-screen GIF at 30+ FPS
- [ ] Settings navigation fluid
- [ ] CPU usage <60% during typical operation
- [ ] Heap usage stable (no leaks from realignment)

---

## Resources

- **LVGL PPA Documentation**: https://docs.lvgl.io/master/integration/chip_vendors/espressif/hardware_accelerator_ppa.html
- **ESP32-P4 PPA Driver**: `components/esp_driver_ppa` in ESP-IDF
- **LVGL Draw System**: `components/lvgl/src/draw/`
- **Similar Issues**: Search LVGL GitHub for "PPA alignment"

---

## Notes

- PPA requires **all four**: address, size, stride, AND data pointer aligned
- Image file formats (PNG, GIF, JPEG) have variable-length headers
- LVGL's design prioritizes memory efficiency over alignment
- This is a **fundamental architectural mismatch** between LVGL and PPA
- May not be solvable without significant LVGL modifications
- DMA2D is a more practical intermediate solution

---

## Decision Log

| Date | Decision | Rationale |
|------|----------|-----------|
| 2025-12-05 | Disable PPA temporarily | System unusable, need immediate fix |
| TBD | Try DMA2D | Less strict requirements, good compromise |
| TBD | Implement PPA fix (if needed) | Only if DMA2D insufficient |

---

**Next Steps**: Disable PPA, enable DMA2D, measure performance improvement.
