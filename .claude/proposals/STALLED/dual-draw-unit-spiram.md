---
version: 1.0.0
type: proposal
schema_version: 1
id: P2-20260302-001
title: "Dual LVGL SW Draw Units with SPIRAM Task Stacks"
priority: P2
component: lvgl/draw
author: Tristan VanFossen
author_email: vanfosst@gmail.com
created: 2026-03-02
updated: 2026-03-02
status: STALLED
stall_reason: "Adversarial analysis showed marginal benefit (~3ms/frame) vs high cost (104KB DRAM, LVGL lock deadlock blocker, 3 unresolved crash vectors). PPA already offloads expensive ops. Lottie bottleneck is render_task serial throughput, not SW draw dispatch."
tags: [performance, lvgl, draw-units, spiram, freertos, thorvg]
completed_date: null
scoped_files:
  - sdkconfig.defaults
  - components/lvgl/src/draw/sw/lv_draw_sw.c
  - components/lvgl/src/widgets/lottie/lv_lottie.c
  - components/lvgl/src/widgets/lottie/lv_lottie_private.h
  - components/lvgl/src/lv_conf_internal.h
---

## Problem

LVGL SW draw pipeline is single-threaded (1 draw unit). ESP32-P4 is dual-core 360 MHz
with 32 MB SPIRAM. The second core is idle during LVGL rendering. Enabling a second SW
draw unit would let LVGL parallelize fill/blit/blend operations across both cores.

## Previous Attempt (2026-03-02) — Failed

Enabled `LV_OS_FREERTOS` + `LV_DRAW_SW_DRAW_UNIT_CNT=2`. Hit three sequential blockers:

| # | Crash | Root Cause | Fix Applied |
|---|---|---|---|
| 1 | `xTaskCreateStatic` with SPIRAM buffer → abort | `portVALID_STACK_MEM` rejects SPIRAM on ESP32-P4 | Switched to DRAM BSS stack |
| 2 | `lv_canvas_get_draw_buf` in render_task → abort | `LV_OS_FREERTOS` adds `LV_ASSERT_OBJ` lock checks; render_task doesn't hold LVGL lock | Moved buffer clear to LVGL task |
| 3 | ThorVG `tvg_canvas_draw` from render_task → abort | Same PC/RA as #1; ThorVG internal abort when called from non-LVGL task under `LV_OS_FREERTOS` | **No fix — reverted to single draw unit** |

Crash #3 is the fundamental blocker. ThorVG calls `abort()` when its rendering pipeline
runs on a task other than the one that initialized the ThorVG engine, but ONLY under
`LV_OS_FREERTOS` mode. Under `LV_OS_NONE`, the same render_task architecture works.

## Blockers to Resolve

### B1: ESP32-P4 FreeRTOS port rejects SPIRAM task stacks

`portVALID_STACK_MEM()` in ESP-IDF v5.5 FreeRTOS port for ESP32-P4 only accepts internal
DRAM addresses. Both `xTaskCreateWithCaps(MALLOC_CAP_SPIRAM)` and `xTaskCreateStatic`
with a `heap_caps_malloc(MALLOC_CAP_SPIRAM)` buffer trigger `configASSERT` → abort.

**Investigation needed:**
- Is this an ESP-IDF v5.5 bug or intentional for P4? Check ESP-IDF GitHub issues.
- Does ESP-IDF v5.4 or v5.6 behave differently?
- Is there a Kconfig option to relax `portVALID_STACK_MEM` for SPIRAM?
- Could `CONFIG_FREERTOS_TASK_FUNCTION_WRAPPER` or cache-line alignment solve this?

**Impact:** Without SPIRAM task stacks, LVGL draw threads (2 × 32 KB = 64 KB) plus the
lottie render task (40 KB) consume 104 KB of the ~228 KB available DRAM. Leaves only
~124 KB for all other DRAM allocations. Tight but may be workable if nothing else is
added.

### B2: LV_OS_FREERTOS lock assertions block cross-task LVGL access

With `LV_OS_FREERTOS`, LVGL wraps object access in `LV_ASSERT_OBJ` which checks that the
LVGL mutex is held. Any non-LVGL task calling `lv_canvas_get_draw_buf()`,
`lv_draw_buf_clear()`, or similar functions triggers abort.

**Resolution options:**
- **Option A**: Pre-extract all data the render_task needs (buffer pointer, stride, size)
  on the LVGL task and pass via the job queue. Render_task calls zero `lv_*` functions.
  Status: partially implemented (buffer clear moved to LVGL task), but ThorVG still
  crashes (see B3).
