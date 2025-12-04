---
proposal_id: "lottie-integration-fixes"
title: "Greenwood Clock - Lottie Animation Integration Fixes"
github_issue: null
created: "2025-12-02"
updated: "2025-12-02"

status: "BACKLOG"
priority: "medium"
complexity: "medium"

category: ["bugfix", "feature"]
tags: ["esp32", "lvgl", "lottie", "animations", "ui"]

estimated_hours: 8
actual_hours: 0
progress_percent: 0

depends_on: []
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

# Greenwood Clock - Lottie Animation Integration Fixes

## Problem Statement

The greenwood-clock project includes Lottie animation support for weather icons, but the integration has several issues:

1. **Build Status**: Unknown if Lottie component currently builds successfully
2. **File Format**: Lottie expects JSON animation files, current format/structure unknown
3. **SPIFFS Integration**: Animation files should be loaded from SPIFFS but integration unclear
4. **Memory Usage**: Lottie animations can be memory-intensive on embedded systems
5. **Weather Mapping**: Mapping between weather conditions and Lottie animations may be incomplete
6. **Fallback Behavior**: No clear fallback when animations fail to load or render

## Current State

### Component Structure
- **Lottie Component**: `components/lottie/` exists in the project
- **Weather Component**: Uses weather condition codes from OpenWeather API
- **UI Component**: Should display weather icons/animations
- **SPIFFS**: Storage partition for animation files

### Questions to Answer
- Does lottie component build successfully?
- What animation files are currently in SPIFFS?
- Is lottie integrated into the weather display?
- What's the current memory footprint?

## Investigation Plan

### Phase 1: Current State Assessment
- [ ] Verify lottie component builds
- [ ] Check what files are in spiffs/ directory
- [ ] Review lottie component API and usage
- [ ] Test memory usage with animations loaded
- [ ] Identify weather conditions currently mapped

### Phase 2: Identify Issues
- [ ] List any build errors or warnings
- [ ] Document missing animation files
- [ ] Identify memory leaks or excessive usage
- [ ] Check for LVGL integration issues
- [ ] Verify animation playback works

### Phase 3: Fix Implementation
- [ ] Fix any build errors
- [ ] Add missing animation files
- [ ] Optimize memory usage
- [ ] Complete weather condition mapping
- [ ] Add fallback static icons
- [ ] Implement error handling

## Proposed Solution

Ensure Lottie animations are fully integrated and working:

### Build & Integration
- Fix any compilation errors in lottie component
- Ensure proper LVGL Lottie integration
- Add necessary dependencies to CMakeLists.txt
- Verify SPIFFS partition includes animations

### Animation Assets
- Source or create Lottie animations for all weather conditions
- Optimize animations for embedded use (reduce complexity)
- Organize animations in logical directory structure
- Document animation file naming conventions

### Memory Optimization
- Profile memory usage during animation playback
- Implement animation caching strategy
- Add animation preloading/unloading
- Consider reducing animation FPS for memory savings

### Error Handling
- Add fallback to static icons when animations fail
- Handle missing animation files gracefully
- Log animation errors for debugging
- Provide user feedback on animation issues

## Weather Condition Mapping

OpenWeather API provides condition codes. Need animations for:

### Main Conditions
- Clear (day/night)
- Clouds (few, scattered, broken, overcast)
- Rain (light, moderate, heavy, freezing)
- Snow (light, moderate, heavy)
- Thunderstorm (with/without rain)
- Drizzle
- Mist/Fog
- Extreme (tornado, hurricane, etc.)

### Day/Night Variants
- Many conditions need separate day/night animations
- Sun/moon visibility affects icon choice

## Acceptance Criteria

- [ ] Lottie component builds without errors
- [ ] All common weather conditions have animations
- [ ] Animations load and play smoothly (>15 FPS)
- [ ] Memory usage acceptable (<1MB per animation)
- [ ] Fallback icons display when animations unavailable
- [ ] No crashes or memory leaks from animations
- [ ] Day/night variants work correctly
- [ ] Animation assets documented

## Technical Details

### Expected Component API
```c
// components/lottie/lottie.h

/**
 * @brief Load Lottie animation from SPIFFS
 *
 * @param path Path to Lottie JSON file
 * @return Handle to animation or NULL on error
 */
lv_anim_t* lottie_load(const char* path);

/**
 * @brief Create LVGL object for Lottie animation
 *
 * @param parent Parent LVGL object
 * @param anim Animation handle from lottie_load
 * @return LVGL object or NULL on error
 */
lv_obj_t* lottie_create_obj(lv_obj_t* parent, lv_anim_t* anim);

/**
 * @brief Free Lottie animation resources
 *
 * @param anim Animation handle to free
 */
void lottie_free(lv_anim_t* anim);
```

