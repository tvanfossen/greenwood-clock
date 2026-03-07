---
version: 1.0.0
type: proposal
schema_version: 1
id: P2-20260302-004
title: "Weather Display & Animations"
priority: P2
component: display_fsm
author: Tristan VanFossen
author_email: vanfosst@gmail.com
created: 2026-03-02
updated: 2026-03-04
tags: [weather, display, lottie, animations, particles, radar, astronomy]
completed_date: null
scoped_files:
  - components/display_fsm/src/state_weather.cpp
  - components/display_fsm/src/state_radar.cpp
  - components/display_fsm/src/state_astronomy.cpp
  - components/display_fsm/src/state_photos.cpp
  - components/display_fsm/src/state_ambient.cpp
  - components/display_fsm/src/state_surprise.cpp
  - components/display_fsm/src/weather_widgets.c
  - components/display_fsm/src/particle_effects.c
  - components/display_fsm/src/radar_view.c
  - components/display_fsm/src/astro_calc.c
  - components/nws/src/nws_condition_map.c
depends_on:
  - P1-20260302-002
  - P1-20260302-003
  - P2-20260302-005
blocks: []
---

# Weather Display & Animations

## Problem Statement

Weather data exists (from NWS) but has no visual representation. The display needs weather
overlays, condition animations, radar compositing, alert banners, forecast views, astronomy
screen, photo slideshow, and ambient dashboard.

## Proposed Solution

Replace stub states (from Phase 1) with full implementations:
- WeatherCard + ForecastStrip widgets with condition Lottie animations
- LVGL native particle system (rain/snow/flash — PPA accelerated)
- RadarView with map background + transparent radar PNG overlay
- AlertBanner with severity colors and scrolling text
- Astronomy screen with moon phase (Meeus algorithm), sunrise/sunset, aurora
- PhotoSlideshow cycling images from SD card
- AmbientDashboard with aggregated info panel
- SurpriseMessage with JSON layout DSL parser
- ~45 Lottie animation assets across 6 categories

See master plan for layouts, widget APIs, particle configs, and Lottie manifest.

## Acceptance Criteria

- [ ] Weather overlay shows current conditions with animated Lottie
- [ ] 7-day forecast strip with condition icons
- [ ] Rain/snow particles render naturally
- [ ] Radar composites correctly over map
- [ ] Alert banner with correct severity colors
- [ ] Astronomy screen with computed moon phase
- [ ] Photo slideshow from SD card
- [ ] Ambient dashboard with sunrise/sunset/aurora
- [ ] Surprise message renders from JSON DSL
- [ ] No watchdog triggers under weather + particle load
