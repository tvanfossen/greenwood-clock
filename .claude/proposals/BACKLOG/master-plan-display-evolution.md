# Master Plan: Greenwood Clock Display Architecture Evolution

## Context

The clock works — time displays, Lottie hummingbird animates at 20fps, settings screens navigate, background GIFs play. But the architecture can't support what's coming: weather display with live radar, alerts, animated conditions, surprise messages, and remote control. `screen_manager.c` is an implicit state machine with no teardown, no event system, and no way for external tasks to drive display changes. Weather is disabled (Weatherbit API, not integrated). There's no mechanism for a remote control page or push notifications.

This plan introduces 4 proposals that together transform the display into an event-driven state machine with weather integration, rich animations, and web-based remote control.

## Dependency Graph

```
  P1-20260302-002 (Display FSM)  ◄── Foundation, must be first
       │
       ├──────────────────────┐
       ▼                      ▼
  P1-20260302-003         P2-20260302-005
  (NWS Weather)           (Web Control)
       │                      │
       ▼                      │
  P2-20260302-004             │
  (Weather Animations) ◄──────┘
```

## Prerequisite Closures

Before starting this work:

1. **P1-20260225-001 (PPA + Lottie Weather) → COMPLETE**: PPA hardware acceleration is functional.
   64-byte alignment handled by `lv_mem_esp.c` + `LV_DRAW_BUF_STRIDE_ALIGN=64`. PPA config enabled
   (`CONFIG_LV_USE_PPA=y`, `CONFIG_LV_USE_PPA_IMG=y`, `CONFIG_LVGL_PORT_ENABLE_PPA=y`).
   Weatherbit condition mapping is superseded by NWS. Move proposal to `COMPLETE/`.

2. **LVGL Fork**: Lottie render_task changes (shared singleton, queue dispatch, BSS DRAM stack)
   live in `components/lvgl/` on branch `tvanfossen/lottie` with commit `d8db42a64` + uncommitted
   changes in `lv_lottie.c` and `lv_lottie_private.h`. Fork LVGL repo to `tvanfossen/lvgl`,
   push branch, update submodule URL. This preserves our changes independent of upstream.

## Relationship to Existing Proposals

| Existing | Status | Action |
|----------|--------|--------|
| P1-20260225-001 (PPA + Lottie Weather) | BACKLOG | **Move to COMPLETE** — PPA working, Weatherbit superseded by NWS |
| P2-20260302-001 (Dual Draw Units) | STALLED | No change. Independent. |
| feb-2026-master-plan (Pre-commit + OTA) | BACKLOG | Independent workstream. FSM refactor touches screen_manager.c — coordinate if parallel. |

## Sequencing

```
Phase 0:  Close PPA proposal, create LVGL fork  ████
Phase 1:  P1-20260302-002  Display FSM              ██████████░░░░░░░░░░
Phase 2a: P1-20260302-003  NWS Weather                   ██████████░░░░░░
Phase 2b: P2-20260302-005  Web Control                    █████████░░░░░░  (parallel with 2a)
Phase 3:  P2-20260302-004  Weather Animations                   ██████████
```

---

# Visual Design Language & Skills Integration

All UI work across all proposals follows a unified design language. Reference skills at
`~/.claude/skills/` at implementation time for specific phases.

## Design Theme: "Arctic Observatory"

Inspired by `~/.claude/skills/theme-factory/themes/arctic-frost.md` and
`~/.claude/skills/theme-factory/themes/midnight-galaxy.md`, blended for a clock that
transitions between day and night aesthetics.

