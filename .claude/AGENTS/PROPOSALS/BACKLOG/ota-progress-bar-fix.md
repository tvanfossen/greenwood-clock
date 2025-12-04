# OTA Progress Bar Visual Update Fix

**Status**: Backlog
**Complexity**: Low
**Priority**: Low (cosmetic issue)

## Problem

The OTA progress bar doesn't visually update during firmware downloads, even though:
- Progress callbacks are firing correctly (visible in logs: 46%, 47%, 48%... 100%)
- Status label updates work fine ("Downloading: X%")
- `lv_bar_set_value()` is being called with correct percentages

Users can't see download progress visually, making it unclear if the update is working.

## Root Cause (To Investigate)

Possible causes:
1. Progress bar widget not visible (off-screen or hidden behind other elements)
2. LVGL port locking/unlocking timing issue during callbacks
3. Display refresh not triggered after bar value change
4. Bar styling makes changes invisible (same color for filled/unfilled?)

## Proposed Solution

### Phase 1: Debug (15 minutes)
1. Add visual indicator to confirm bar is on-screen
2. Log bar object coordinates and visibility flags
3. Check bar style (background vs indicator colors)
4. Verify `lv_bar_set_value()` return value

### Phase 2: Fix (30 minutes)
Based on findings:
- **If layout issue**: Adjust bar position/size in `create_ota_settings()`
- **If style issue**: Set contrasting colors for bar background vs indicator
- **If refresh issue**: Call `lv_obj_invalidate()` or `lv_refr_now()` after value update
- **If locking issue**: Restructure callback locking strategy

### Code Location
- File: `components/ui/screen_manager.c`
- Function: `ota_progress_callback()` (line ~560)
- Widget creation: `create_ota_settings()` (line ~762)

## Testing

1. Start OTA update
2. Verify progress bar visually fills from 0-100%
3. Verify bar position doesn't overlap other UI elements
4. Test with both small (quick) and large (slow) downloads

## Benefits

- Better user feedback during updates
- Confirms update is actively downloading vs stalled
- Polished UI experience

## Risks

None - cosmetic fix only, doesn't affect OTA functionality.

## Dependencies

None - OTA functionality already works, this is UI polish.

## Estimated Effort

1-2 hours (including testing)