### Weather Icon Integration
```c
// components/ui/weather_icon.c

typedef enum {
    WEATHER_CLEAR_DAY,
    WEATHER_CLEAR_NIGHT,
    WEATHER_CLOUDY,
    WEATHER_RAIN,
    WEATHER_SNOW,
    WEATHER_THUNDERSTORM,
    // ... etc
} weather_icon_type_t;

/**
 * @brief Display weather icon (Lottie or fallback)
 *
 * @param parent Parent LVGL object
 * @param type Weather icon type
 * @param is_day Day (true) or night (false)
 * @return LVGL object with icon/animation
 */
lv_obj_t* weather_icon_create(lv_obj_t* parent,
                               weather_icon_type_t type,
                               bool is_day);
```

### Animation File Structure
```
spiffs/
  animations/
    weather/
      clear_day.json
      clear_night.json
      clouds_few.json
      clouds_broken.json
      rain_light.json
      rain_heavy.json
      snow.json
      thunderstorm.json
      fog.json
  icons/  # Fallback static images
    clear_day.png
    clear_night.png
    # ... etc
```

### Memory Profiling
```c
// Before loading animation
size_t heap_before = esp_get_free_heap_size();

// Load and display animation
lv_anim_t* anim = lottie_load("/spiffs/animations/weather/rain.json");
lv_obj_t* obj = lottie_create_obj(parent, anim);

// After loading
size_t heap_after = esp_get_free_heap_size();
ESP_LOGI(TAG, "Animation memory usage: %zu bytes", heap_before - heap_after);

// Should be <1MB per animation
```

## Animation Asset Sources

Potential sources for weather Lottie animations:

1. **LottieFiles**: https://lottiefiles.com/
   - Search for "weather" animations
   - Filter by free/license
   - Download JSON format

2. **Custom Creation**:
   - Adobe After Effects + Bodymovin plugin
   - Optimize for embedded (reduce complexity)
   - Keep animation under 100KB

3. **Open Source Collections**:
   - Check existing weather app repositories
   - Ensure license compatibility

## Risks & Mitigations

| Risk | Impact | Likelihood | Mitigation |
|------|--------|------------|------------|
| Animations too memory-intensive | High | Medium | Profile and optimize, use simpler animations |
| Missing animation files | Medium | Low | Fallback to static icons |
| Lottie library compatibility issues | High | Low | Test with LVGL version, update if needed |
| Slow animation rendering | Medium | Medium | Reduce FPS, simplify animations |
| SPIFFS storage limitations | Medium | Low | Compress animations, limit count |

## Success Metrics

- Animation load time: <500ms per animation
- Animation playback: >15 FPS smooth
- Memory usage: <1MB per animation
- Coverage: 100% of common weather conditions
- Fallback success rate: 100% when animation unavailable
- Zero crashes from animation subsystem

## Testing Plan

### Unit Tests
- Test animation loading from SPIFFS
- Test fallback to static icons
- Test memory cleanup on animation free
- Test invalid file handling

### Integration Tests
- Test weather condition → animation mapping
- Test day/night variant selection
- Test animation switching when weather updates
- Test multiple animations loaded simultaneously

### Performance Tests
- Profile memory usage with all animations
- Measure animation FPS on device
- Test animation preloading performance
- Monitor heap fragmentation

## Related Work

- LVGL Lottie Integration: https://docs.lvgl.io/master/libs/rlottie.html
- LottieFiles: https://lottiefiles.com/
- OpenWeather Condition Codes: https://openweathermap.org/weather-conditions

## Notes

### Why Lottie?

Advantages:
- Smooth, scalable vector animations
- Smaller than video files
- Resolution-independent
- Widely supported format

Disadvantages:
- Can be memory-intensive
- Requires JSON parsing
- Complex animations may lag on embedded

### Alternative Approach

If Lottie proves too resource-intensive:
- Use static PNG icons (much lighter)
- Use simple LVGL animations (built-in)
- Use GIF animations (heavier than Lottie)

### Priority Justification

**Medium priority** because:
- Weather display works without animations (has fallback)
- Visual polish, not critical functionality
- Moderate complexity to implement correctly
- Improves user experience significantly

---

**Repository**: https://github.com/tvanfossen/greenwood-clock

**Created**: 2025-12-02

**Status**: Backlog (medium priority, needs investigation phase first)
