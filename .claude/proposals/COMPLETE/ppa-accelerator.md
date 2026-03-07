---
version: 1.0.0
type: proposal
schema_version: 1
id: P1-20260225-001
title: "PPA Hardware Acceleration + Lottie Weather Animations"
priority: P1
component: lvgl/ppa
author: Tristan VanFossen
author_email: vanfosst@gmail.com
created: 2026-02-25
updated: 2026-02-25
tags: [performance, ppa, lvgl, hardware-acceleration, display, lottie, weather]
completed_date: 2026-03-03
scoped_files:
  - components/lvgl/src/draw/lv_image_decoder.c
  - components/lvgl_mem_esp/lv_mem_esp.c
  - components/lottie/
  - components/ui/ui.c
  - components/weather/weather.h
  - sdkconfig.defaults
  - docs/PPA_STATUS.md
depends_on: []
blocks:
  - P1-20260225-002
---

# PPA Hardware Acceleration + Lottie Weather Animations

## Problem Statement

PPA (Pixel Processing Accelerator) is ESP32-P4's dedicated hardware unit for rectangle fills
(~9x faster), image blending (~30% faster), and display rotation/mirroring (~40% faster).
Without PPA, all LVGL rendering is software-only on the CPU: frame drops, sluggish touch
response, GIF playback stutter.

PPA is configured on (`CONFIG_LV_USE_PPA=y`, `CONFIG_LVGL_PORT_ENABLE_PPA=y`) but is believed
non-functional on hardware. Failure signature:

```
E (xxxxx) ppa_fill: out.buffer addr or out.buffer_size not aligned to cache line size
[Error] lv_draw_ppa_fill: PPA fill failed: 258
[Warn] lv_draw_buf_init: Data is not aligned, ignored
```

The device silently falls back to software rendering — performance cost is real but invisible
in logs unless you're looking.

**Primary motivating use case**: Weather condition icons are currently fetched as PNGs from
Weatherbit's CDN. With the Azure backend (P1-20260225-002), the icon URL is dropped entirely
and the condition is identified by a numeric code. The target UX replaces static PNG icons
with **per-condition Lottie animations** (sunny, rain, snow, cloudy, storm, etc.) stored on
SD card. Lottie is already in `components/lottie/` (UNVERIFIED). PPA is required for Lottie
to render at acceptable frame rates — without it, a Lottie animation at 1024×600 will either
stutter badly or kill GIF background performance.

**Degradation path**: If PPA cannot be made to work (Option C), weather icons fall back to
static PNG assets on SPIFFS — no animation, but functional.

## Proposed Solution

Three fix approaches in priority order:

**Option A (preferred)**: Post-decode pixel data realignment in LVGL's image decoder. After
decoding a PNG/GIF/Lottie frame, copy pixel data into a fresh 64-byte-aligned buffer before
handing it to PPA. Memory doubles briefly during decode — acceptable given 8 MB SPIRAM.

**Option B (fallback)**: Switch from PPA to DMA2D. Relaxed alignment requirements, no decoder
changes needed. Prior DMA2D attempts failed on STM32-specific header conflicts — verify
resolved in ESP-IDF 5.5 before attempting. Estimated 30–50% improvement vs pure software;
sufficient for Lottie if frame sizes are managed.

**Option C (last resort)**: Software rendering + static PNG icons. Lottie dropped. GIF
backgrounds may need dimension/FPS reduction. Degrades UX but preserves stability.

## Acceptance Criteria

**PPA / rendering:**
- [ ] Boot log shows all three `[2] ✓ PPA` messages with no `ppa_fill` errors
- [ ] GIF background playback smooth at sustained ≥30 FPS
- [ ] Touch input latency <100 ms under active GIF playback
- [ ] Screen transitions (clock → settings) complete in <300 ms
- [ ] No regression in display stability (no new blue screens or watchdog triggers)
- [ ] `docs/PPA_STATUS.md` updated with verified hardware result, approach taken, benchmarks

**Lottie weather animations:**
- [ ] `components/lottie/` integration confirmed working (UNVERIFIED status resolved)
- [ ] Per-condition Lottie JSON files created/sourced for all major Weatherbit condition groups
- [ ] Firmware maps Weatherbit `weather_code` → Lottie file path on SD card
- [ ] Lottie animation renders without frame drops alongside GIF background
- [ ] Graceful fallback: if Lottie file missing, show static PNG icon from SPIFFS

## Weatherbit Condition Code → Animation Mapping

Weatherbit codes group naturally into animation sets. Minimum viable set:

| Group | Code range | Animation | Lottie file |
|---|---|---|---|
| Clear/sunny | 800 | sunny.json | A:/lottie/weather/sunny.json |
| Partly cloudy | 801–802 | partly_cloudy.json | A:/lottie/weather/partly_cloudy.json |
| Overcast | 803–804 | cloudy.json | A:/lottie/weather/cloudy.json |
| Drizzle | 300–321 | drizzle.json | A:/lottie/weather/drizzle.json |
| Rain | 500–531 | rain.json | A:/lottie/weather/rain.json |
| Snow | 600–622 | snow.json | A:/lottie/weather/snow.json |
| Thunderstorm | 200–232 | storm.json | A:/lottie/weather/storm.json |
| Fog/mist | 700–741 | fog.json | A:/lottie/weather/fog.json |

