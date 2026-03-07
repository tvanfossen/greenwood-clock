---
version: 1.0.0
type: proposal
schema_version: 1
id: P1-20260302-002
title: "Display State Machine (TinyFSM)"
priority: P1
component: display_fsm
author: Tristan VanFossen
author_email: vanfosst@gmail.com
created: 2026-03-02
updated: 2026-03-04
tags: [display, fsm, tinyfsm, architecture, lvgl, ui]
completed_date: null
scoped_files:
  - components/display_fsm/
  - components/ui/ui.c
  - components/ui/screen_manager.c
  - main/main.cpp
depends_on: []
blocks:
  - P1-20260302-003
  - P2-20260302-004
  - P2-20260302-005
---

# Display State Machine (TinyFSM)

## Problem Statement

`screen_manager.c` is an implicit state machine: stack-based navigation, screens cached
forever (never destroyed), no teardown, no event dispatch, no way for external tasks
(weather, HTTP API, alerts) to drive display changes. Adding weather overlay, radar view,
alert banners, and surprise messages to this architecture will produce spaghetti.

## Proposed Solution

Replace screen_manager with TinyFSM-based architecture in new `components/display_fsm/`.
Single FreeRTOS task consumes event queue, dispatches to TinyFSM states. All LVGL ops
under lock in FSM task. States own their widgets and clean up on exit.

See master plan: `.claude/proposals/BACKLOG/master-plan-display-evolution.md` for full
architecture specification including:
- Threading model (single consumer, multi-producer queue)
- Display states (ClockFull home + 7 overlay states + Settings + SurpriseMessage)
- Event-driven model with debounce (DisplayScheduler)
- Layer 1/2/3 architecture (Widgets → FSM Base → Concrete States)
- C/C++ bridge (extern "C" API)

## Acceptance Criteria

- [ ] TinyFSM compiles under ESP-IDF v5.5 (gnu++23, no RTTI, no exceptions)
- [ ] FSM task boots, enters ClockFull state, displays time correctly
- [ ] Gesture swipe-up transitions to Settings, back returns to ClockFull
- [ ] All settings screens functional
- [ ] `display_fsm_send_event()` callable from C components
- [ ] Entry/exit create/destroy ALL LVGL objects — no leaked widgets
- [ ] LVGL fade-in/out animations on state transitions
- [ ] DisplayScheduler debounce prevents re-triggering within cooldown
- [ ] Return timer fires → returns to ClockFull
- [ ] No LVGL lock held across blocking operations
