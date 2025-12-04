# Debug Log Viewer

**Status**: Backlog
**Priority**: Low
**Estimated Effort**: Small (1-2 days)
**Created**: 2025-12-02

## Problem Statement

When debugging issues or monitoring the Greenwood Clock's behavior, users currently must:
- Connect a USB cable to access serial monitor
- Use `idf.py monitor` on a development machine
- Parse verbose ESP-IDF logs
- Cannot easily check logs without physical access

This is inconvenient for:
- Troubleshooting WiFi connection issues
- Monitoring OTA update status
- Debugging weather API failures
- General device health monitoring

Users need a way to view system logs directly on the touchscreen, without requiring USB connection or external tools.

## User Feedback

From hardware testing session (2025-12-02):
> "It would be handy to add a debug window in the settings screen that will show print logs out, ONLY ENABLED WHEN ON THAT SCREEN (new proposal)"

## Proposed Solution

### 1. On-Screen Debug Log Viewer

**Settings Menu Integration:**
```
Settings
├── WiFi Settings
├── Brightness
├── Appearance
├── Software Update
├── About
└── Debug Logs ← NEW
    └── [Scrollable log view]
```

**Debug Log Screen Design:**

```
┌─────────────────────────────────────────┐
│ < Back           Debug Logs              │
├─────────────────────────────────────────┤
│ I (12345) main: Heap free: 27MB         │
│ I (12346) network: WiFi connected       │
│ I (12347) weather: Fetching...          │
│ W (12348) weather: Retry 1/3            │
│ I (12350) weather: Success              │
│ I (12351) ui: Clock updated             │
│ E (12352) ota: Connection failed        │
│ I (12353) ota: Retrying...              │
│ I (12354) main: Heap free: 27MB         │
│ ...                                      │
│ [Auto-scroll: ON] [Clear] [Log Level▼]  │
└─────────────────────────────────────────┘
```

**Features:**
- Scrollable log view (last 100-500 lines)
- Color-coded log levels (Info, Warning, Error)
- Timestamp for each log entry
- Auto-scroll toggle (follow latest logs)
- Clear button to reset log buffer
- Log level filter dropdown

### 2. Log Capture System

**Architecture:**

```
ESP_LOG (throughout codebase)
    ↓
Custom vprintf hook (esp_log_set_vprintf)
    ↓
Circular buffer (ring buffer in RAM)
    ↓
Debug screen reads from buffer (when active)
```

**Implementation:**

```c
// components/debug_log/debug_log.h

#define LOG_BUFFER_LINES 500     // Maximum log lines in memory
#define LOG_LINE_MAX_LEN 128     // Maximum characters per line

typedef enum {
    LOG_LEVEL_ERROR = 0,
    LOG_LEVEL_WARN,
    LOG_LEVEL_INFO,
    LOG_LEVEL_DEBUG,
    LOG_LEVEL_VERBOSE
} log_level_filter_t;

/**
 * @brief Initialize debug log capture system
 */
void debug_log_init(void);

/**
 * @brief Get log lines for display
 * @param buffer Buffer to store log lines
 * @param max_lines Maximum number of lines to retrieve
 * @param filter Log level filter
 * @return Number of lines retrieved
 */
int debug_log_get_lines(char** buffer, int max_lines, log_level_filter_t filter);

/**
 * @brief Clear all captured logs
 */
void debug_log_clear(void);

/**
 * @brief Enable/disable log capture
 * @param enable true to capture logs, false to disable
 */
void debug_log_set_enabled(bool enable);
```

**Circular Buffer Implementation:**