Source Lottie files from LottieFiles (free CC BY license). Test each file for LVGL
compatibility before committing to the set — Lottie feature support varies by player version.

## Implementation Plan

### Phase 1: Baseline Measurement + Root Cause Confirmation

Establish ground truth before touching code.

1. Flash current firmware, connect UDP log listener (`python tools/udp_log_listen.py`)
2. Trigger GIF playback, watch for `ppa_fill` errors — confirm PPA is failing
3. Add `lv_tick_get()` instrumentation around `lv_task_handler()` to measure actual frame
   periods under GIF load — establish the software-render baseline
4. Verify all required config flags in `build/config/sdkconfig.h`
5. Add temporary log in `lv_mem_esp.c` confirming `lv_malloc` returns 64-byte-aligned pointers
6. Capture the exact failing pixel data pointer address — confirm it is NOT 64-byte aligned
7. Document findings in `docs/PPA_STATUS.md`

### Phase 2: Option A — Image Decoder Realignment

Target: `components/lvgl/src/draw/lv_image_decoder.c`

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

Hook into both PNG (LodePNG) and GIF decode paths — they are separate in LVGL. Validate:
no `ppa_fill` errors in logs, frame timing improvement measurable vs Phase 1 baseline.

### Phase 3: Option B — DMA2D (if Option A fails)

1. Set `CONFIG_LV_USE_PPA=n`, `CONFIG_LV_USE_DRAW_DMA2D=y` in `sdkconfig.defaults`
2. Verify DMA2D headers are ESP32-specific (not STM32 — prior failure point)
3. Rebuild, measure frame timing vs Phase 1 baseline
4. Accept if improvement exceeds 20% frame time reduction

### Phase 4: Lottie Integration

Requires Phase 2 or 3 to succeed first.

1. Verify `components/lottie/` builds and renders a test animation — resolve UNVERIFIED status
2. Source Lottie JSON files for all 8 condition groups from LottieFiles; validate LVGL compat
3. Store files at `A:/lottie/weather/<name>.json` on SD card (via `tools/file_push.py`)
4. Add `weather_code_to_lottie_path()` lookup in `components/weather/weather.h`:

   ```c
   const char* weather_code_to_lottie_path(int weather_code);
   ```

5. Update weather UI in `components/ui/ui.c`:
   - Replace `lv_img_create()` + `weather_fetch_icon()` with `lv_lottie_create()` (or equivalent API)
   - If `lv_lottie_create()` fails or file missing: fall back to static PNG icon on SPIFFS
   - Lottie widget sized to fit weather panel — does not overlap clock face

6. Add static fallback PNGs to SPIFFS (`B:/icons/weather/<name>.png`) for Option C

### Phase 5: Performance Validation + Documentation

1. 30-minute soak test: GIF background + Lottie weather animation simultaneously — no blue screens
2. Log heap usage before/after alignment buffer and Lottie decode — confirm no OOM
3. Confirm frame timing with both active meets acceptance criteria
4. Update `docs/PPA_STATUS.md` — verified status, approach, benchmark numbers
5. Update CLAUDE.md PPA status section

## Risks & Considerations

- **LVGL submodule modification**: `components/lvgl` is a managed component. Edits to
  `lv_image_decoder.c` will conflict on `idf_component_manager` updates. Pin component to a
  specific hash or isolate changes as a patch file before modifying.
- **GIF + Lottie simultaneous heap pressure**: 1024×600 RGB565 = 1.2 MB per frame. GIF frame
  buffer + Lottie frame buffer + alignment copy buffer may approach 8 MB SPIRAM limit under
  load. Log heap at worst-case combination before declaring success.
- **GIF and PNG decode paths are separate**: Realignment in Phase 2 must hook into both. A fix
  that only touches the PNG decoder will leave GIF frames unaligned — PPA will still fail for
  animated backgrounds.
- **Lottie component UNVERIFIED**: `components/lottie/` has never been confirmed working on
  this hardware. Phase 4 may discover it needs significant integration work or is broken on
  ESP32-P4. If Lottie is not viable, static PNGs on SPIFFS are the fallback.
- **Lottie LottieFiles license**: CC BY license requires attribution in app/docs. Verify
  specific files' licenses before shipping. Some premium LottieFiles require paid license.
- **DMA2D STM32 conflict (Option B)**: Prior attempt pulled in STM32-specific headers.
  May require `#ifdef CONFIG_IDF_TARGET_ESP32P4` guards or manual header path exclusion.
- **Performance regression in Option A**: Alignment copy adds CPU time on every decode. Net
  gain must exceed copy cost. For a 1.2 MB frame buffer copied once per decode cycle this is
  likely fine, but measure it.

## Implementation Log

*Entries added as work progresses*

## References

- `docs/PPA_STATUS.md` — root cause analysis, failure history, fix approaches
- `components/lvgl_mem_esp/lv_mem_esp.c` — custom 64-byte aligned allocator
- `components/lottie/` — Lottie player component (UNVERIFIED)
- ESP32-P4 Technical Reference Manual — PPA chapter (alignment requirements)
- LVGL `lv_draw_buf` API — `data` vs `unaligned_data` fields
- LottieFiles (lottiefiles.com) — source for weather condition animations
- Weatherbit condition codes: https://www.weatherbit.io/api/codes
- P1-20260225-002 (Azure backend) — provides `weather_code` field that drives Lottie selection
