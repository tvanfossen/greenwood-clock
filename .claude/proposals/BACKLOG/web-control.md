---
version: 1.0.0
type: proposal
schema_version: 1
id: P2-20260302-005
title: "Web Control Interface"
priority: P2
component: http_api
author: Tristan VanFossen
author_email: vanfosst@gmail.com
created: 2026-03-02
updated: 2026-03-04
tags: [web, control, http, mdns, remote, ui]
completed_date: null
scoped_files:
  - components/http_api/http_api.c
  - components/http_api/http_api.h
  - sdcard/www/
  - main/main.cpp
  - sdkconfig.defaults
depends_on:
  - P1-20260302-002
blocks:
  - P2-20260302-004
---

# Web Control Interface

## Problem Statement

No remote control capability. Changing display content requires reflashing firmware or
pushing files via CLI tools. User wants to push surprise messages, upload animations,
and control display state from a phone.

## Proposed Solution

- mDNS advertisement (`greenwood-clock.local`)
- Static file server from SD card (`A:/www/`)
- REST API endpoints for display control, weather data, asset management
- Single-page vanilla HTML/JS/CSS control app (< 50KB)

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
| `/api/assets/list` | GET | List files |
| `/api/assets/upload` | POST | Upload file to SD card |

See master plan for full architecture, web page design, and surprise message flow.

## Acceptance Criteria

- [ ] Control page loads on phone browser
- [ ] Display state buttons change device state
- [ ] Surprise message displays on device
- [ ] Weather data shown on control page
- [ ] Asset upload writes to SD card
- [ ] mDNS discovery works
- [ ] Page size < 50KB
- [ ] No crash on rapid requests