```c
// components/debug_log/debug_log.c

#include "debug_log.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>

static const char* TAG = "debug_log";

// Circular buffer for log storage
typedef struct {
    char lines[LOG_BUFFER_LINES][LOG_LINE_MAX_LEN];
    int head;      // Next write position
    int count;     // Number of valid lines
    bool enabled;  // Capture enabled flag
} log_buffer_t;

static log_buffer_t s_log_buffer = {0};
static SemaphoreHandle_t s_log_mutex = NULL;

// Original vprintf function (to chain)
static vprintf_like_t s_original_vprintf = NULL;

// Custom vprintf hook to capture logs
static int debug_log_vprintf(const char* fmt, va_list args) {
    // Always call original vprintf for serial output
    int ret = s_original_vprintf(fmt, args);

    // If capture disabled, just pass through
    if (!s_log_buffer.enabled) {
        return ret;
    }

    // Capture to buffer
    if (xSemaphoreTake(s_log_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        // Format log message
        vsnprintf(s_log_buffer.lines[s_log_buffer.head],
                  LOG_LINE_MAX_LEN, fmt, args);

        // Remove trailing newline
        size_t len = strlen(s_log_buffer.lines[s_log_buffer.head]);
        if (len > 0 && s_log_buffer.lines[s_log_buffer.head][len-1] == '\n') {
            s_log_buffer.lines[s_log_buffer.head][len-1] = '\0';
        }

        // Advance head (circular)
        s_log_buffer.head = (s_log_buffer.head + 1) % LOG_BUFFER_LINES;

        // Update count
        if (s_log_buffer.count < LOG_BUFFER_LINES) {
            s_log_buffer.count++;
        }

        xSemaphoreGive(s_log_mutex);
    }

    return ret;
}

void debug_log_init(void) {
    // Create mutex
    s_log_mutex = xSemaphoreCreateMutex();

    // Hook into ESP_LOG vprintf
    s_original_vprintf = esp_log_set_vprintf(debug_log_vprintf);

    // Start disabled to save memory
    s_log_buffer.enabled = false;

    ESP_LOGI(TAG, "Debug log capture initialized (disabled)");
}

void debug_log_set_enabled(bool enable) {
    if (xSemaphoreTake(s_log_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        s_log_buffer.enabled = enable;
        ESP_LOGI(TAG, "Log capture %s", enable ? "enabled" : "disabled");
        xSemaphoreGive(s_log_mutex);
    }
}

void debug_log_clear(void) {
    if (xSemaphoreTake(s_log_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        memset(&s_log_buffer.lines, 0, sizeof(s_log_buffer.lines));
        s_log_buffer.head = 0;
        s_log_buffer.count = 0;
        xSemaphoreGive(s_log_mutex);
    }
}

int debug_log_get_lines(char** buffer, int max_lines, log_level_filter_t filter) {
    int copied = 0;

    if (xSemaphoreTake(s_log_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        int count = (s_log_buffer.count < max_lines) ? s_log_buffer.count : max_lines;

        // Copy lines from oldest to newest
        int start = (s_log_buffer.count >= LOG_BUFFER_LINES)
                    ? s_log_buffer.head
                    : 0;

        for (int i = 0; i < count; i++) {
            int idx = (start + i) % LOG_BUFFER_LINES;

            // Apply log level filter
            char level = s_log_buffer.lines[idx][0];
            if (filter == LOG_LEVEL_ERROR && level != 'E') continue;
            if (filter == LOG_LEVEL_WARN && level != 'W' && level != 'E') continue;
            // INFO includes I, W, E
            // DEBUG includes D, I, W, E
            // VERBOSE includes all

            strcpy(buffer[copied], s_log_buffer.lines[idx]);
            copied++;
        }

        xSemaphoreGive(s_log_mutex);
    }

    return copied;
}
```

### 3. Debug Screen UI Implementation

**Screen Creation:**

