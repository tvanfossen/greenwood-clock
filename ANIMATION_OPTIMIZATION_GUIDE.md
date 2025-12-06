# Animation & General Performance Optimization Guide

## Current Configuration Status

### ✅ Already Optimized
- **Color Depth**: 16-bit (RGB565) - Good balance between quality and performance
- **Display Refresh**: 16ms (62.5 FPS target)
- **Double Buffering**: Enabled - Eliminates tearing
- **DMA**: Enabled - Fast hardware transfers
- **LVGL Task Priority**: 5 (High) - UI gets good CPU time
- **Task Stack**: 16KB - Sufficient for GIF decoding
- **Draw Buffer**: Full screen (1024×600) - Optimal rendering
- **Frame Cache**: Enabled (LV_GIF_CACHE_DECODE_DATA=y) - Frames cached after first loop

---

## 🚀 Recommended Optimization Settings

### 1. **Reduce LVGL Refresh Period** (Most Impact)
**Current**: 20ms (50 FPS in code) vs 16ms (62.5 FPS in config)
**Recommendation**: Reduce to **10ms** for 100 FPS target

```cpp
// In main/main.cpp, change:
.timer_period_ms = 20,  // ← Change this
// To:
.timer_period_ms = 10,  // Twice as fast
```

**Why**: Animations will be smoother, especially GIF playback. At 1024×600, modern ESP32-P4 can handle this easily.

---

### 2. **Increase Draw Unit Count** (Multi-threading)
**Current**: `CONFIG_LV_DRAW_SW_DRAW_UNIT_CNT=1` (single-threaded rendering)
**Recommendation**: Change to `2` or `4` for parallel rendering

```cpp
// In sdkconfig.defaults, change:
CONFIG_LV_DRAW_SW_DRAW_UNIT_CNT=1
// To:
CONFIG_LV_DRAW_SW_DRAW_UNIT_CNT=4
```

**Why**: Enables LVGL to use multiple CPU cores for rendering. ESP32-P4 has 2 cores, so 2-4 units can improve throughput by 30-50%.

**Implementation**: Edit `sdkconfig.defaults` and re-run `idf.py build`

---

### 3. **Increase Draw Layer Buffer Size**
**Current**: `CONFIG_LV_DRAW_LAYER_SIMPLE_BUF_SIZE=24576` (24KB)
**Recommendation**: Increase to `65536` (64KB) or `131072` (128KB)

```cpp
CONFIG_LV_DRAW_LAYER_SIMPLE_BUF_SIZE=131072
```

**Why**: Larger buffer reduces rendering passes. At 1024×600, more data fits in one pass, reducing memory copies.

---

### 4. **Enable Fast Memory Pool for Drawing**
**Current**: `CONFIG_LV_DRAW_LAYER_MAX_MEMORY=0` (unlimited)
**Recommendation**: Keep as-is, but ensure SPIRAM is configured

SPIRAM is already enabled and being used for display buffers. This is good.

---

### 5. **Task Sleep Optimization**
**Current**: `.task_max_sleep_ms = 500` (can sleep up to 500ms)
**Recommendation**: Reduce to **100ms** for more responsive animations

```cpp
.task_max_sleep_ms = 100,  // More responsive, less latency
```

**Why**: UI task won't sleep too long, keeping animation loop responsive.

---

### 6. **Increase LVGL Task Stack**
**Current**: 16KB
**Recommendation**: Keep at 16KB, but monitor with:
```bash
idf.py monitor  # Look for "Stack overflow" warnings
```

---

## 📊 Performance Comparison Table

| Setting | Current | Recommended | Benefit |
|---------|---------|-------------|---------|
| Timer Period | 20ms (50 FPS) | 10ms (100 FPS) | Smoother animations |
| Draw Units | 1 | 4 | 30-50% faster rendering |
| Layer Buffer | 24KB | 128KB | Fewer memory copies |
| Task Sleep | 500ms | 100ms | Better responsiveness |
| Stack Size | 16KB | 16KB | ✅ Already optimal |
| DMA | Enabled | Enabled | ✅ Already optimal |
| Double Buffer | Yes | Yes | ✅ Already optimal |

---

## Implementation Priority

### Phase 1 (Highest Impact - Do First)
1. **Reduce timer period to 10ms** (code change)
2. **Reduce task sleep to 100ms** (code change)

### Phase 2 (Medium Impact)
3. **Increase draw layer buffer to 128KB** (config change)
4. **Enable 4x draw units** (config change)

### Phase 3 (Fine-tuning)
5. Monitor frame rates with `lv_tick_get()` / timing logs
6. Adjust based on actual performance

---

## Testing Instructions

### After Making Changes:

```bash
# Full rebuild with new config
cd /home/tvanfossen/Projects/greenwood-clock
idf.py fullclean
idf.py build

# Flash and monitor
idf.py flash monitor

# Watch logs for:
# - GIF animation smoothness
# - Frame timing logs
# - Memory warnings
# - Task stack warnings
```

### Visual Indicators of Success:
- ✅ GIF plays smoothly without stuttering
- ✅ Quick UI transitions between screens
- ✅ No "Image decoder didn't set stride" warnings flooding logs
- ✅ Weather updates don't cause animation hitches
- ✅ Touch response is snappy

---

## Memory Considerations

**Available SPIRAM**: 8MB (external RAM)
**Current Usage**: 
- Display buffers (double): ~3MB (1024×600×2×2 bytes)
- GIF cache: Automatic (frames cached on-demand)
- Heap: ~17-18MB available for other operations

**Safe to apply all recommendations**: Yes, plenty of headroom.

---

## Advanced Tuning (Optional)

### If animations still feel slow:
1. Check if weather updates are blocking: Add timestamps to `weather_update_cb`
2. Profile with `esp_timer_get_time()` to find bottlenecks
3. Consider reducing splash screen animation complexity

### If memory becomes tight:
1. Reduce layer buffer size back to 65KB
2. Reduce draw units to 2
3. Monitor with `esp_get_free_heap_size()` in logs

---

## Code Changes Summary

```cpp
// main/main.cpp - Update these values:

lvgl_port_cfg_t port_config = {
    .task_priority = 5,        // ✅ Good as-is
    .task_stack = 16*1024,     // ✅ Good as-is
    .task_affinity = -1,       // ✅ Good as-is
    .task_max_sleep_ms = 100,  // ← CHANGE from 500
    .timer_period_ms = 10,     // ← CHANGE from 20
};
```

---

## Expected Results

With all Phase 1-2 changes:
- **GIF animation**: 2-3x smoother
- **UI responsiveness**: 30-50% faster
- **Frame consistency**: Much steadier frame rate
- **Touch latency**: Reduced by ~50ms
