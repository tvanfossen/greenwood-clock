---
version: 1.0.0
type: proposal
schema_version: 1
id: P1-20260302-003
title: "NWS Weather Service Integration"
priority: P1
component: nws
author: Tristan VanFossen
author_email: vanfosst@gmail.com
created: 2026-03-02
updated: 2026-03-04
tags: [weather, nws, api, http, forecast, alerts, radar]
completed_date: null
scoped_files:
  - components/nws/
  - components/settings/settings.h
  - main/main.cpp
  - sdkconfig.defaults
depends_on:
  - P1-20260302-002
blocks:
  - P2-20260302-004
---

# NWS Weather Service Integration

## Problem Statement

Weather is disabled. The existing Weatherbit component requires an API key and has been
discontinued for free tier. The NWS weather.gov API is free, unlimited, US-only (sufficient
for Michigan), requires no API key (just User-Agent).

## Proposed Solution

New `components/nws/` component with:
- Location resolver (/points endpoint + NVS cache)
- Current conditions (/stations/{id}/observations/latest)
- 7-day forecast (/gridpoints/{office}/{gridX},{gridY}/forecast)
- Active alerts (/alerts/active?point={lat},{lon})
- Radar PNG (NOAA ImageServer)
- Kp/aurora index (SWPC)

Single FreeRTOS task with staggered polling. Events delivered to display FSM queue.

See master plan for full data structures, polling intervals, and HTTP details.

## Acceptance Criteria

- [ ] /points lookup succeeds and caches grid/station in NVS
- [ ] Current conditions fetch returns valid data
- [ ] 7-day forecast returns 14 periods
- [ ] Alert fetch returns active alerts
- [ ] Radar fetch returns valid PNG
- [ ] Weather task polls without memory leak
- [ ] Events delivered to FSM queue
- [ ] Graceful handling of network errors
- [ ] User-Agent header on all requests