```c
// components/ui/screen_manager.c

static lv_obj_t* debug_log_textarea = NULL;
static lv_timer_t* debug_log_update_timer = NULL;
static bool debug_auto_scroll = true;
static log_level_filter_t debug_log_filter = LOG_LEVEL_INFO;

// Update log display (called by timer)
static void debug_log_update_cb(lv_timer_t* timer) {
    char* lines[100];
    for (int i = 0; i < 100; i++) {
        lines[i] = malloc(LOG_LINE_MAX_LEN);
    }

    // Fetch latest logs
    int count = debug_log_get_lines(lines, 100, debug_log_filter);

    // Build display text
    char display_text[100 * LOG_LINE_MAX_LEN] = {0};
    for (int i = 0; i < count; i++) {
        strcat(display_text, lines[i]);
        strcat(display_text, "\n");
    }

    // Update textarea
    lvgl_port_lock(0);
    if (debug_log_textarea != NULL) {
        lv_textarea_set_text(debug_log_textarea, display_text);

        // Auto-scroll to bottom
        if (debug_auto_scroll) {
            lv_textarea_set_cursor_pos(debug_log_textarea, LV_TEXTAREA_CURSOR_LAST);
        }
    }
    lvgl_port_unlock();

    // Free line buffers
    for (int i = 0; i < 100; i++) {
        free(lines[i]);
    }
}

// Clear button callback
static void debug_clear_btn_cb(lv_event_t* e) {
    debug_log_clear();
    ESP_LOGI(TAG, "Debug log cleared by user");
}

// Auto-scroll toggle callback
static void debug_autoscroll_cb(lv_event_t* e) {
    lv_obj_t* sw = lv_event_get_target(e);
    debug_auto_scroll = lv_obj_has_state(sw, LV_STATE_CHECKED);
}

// Log level dropdown callback
static void debug_level_cb(lv_event_t* e) {
    lv_obj_t* dd = lv_event_get_target(e);
    debug_log_filter = (log_level_filter_t)lv_dropdown_get_selected(dd);
}

static lv_obj_t* create_debug_log_screen(void) {
    lv_obj_t* scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);

    // Title
    lv_obj_t* title = lv_label_create(scr);
    lv_label_set_text(title, "Debug Logs");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

    // Back button
    create_back_button(scr);

    // Log textarea (scrollable, read-only)
    debug_log_textarea = lv_textarea_create(scr);
    lv_obj_set_size(debug_log_textarea, 950, 450);
    lv_obj_align(debug_log_textarea, LV_ALIGN_CENTER, 0, -20);
    lv_textarea_set_text(debug_log_textarea, "Logs will appear here...");
    lv_obj_set_style_text_font(debug_log_textarea, &lv_font_montserrat_14, 0);
    lv_obj_set_style_bg_color(debug_log_textarea, lv_color_hex(0x1a1a1a), 0);
    lv_obj_set_style_text_color(debug_log_textarea, lv_color_hex(0x00ff00), 0);

    // Control bar at bottom
    lv_obj_t* control_bar = lv_obj_create(scr);
    lv_obj_set_size(control_bar, 950, 60);
    lv_obj_align(control_bar, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_set_flex_flow(control_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(control_bar, LV_FLEX_ALIGN_SPACE_EVENLY, 0, 0);

    // Auto-scroll switch
    lv_obj_t* autoscroll_sw = lv_switch_create(control_bar);
    lv_obj_add_state(autoscroll_sw, LV_STATE_CHECKED);  // Default ON
    lv_obj_add_event_cb(autoscroll_sw, debug_autoscroll_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_t* autoscroll_label = lv_label_create(control_bar);
    lv_label_set_text(autoscroll_label, "Auto-scroll");

    // Clear button
    lv_obj_t* clear_btn = lv_btn_create(control_bar);
    lv_obj_set_size(clear_btn, 100, 40);
    lv_obj_add_event_cb(clear_btn, debug_clear_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t* clear_label = lv_label_create(clear_btn);
    lv_label_set_text(clear_label, "Clear");
    lv_obj_center(clear_label);

    // Log level dropdown
    lv_obj_t* level_dd = lv_dropdown_create(control_bar);
    lv_dropdown_set_options(level_dd, "ERROR\nWARN\nINFO\nDEBUG\nVERBOSE");
    lv_dropdown_set_selected(level_dd, LOG_LEVEL_INFO);  // Default INFO
    lv_obj_add_event_cb(level_dd, debug_level_cb, LV_EVENT_VALUE_CHANGED, NULL);

    // Enable log capture when screen is created
    debug_log_set_enabled(true);

    // Start update timer (refresh every 500ms)
    debug_log_update_timer = lv_timer_create(debug_log_update_cb, 500, NULL);

    return scr;
}

// Clean up when leaving debug screen
static void cleanup_debug_log_screen(void) {
    // Disable log capture to save memory
    debug_log_set_enabled(false);

    // Delete update timer
    if (debug_log_update_timer != NULL) {
        lv_timer_del(debug_log_update_timer);
        debug_log_update_timer = NULL;
    }

    debug_log_textarea = NULL;
}
```

