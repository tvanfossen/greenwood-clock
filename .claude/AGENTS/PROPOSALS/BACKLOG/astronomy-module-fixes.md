---
proposal_id: "astronomy-module-fixes"
title: "Greenwood Clock - Astronomy Module Stack Fault Fixes"
github_issue: null
created: "2025-12-01"
updated: "2025-12-01"

status: "BACKLOG"
priority: "medium"
complexity: "high"

category: ["bugfix", "feature", "optimization"]
tags: ["esp32", "astronomy", "memory", "stack-overflow"]

estimated_hours: 10
actual_hours: 0
progress_percent: 0

depends_on: ["greenwood-clock-maintenance-handoff"]
blocks: []

commits: []
branches: []
pull_requests: []

agent_notes: []

stall_reason: null
unblock_requirements: []

completion_date: null
verification_status: "pending"
---

# Greenwood Clock - Astronomy Module Stack Fault Fixes

## Problem Statement

The astronomy module (`components/astronomy/`) causes stack protection faults when invoked. The module is currently disabled in the UI code to prevent crashes. Moon phase display is a desired feature but cannot be enabled until memory issues are resolved.

### Symptoms
- Stack overflow protection fault when calling `astronomy_fetch_moon_phase()`
- Heap allocation failures (if using heap)
- Device resets/crashes
- Module is commented out in `components/ui/ui.c`

## Proposed Solution

Profile and optimize the astronomy module's memory usage through:
1. Stack usage analysis and profiling
2. Move large buffers from stack to heap or static allocation
3. Optimize HTTP response buffering
4. Reduce Base64 encoding overhead
5. Test and validate fixes
6. Re-enable moon phase display in UI

## Current State

### Astronomy Module Status
- **Status**: Disabled (commented out in ui.c line 11, 56)
- **API**: AstronomyAPI.com integration
- **Functionality**: Moon phase calculation and icon fetching
- **Problem**: Excessive stack usage causes crashes

### Code Analysis Needed
- `components/astronomy/astronomy.c` - identify large stack allocations
- HTTP buffer sizing
- Base64 encoding implementation (lines 13-40 in astronomy.c)
- JSON parsing buffers

## Implementation Plan

### Phase 1: Memory Profiling & Root Cause Analysis
- [ ] Enable stack high watermark monitoring for astronomy tasks
- [ ] Profile heap usage during moon phase fetch
- [ ] Identify large stack-allocated buffers (char arrays, structs)
- [ ] Measure actual vs required buffer sizes
- [ ] Document memory usage baseline

### Phase 2: Stack Optimization
- [ ] Move large buffers (>512 bytes) from stack to heap
- [ ] Optimize Base64 encoding to use smaller buffers or streaming
- [ ] Reduce HTTP response buffer size (use chunked reading)
- [ ] Replace large local char arrays with malloc/free
- [ ] Consider static allocation for buffers reused across calls

### Phase 3: HTTP Client Optimization
- [ ] Switch to event-driven HTTP client (smaller buffers)
- [ ] Implement chunked JSON parsing (avoid full response buffer)
- [ ] Stream moon icon download to SPIFFS or malloc
- [ ] Add response size limits and validation

### Phase 4: Testing & Validation
- [ ] Run astronomy fetch in loop (100+ iterations)
- [ ] Monitor stack high watermark (should be <80% of allocated)
- [ ] Verify no heap fragmentation
- [ ] Test with low memory conditions
- [ ] Ensure no memory leaks

### Phase 5: UI Re-integration
- [ ] Uncomment astronomy includes in ui.c
- [ ] Add moon phase widget to UI grid
- [ ] Schedule periodic updates (24 hours)
- [ ] Add error handling for fetch failures
- [ ] Test UI with moon phase display enabled

## Acceptance Criteria

- [ ] Astronomy module runs without stack overflows
- [ ] Stack usage <4KB per call (verify with uxTaskGetStackHighWaterMark)
- [ ] Heap usage <16KB per call
- [ ] No memory leaks over 100 iterations
- [ ] Moon phase display functional in UI
- [ ] Device stable for 7 days with astronomy enabled
- [ ] Error handling graceful (no crashes on API failures)

## Technical Details

### Example Stack Optimization

**Before** (problematic):
```c
int astronomy_fetch_moon_phase(...) {
    char url_buf[512];
    char response_buf[4096];  // LARGE STACK ALLOCATION
    char auth_header[256];
    char encoded[192];
    // ... rest of function
}
```

**After** (optimized):
```c
int astronomy_fetch_moon_phase(...) {
    char url_buf[256];  // Reduced
    char* response_buf = malloc(4096);  // Heap allocated
    if (!response_buf) return -1;

    char auth_header[256];
    // Base64 encoding done in separate function with controlled stack

    // ... rest of function

    free(response_buf);
    return 0;
}
```

### HTTP Chunked Reading
```c
esp_err_t astronomy_fetch_with_streaming(char** out_buf, size_t* out_len) {
    esp_http_client_config_t config = {
        .url = url,
        .event_handler = http_event_handler,  // Stream data
    };

    // Event handler accumulates data into dynamically growing buffer
    // No large stack allocation needed
}
```

### Stack Monitoring
```c
void astronomy_test_stack_usage(void) {
    UBaseType_t before = uxTaskGetStackHighWaterMark(NULL);
    astronomy_fetch_moon_phase(...);
    UBaseType_t after = uxTaskGetStackHighWaterMark(NULL);
    size_t used = (before - after) * sizeof(StackType_t);
    ESP_LOGI(TAG, "Astronomy function used %u bytes of stack", used);
}
```

## Risks & Mitigations

| Risk | Impact | Likelihood | Mitigation |
|------|--------|------------|------------|
| Heap fragmentation | High | Medium | Use fixed-size pools, monitor fragmentation |
| Increased complexity | Medium | Low | Keep changes minimal, add comments |
| API response size exceeds buffer | High | Low | Add size limits, validate Content-Length |
| Memory leak introduction | High | Medium | Comprehensive testing, static analysis |

## Success Metrics

- Stack usage: <4KB per call
- Heap usage: <16KB per call
- Memory leak rate: 0 bytes over 100 calls
- Crash rate: 0 over 7 days
- UI responsiveness: <100ms delay when updating moon phase

## Related Work

- ESP32 Memory Analysis: https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-guides/performance/ram-usage.html
- AstronomyAPI Documentation: https://docs.astronomyapi.com/

## Notes

### Investigation Checklist

When starting this work, investigate:
1. Current stack size allocated to tasks
2. Actual size of AstronomyAPI JSON responses
3. Whether moon icons can be fetched directly to LVGL image buffer
4. Alternative astronomy APIs with smaller responses
5. Possibility of calculating moon phase locally (no API)

### Alternative Approaches

If memory optimization proves insufficient:
1. **Local Calculation**: Implement moon phase algorithm locally (no API needed)
2. **Simplified Display**: Show text-only phase instead of icon
3. **Background Task**: Run astronomy fetch in dedicated task with larger stack
4. **Caching**: Fetch once per month, cache result in NVS

### Testing Commands

```bash
# Enable stack overflow checking in menuconfig
idf.py menuconfig
# Component config → FreeRTOS → Check for stack overflow: Method 2

# Build with debug symbols
idf.py build

# Monitor stack usage
idf.py monitor | grep "Stack high watermark"
```

---

**Repository**: https://github.com/tvanfossen/greenwood-clock

**Created**: 2025-12-01

**Status**: Backlog (medium priority, high complexity)