- **Option B**: Acquire `lvgl_port_lock()` in the render_task before LVGL calls. Risk:
  deadlock if LVGL task is blocked waiting for render_done_sem while holding the lock.
  Would require restructuring the blocking protocol.

### B3: ThorVG abort under LV_OS_FREERTOS (CRITICAL — root cause unknown)

ThorVG's `tvg_canvas_draw()` / `tvg_canvas_sync()` calls `abort()` when invoked from the
lottie render_task under `LV_OS_FREERTOS`. The crash signature:

```
PC=0x4ff00d9a  RA=0x4ff0b56a  SP=0x4ff26a30
Stage: health_loop
Reason: Illegal instruction
```

Same PC/RA as the `portVALID_STACK_MEM` crash, suggesting both go through `configASSERT`.
The SP is 1207 bytes from the render_task's stack top — crash fires on the FIRST ThorVG
call after dequeue, not deep in the render pipeline.

**Investigation needed:**
- Decode PC/RA using `riscv32-esp-elf-addr2line` against the .elf to identify exact
  assert location.
- Does `LV_OS_FREERTOS` change ThorVG's initialization path? Check if `tvg_engine_init`
  registers thread affinity.
- Does ThorVG use `lv_malloc` internally, which under `LV_OS_FREERTOS` might check lock
  state?
- Would initializing ThorVG engine on the render_task instead of the draw unit init solve
  the thread affinity issue?
- Test: does ThorVG work from a non-LVGL task if `tvg_engine_init` is called from THAT
  task?

### B4: ThorVG worker thread stack size (secondary)

When `LV_DRAW_SW_DRAW_UNIT_CNT > 1`, LVGL's stock `lv_draw_sw.c` passes the count to
`tvg_engine_init(TVG_ENGINE_SW, N)`, spawning N ThorVG worker pthreads with default
3-8 KB stack — insufficient for ThorVG's C++ call depth.

**Resolution:** Already solved by patching `lv_draw_sw.c` to always pass `0` (keeping
ThorVG single-threaded on the dedicated render_task). This patch must be maintained if
dual draw units are re-enabled.

## Proposed Investigation Plan

1. **Decode the crash** — Run `riscv32-esp-elf-addr2line -e build/greenwood-clock.elf
   0x4ff00d9a 0x4ff0b56a` against a build with `LV_OS_FREERTOS` enabled. Identify
   whether the assert is in FreeRTOS port code, LVGL, or ThorVG.

2. **Test ThorVG thread affinity** — Create a minimal test: initialize ThorVG on task A,
   render on task B, under `LV_OS_FREERTOS`. Confirm whether the crash is ThorVG-specific
   or LVGL-mediated.

3. **Check ESP-IDF SPIRAM stack support** — Search ESP-IDF issues/docs for P4 + SPIRAM +
   FreeRTOS task stack. Determine if this is a known limitation or fixable via config.

4. **Evaluate DRAM-only path** — If SPIRAM stacks remain blocked, calculate whether
   104 KB DRAM for draw threads + render task is sustainable alongside all other DRAM
   consumers (ThorVG scene graph, FreeRTOS TCBs, semaphores, etc.).

5. **Prototype fix for B3** — If addr2line reveals the assert is in `lv_malloc` lock
   checking, try providing ThorVG a custom allocator that bypasses LVGL's heap.

## Success Criteria

- [ ] `LV_OS_FREERTOS=y` + `LV_DRAW_SW_DRAW_UNIT_CNT=2` builds and boots without crash
- [ ] Main clock screen Lottie renders animated frames (not junk pixels)
- [ ] Settings screen Lottie preview renders simultaneously
- [ ] No DRAM exhaustion (internal_free > 80 KB at steady state)
- [ ] Display watchdog does not fire (frames render within 15s timeout)

## DRAM Budget (projected)

| Consumer | DRAM (KB) |
|---|---|
| LVGL draw thread 0 | 32 |
| LVGL draw thread 1 | 32 |
| Lottie shared render task (BSS) | 40 |
| FreeRTOS TCBs + semaphores | ~5 |
| ThorVG scene graph (per widget, if DRAM fallback) | ~50-100 |
| Remaining for load_task + other | ~20-70 |
| **Total available** | **~228** |

@architect: BLOCKED — B3 root cause unknown. Need addr2line decode before proceeding.