### 4. Memory Management

**Memory Footprint:**

```
Log buffer: 500 lines × 128 bytes = 64 KB
LVGL textarea: ~20 KB (internal buffers)
Timer overhead: ~1 KB
Total: ~85 KB additional RAM when active
```

**Optimization:**
- Only enable capture when debug screen is active
- Clear buffer when leaving screen (optional)
- Limit buffer size to 500 lines (configurable)

**Current Heap Status:**
- Free: ~27 MB
- Additional 85 KB: < 0.5% impact
- Safe for implementation

### 5. Log Level Color Coding

**Color Scheme:**

```c
static lv_color_t get_log_level_color(char level) {
    switch (level) {
        case 'E': return lv_color_hex(0xFF0000);  // Red - Error
        case 'W': return lv_color_hex(0xFFA500);  // Orange - Warning
        case 'I': return lv_color_hex(0x00FF00);  // Green - Info
        case 'D': return lv_color_hex(0x00FFFF);  // Cyan - Debug
        case 'V': return lv_color_hex(0x808080);  // Gray - Verbose
        default:  return lv_color_hex(0xFFFFFF);  // White - Unknown
    }
}
```

**Rich Text Support:**
```c
// Use LVGL's rich text (if available) or separate labels per level
lv_obj_set_style_text_color(line_label, get_log_level_color(level), 0);
```

## Implementation Phases

### Phase 1: Core Log Capture (1 day)
- Implement circular buffer
- Hook into `esp_log_set_vprintf()`
- Add enable/disable logic
- Test with existing logs

### Phase 2: Debug Screen UI (0.5 days)
- Create debug log screen in screen manager
- Add to settings menu
- Implement textarea display
- Add control buttons (Clear, Auto-scroll)

### Phase 3: Polish & Optimization (0.5 days)
- Add log level filtering
- Add color coding
- Test memory usage
- Optimize refresh rate

## Technical Considerations

### Performance Impact

**CPU Usage:**
- vprintf hook: ~50 μs per log line
- Buffer update: ~10 μs per line
- UI refresh (500ms): ~5ms (negligible)

**Total Impact:** < 0.1% CPU when active, 0% when disabled

### Memory Optimization

**Buffer Size Tuning:**
- 500 lines: Good for most debugging (64 KB)
- 250 lines: Reduced memory (32 KB)
- 100 lines: Minimal memory (13 KB)

**Recommendation:** Start with 500, make configurable later

### Thread Safety

**vprintf Hook:**
- Called from any task context
- Must use mutex for buffer access
- Timeout on mutex (10ms) to prevent blocking

**UI Update:**
- Only from LVGL timer (LVGL task)
- Lock LVGL port during updates
- No cross-task conflicts

## Testing Plan

### Unit Testing
- Circular buffer wrap-around
- Mutex contention (high log volume)
- Log level filtering accuracy
- Memory leak detection

### Integration Testing
1. **Basic Capture**:
   - Open debug screen
   - Verify logs appear
   - Close screen, verify capture stops
2. **High Volume**:
   - Generate 1000 logs rapidly
   - Verify no crashes
   - Verify buffer wraps correctly
3. **Filtering**:
   - Set filter to ERROR
   - Verify only errors shown
   - Test all filter levels
4. **Controls**:
   - Clear button works
   - Auto-scroll toggle works
   - Level dropdown updates display