| Element | Day Mode | Night Mode |
|---------|----------|------------|
| Background | Soft blue-gray (#d4e4f7) | Deep purple-black (#1a1a2e) |
| Primary text | Charcoal (#2b2b2b) | Crisp white (#fafafa) |
| Accent | Steel blue (#4a6fa5) | Lavender (#a490c2) |
| Alert warm | Coral (#e76f51) | Same |
| Alert cool | Teal (#2d8b8b) | Same |
| Temperature | Large, bold, high contrast | Same |
| Subtle text | Mid gray (#708090) | Silver (#c0c0c0) |

Day/night mode switches automatically at sunrise/sunset (computed by `astro_calc`).

## Typography Hierarchy (Nunito — already in `components/fonts/`)

| Use | Font | Size |
|-----|------|------|
| Clock full | Nunito 256pt | Primary time display |
| Clock minimized | Nunito 128pt | Corner time |
| Temperature | Nunito 128pt | Weather overlay anchor |
| Section headers | Nunito 48pt | State labels, day names |
| Body text | Nunito 48pt | Descriptions, data values |
| Small data | System default 24pt | Forecast strip, metadata |

## Animation & Motion Principles

Reference `~/.claude/skills/frontend-design/SKILL.md` motion guidelines:
- **Purposeful motion** — animations convey information (rain direction, wind strength), not decoration
- **Ease-out for entries** (300ms) — elements arrive with deceleration (natural)
- **Ease-in for exits** (200ms) — elements depart with acceleration (quick)
- **No linear motion** — all transitions use cubic bezier easing
- **Subtle is better** — continuous animations (weather Lottie) should not compete with data
- **Particle physics** — rain falls at gravity-appropriate speed, snow drifts with wobble

## Lottie Design Constraints

Reference `~/.claude/skills/canvas-design/SKILL.md` craftsmanship standards and
`~/.claude/skills/algorithmic-art/SKILL.md` for particle/generative patterns:
- **Max 10-15 shapes** per weather condition animation (ThorVG performance)
- **Unified palette** — all animations use the Arctic Observatory color scheme
- **Anchor-centric** — animations frame/surround their data element (temperature, moon)
- **Looping** — seamless loop, no visible restart point
- **No "AI-generated" feel** — clean, intentional, hand-crafted aesthetic

## Skills Application Points

| Phase | Step | Skill | Application |
|-------|------|-------|-------------|
| 1 | 1.3 ClockWidget | theme-factory | Day/night color scheme for clock text |
| 1 | 1.7 Settings | frontend-design | Settings UI layout, spacing, touch targets |
| 2b | 2b.5 Web page | frontend-design | Mobile-first design, bold aesthetic, motion |
| 2b | 2b.5 Web page | brand-guidelines | Typography pairing, color application |
| 3 | 3.1 WeatherCard | canvas-design | Layout composition, visual hierarchy |
| 3 | 3.3 ParticleSystem | algorithmic-art | Seeded randomness, natural motion patterns |
| 3 | 3.7 AlertBanner | theme-factory | Severity color palette, contrast |
| 3 | 3.8 Astronomy | canvas-design | Moon phase visual, celestial layout |
| 3 | 3.14 Lottie assets | canvas-design | Animation design philosophy, craftsmanship |
| 3 | 3.14 Lottie assets | algorithmic-art | Generative patterns for particles in Lottie |

---

# Proposal 1: P1-20260302-002 — Display State Machine (TinyFSM)

## Problem

`screen_manager.c` is an implicit state machine: stack-based navigation, screens cached forever (never destroyed), no teardown, no event dispatch, no way for external tasks (weather, HTTP API, alerts) to drive display changes. Adding weather overlay, radar view, alert banners, and surprise messages to this architecture will produce spaghetti.

The clock must always be visible (minimized when not primary), states must own their widgets and clean up on exit, and external events (weather data ready, alert received, HTTP push command) must drive state transitions.

## Proposed Solution

### TinyFSM Integration

New component: `components/display_fsm/`

```
components/display_fsm/
├── CMakeLists.txt
├── include/
│   ├── display_fsm.h           # C API (extern "C" wrappers) + base class declaration
│   ├── display_events.h        # Event struct definitions (C-compatible)
│   ├── display_states.h        # Forward declarations of all state classes
│   └── tinyfsm.hpp             # TinyFSM header (vendored, MIT license)
└── src/
    ├── display_fsm_base.cpp    # Base class: clock_minimize/restore, alert banner, shared logic
    ├── display_fsm_task.cpp    # FreeRTOS task: xQueueReceive → dispatch loop
    ├── display_scheduler.cpp   # Event-driven display scheduling, debounce, return-to-clock timers
    ├── state_clock.cpp         # ClockFull — full-screen clock display
    ├── state_weather.cpp       # WeatherOverlay — minimized clock + conditions + forecast
    ├── state_radar.cpp         # RadarOverlay — minimized clock + map + radar PNG
    ├── state_astronomy.cpp     # Astronomy — moon phase, sunrise/sunset, aurora, events
    ├── state_photos.cpp        # PhotoSlideshow — SD card image rotation
    ├── state_ambient.cpp       # AmbientDashboard — aggregate info panel
    ├── state_surprise.cpp      # SurpriseMessage — JSON layout parser + rendering
    ├── state_settings.cpp      # Settings — sub-states for each settings screen
    ├── astro_calc.cpp          # Moon phase, sunrise/sunset, solar position algorithms
    ├── weather_widgets.cpp     # Weather widget factories (temp, forecast strip, condition icon)
    ├── particle_effects.cpp    # LVGL native rain/snow/flash particle system
    └── radar_view.cpp          # Map + radar image layer management, home marker
```

### Threading Model

```
┌─────────────────────────┐     ┌─────────────────────────────────┐
│  Data Tasks (FreeRTOS)  │     │  FSM Task (single FreeRTOS task)│
│                         │     │                                 │
│  nws_weather_task ──────┼─Q──►│  while (xQueueReceive(q)) {     │
│  nws_alert_task ────────┼─Q──►│    switch (evt.type) {          │
│  nws_radar_task ────────┼─Q──►│      dispatch to TinyFSM        │
│  http_api handlers ─────┼─Q──►│    }                            │
│  lvgl gesture cb ───────┼─Q──►│  (all LVGL ops under lock here) │
└─────────────────────────┘     └─────────────────────────────────┘
```

Single event queue (`xQueueSend` from any task). FSM task is the sole consumer. TinyFSM's static state pointer is only accessed from this one task — no thread safety issues.

### Display States — Event-Driven with Clock Home

The clock is the **home state**. Other states appear briefly (~30s) when data changes trigger
them, then return to clock. This is event-driven, not a timed carousel. The device is mounted
high on a wall — no touch needed except swipe-up for settings.

```
                      ┌─────────────────────────────────┐
                      │        ClockFull (HOME)          │
                      │  Always returns here after 30s   │
                      └──┬──┬──┬──┬──┬──┬───────────────┘
                         │  │  │  │  │  │
    EvWeatherUpdate ─────┘  │  │  │  │  │   (each shows ~30s, returns to clock)
    EvRadarReady ───────────┘  │  │  │  │   (debounced — won't re-trigger within cooldown)
    EvAstroTrigger ────────────┘  │  │  │
    EvPhotoTrigger ────────────────┘  │  │
    EvAmbientTrigger ──────────────────┘  │
    EvSurpriseMessage ────────────────────┘  (custom duration_s, not 30s)

    ALL states: swipe-up → Settings (only touch interaction)
    ┌─────────────────────────────────────────┐
    │           Settings (sub-states)          │
    │  Menu → WiFi / Brightness / BG / ...     │
    │  Back → returns to ClockFull             │
    └─────────────────────────────────────────┘

    OVERLAYS (composited on top of current state):
    ┌──────────┐
    │  Alert   │  ← weather/aurora alert: banner on top of any state
    │  Banner  │     auto-dismiss when alert expires
    └──────────┘
```

### Trigger Schedule

| State | Trigger | Cooldown | Display Duration |
|-------|---------|----------|------------------|
| WeatherOverlay | New conditions data (every 15 min) | 30 min | 30s |
| RadarOverlay | New radar PNG + precipitation active | 30 min | 30s |
| Astronomy | Dawn/dusk transition, or midnight | 12 hours | 30s |
| PhotoSlideshow | Periodic (every 30 min) | 30 min | 30s |
| AmbientDashboard | Periodic (every 45 min) | 45 min | 30s |
| SurpriseMessage | HTTP push (immediate) | none | custom |

### C/C++ Bridge

```c
// display_fsm.h — C API for use by C components
void display_fsm_init(void);
void display_fsm_send_event(display_event_t *evt);

typedef enum {
    DISPLAY_EVT_DISPLAY_TIMEOUT,
    DISPLAY_EVT_WEATHER_UPDATE,
    DISPLAY_EVT_FORECAST_UPDATE,
    DISPLAY_EVT_ALERT_RECEIVED,
    DISPLAY_EVT_RADAR_READY,
    DISPLAY_EVT_ASTRO_TRIGGER,
    DISPLAY_EVT_PHOTO_TRIGGER,
    DISPLAY_EVT_AMBIENT_TRIGGER,
    DISPLAY_EVT_GESTURE,
    DISPLAY_EVT_SETTINGS_BACK,
    DISPLAY_EVT_SURPRISE_MESSAGE,
    DISPLAY_EVT_FORCE_STATE,
    DISPLAY_EVT_SCHEDULE_CONFIG,
    DISPLAY_EVT_CLOCK_UPDATE,
} display_event_type_t;

typedef struct {
    display_event_type_t type;
    union { /* event-specific payloads */ };
} display_event_t;
```

---

# Proposal 2: P1-20260302-003 — NWS Weather Service Integration

## Problem

Weather is disabled. The existing Weatherbit component requires an API key, has usage limits, and the service was discontinued for free tier. The NWS weather.gov API is free, unlimited, US-only (sufficient — device is in Michigan), requires no API key (just User-Agent), and provides current conditions, 7-day forecast, weather alerts, and links to radar data.

## Proposed Solution

### New Component: `components/nws/`

```
components/nws/
├── CMakeLists.txt
├── include/
│   └── nws.h               # Public API + data structs
└── src/
    ├── nws_location.c       # /points/{lat},{lon} → grid/station lookup + NVS cache
    ├── nws_conditions.c     # /stations/{id}/observations/latest
    ├── nws_forecast.c       # /gridpoints/{office}/{gridX},{gridY}/forecast
    ├── nws_alerts.c         # /alerts/active?point={lat},{lon}
    └── nws_radar.c          # NOAA ImageServer exportImage
```

### Location Setup (One-Time)

Uses lat/lon from `secrets.h`. The `/points` endpoint returns the nearest NWS office, grid coordinates, and observation station.

Cache in NVS: `nws_office`, `nws_grid_x`, `nws_grid_y`, `nws_station`.
Re-fetch only when lat/lon changes in settings.

### Data Task

Single FreeRTOS task (`nws_task`) with staggered polling:
- conditions: every 15 minutes
- forecast: every 1 hour
- alerts: every 5 minutes
- radar: every 10 minutes
- kp_index: every 3 hours

### Radar Fetch

NOAA ImageServer exportImage endpoint returns transparent RGBA PNG.
Store in SPIRAM buffer. Pass pointer to FSM via event.

---

# Proposal 3: P2-20260302-004 — Weather Display & Animations

## Problem

Weather data exists (from NWS service) but has no visual representation.

## Proposed Solution

### Weather Overlay Layout (1024×600)

```
┌──────────────────────────────────────────────────────────┐
│                                              ┌─────────┐│
│                                              │  12:45  ││
│    ┌──────────────────────┐                  │   PM    ││
│    │   Lottie Condition   │                  │ Mon,    ││
│    │   Animation 300×300  │   72°F           │ Mar 2   ││
│    │                      │   Mostly Cloudy  └─────────┘│
│    │   (plays around the  │   Feels like 68°F           │
│    │    temperature text   │   Wind: NW 12 mph          │
│    │    as anchor point)  │   Humidity: 65%             │
│    └──────────────────────┘                             │
│                                                         │
│  ┌────────┬────────┬────────┬────────┬────────┬────────┬────────┐
│  │  Mon   │  Tue   │  Wed   │  Thu   │  Fri   │  Sat   │  Sun   │
│  │ [icon] │ [icon] │ [icon] │ [icon] │ [icon] │ [icon] │ [icon] │
│  │ 72/58  │ 65/50  │ 60/48  │ 55/45  │ 68/52  │ 70/55  │ 66/50  │
│  └────────┴────────┴────────┴────────┴────────┴────────┴────────┘
└──────────────────────────────────────────────────────────┘
```

### LVGL Native Particle System

Rain/snow via LVGL native objects with `lv_anim` (PPA-accelerated), not Lottie.
30 particles at 2×12px each is trivial for the hardware.

### ~45 Lottie assets total across 6 categories:
- Weather Conditions Day (12)
- Weather Conditions Night (12)
- Astronomy (5)
- Surprise/Celebration (7)
- UI Elements (5)
- Ambient Dashboard (2)

---

# Proposal 4: P2-20260302-005 — Web Control Interface

## Problem

No remote control capability. User wants to push surprise messages, upload animations, and control display state from a phone.

## Proposed Solution

### Architecture

Static web page served from SD card (`A:/www/`). REST API endpoints for display control, weather data, asset management. mDNS for local network discovery (`greenwood-clock.local`).

### New REST Endpoints

| Endpoint | Method | Purpose |
|----------|--------|---------|
| `/www/*` | GET | Serve static files from SD card |
| `/api/display/state` | GET/POST | Query/force display state |
| `/api/display/surprise` | POST | Push surprise layout JSON |
| `/api/display/schedule` | GET/POST | Display schedule config |
| `/api/weather/current` | GET | Current conditions |
| `/api/weather/forecast` | GET | 7-day forecast |
| `/api/weather/alerts` | GET | Active alerts |
| `/api/assets/list` | GET | List animation/background files |
| `/api/assets/upload` | POST | Upload file to SD card |

### Web Page

Single-page vanilla HTML/JS/CSS app, < 50KB total, mobile-responsive.

---

# Detailed Execution Plan

## Phase 0: Prerequisite Closures
- Step 0.1: Close PPA proposal → COMPLETE
- Step 0.2: Create LVGL fork
- Step 0.3: Create proposal files

## Phase 1: Display FSM (11 steps)
- 1.1: Scaffold display_fsm component
- 1.2: FSM task + event queue
- 1.3: ClockWidget (extract from ui.c)
- 1.4: FSM base class
- 1.5: ClockFull state
- 1.6: DisplayScheduler
- 1.7: Migrate Settings screens
- 1.8: Wire gesture events
- 1.9: Wire into main.cpp
- 1.10: Stub states for all display positions
- 1.11: Delete screen_manager dependency

## Phase 2a: NWS Weather (10 steps)
- 2a.1-2a.10: Component skeleton through settings schema update

## Phase 2b: Web Control (6 steps, parallel with 2a)
- 2b.1-2b.6: mDNS through deploy + test

## Phase 3: Weather Animations (16 steps)
- 3.1-3.16: Widget implementations through condition mapping

## Scope: ~32 new files, ~12 modified files, ~5700 LOC