### Manual Testing on Hardware
- Readability of logs on screen
- Auto-scroll smoothness
- Clear button responsiveness
- Filter dropdown usability
- Memory stability (no leaks)
- No impact on clock performance

## User Experience

### Workflow: Viewing Logs

1. User swipes up from clock screen
2. Taps "Debug Logs"
3. Debug screen appears with scrolling logs
4. User reads recent activity (WiFi, weather, etc.)
5. Optionally adjusts filter to "ERROR" only
6. Optionally clears old logs
7. Taps back to return

**Estimated Time:** 10-30 seconds (depends on debugging task)

### Use Cases

**1. WiFi Troubleshooting:**
- User can't connect to WiFi
- Opens debug logs
- Sees: "E (123) network: Connection failed: wrong password"
- User realizes password typo

**2. OTA Update Monitoring:**
- User initiates OTA update
- Opens debug logs while downloading
- Sees progress: "I (456) ota: Downloaded 1MB/3.8MB (26%)"
- Confirms update is progressing

**3. Weather API Issues:**
- Weather not updating
- Opens debug logs
- Sees: "E (789) weather: HTTP 429 Too Many Requests"
- User waits before retrying

## Success Metrics

- Logs appear on screen within 500ms of navigation
- Auto-scroll keeps latest logs visible
- Clear button empties buffer immediately
- Log level filter updates display instantly
- No memory leaks after 1000 screen opens/closes
- No performance impact on clock (1 FPS maintained)
- Buffer captures at least 500 lines reliably

## Future Enhancements

### Phase 4: Advanced Features
- **Export Logs**: Save to SPIFFS as text file
- **Search**: Find text in logs
- **Timestamp Filter**: Show logs from last N minutes
- **Pause**: Freeze log capture temporarily
- **Log Levels per Component**: Filter by TAG

### Phase 5: Remote Debugging
- **WiFi Log Streaming**: View logs over HTTP
- **WebSocket Support**: Real-time log streaming to browser
- **Mobile App**: Dedicated log viewer app

## Dependencies

- ESP-IDF logging system (`esp_log.h`)
- LVGL textarea widget
- FreeRTOS mutex (thread safety)
- Screen manager (navigation)

## Risks and Mitigations

| Risk | Impact | Mitigation |
|------|--------|------------|
| High log volume crashes | Out of memory | Limit buffer size, disable capture when inactive |
| vprintf hook breaks logging | No serial logs | Chain to original vprintf, always call it first |
| UI update lag | Janky display | Reduce refresh rate to 1 Hz, optimize rendering |
| Buffer overflow | Lost logs | Circular buffer wraps gracefully, oldest lost first |
| Mutex deadlock | System hang | Use timeout on mutex acquisition (10ms) |

## References

- ESP-IDF logging: https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/log.html
- LVGL textarea: https://docs.lvgl.io/8/widgets/textarea.html
- FreeRTOS mutex: https://www.freertos.org/a00113.html

## Open Questions

1. Should we persist logs to SPIFFS for later viewing?
   - **Recommendation**: Phase 4 enhancement, not MVP

2. What's the optimal refresh rate for the UI?
   - **Recommendation**: 500ms (2 Hz), adjustable

3. Should we support exporting logs via WiFi?
   - **Recommendation**: Phase 5, remote debugging feature

4. Should we limit capture to certain components (TAGs)?
   - **Recommendation**: Phase 4, advanced filtering

## Approval Checklist

- [ ] Design reviewed and approved
- [ ] Memory footprint acceptable (85 KB)
- [ ] Performance impact negligible (<0.1% CPU)
- [ ] Implementation phases agreed upon
- [ ] Testing plan accepted
- [ ] User workflows validated
- [ ] Thread safety verified

---

**Next Steps After Approval:**
1. Implement circular buffer with mutex
2. Hook into `esp_log_set_vprintf()`
3. Create debug log screen UI
4. Add to settings menu
5. Test with high log volume
6. Validate on hardware
