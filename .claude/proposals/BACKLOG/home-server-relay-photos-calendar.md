---
version: 1.0.0
type: proposal
schema_version: 1
id: P2-20260701-001
title: "Home-Server Relay — Remote Photo Add + Family Calendar"
priority: P2
component: backend
author: Tristan VanFossen
author_email: vanfosst@gmail.com
created: 2026-07-01
updated: 2026-07-01
tags: [home-server, relay, photos, calendar, ical, remote, sync, self-hosted, skylight]
completed_date: null
scoped_files:
  - components/http_api/http_api.c
  - components/sdcard/
  - components/display_fsm/src/state_photos.cpp
  - components/display_fsm/src/display_scheduler.cpp
  - main/main.cpp
depends_on: []
blocks: []
---

# Home-Server Relay — Remote Photo Add + Family Calendar

## Problem Statement

The two features families actually buy a Skylight for are **remote photo send** ("email a
photo to the frame from anywhere") and a **shared calendar**. This device today only
supports **local-LAN** photo upload and has no calendar. We explicitly do NOT want managed
cloud storage.

We already have a **home server and a domain**. That changes the cost/benefit: instead of
cramming JPEG decode, MIME/IMAP parsing, and iCal/RRULE handling onto the fragile ESP32‑P4
(where a crash before the OTA window can brick the device), the heavy lifting moves to the
home server, and the device just polls **pre-digested** content — which it already does well
(NWS-style poll loops, PNG decode, SD-card photo slideshow).

This is the **self-hosted counterpart** to `greenwood-azure-backend` (P1-20260225-002): same
"thin backend" idea, but on our own box with no cloud bill and no managed storage.

## Proposed Solution

Home server acts as a **thin relay + transcoder**. The device makes **outbound HTTPS only**
to the server — nothing inbound to the device, keeping the brick-prone unit off the public
surface.

```
family ──(web PWA / email / Telegram, swappable)──▶  home server  ──▶  device polls
                                                    (ingest → transcode → cache → serve)   (outbound HTTPS)
```

**Division of labor** (the point of the design — hard parts leave the firmware):

| Hard part | Home server | Device |
|---|---|---|
| JPEG (phone) decode + downscale to 1024×600 | Pillow/ImageMagick | GET ready PNG (already supported) |
| MIME / IMAP (if email ingest) | Python stdlib | nothing |
| iCal / **RRULE** / timezones | `icalendar` + `recurring-ical-events` | GET clean agenda JSON |
| Internet-facing TLS / auth / rate-limit | Caddy/nginx + Let's Encrypt (domain owned) | reuse existing bearer token |
| Family upload UX | web PWA / email / Telegram | unaffected by firmware |

**Device contract** (stable, minimal): `GET /clock/photos/manifest`, `GET /clock/photos/<id>.png`,
`GET /clock/agenda.json`. Ingest method can change server-side without a firmware update.

Delivery format = **PNG** (portable; device already decodes and caches `.r565`). Only pre-bake
`.r565` server-side later if we want to skip even PNG decode.

## Acceptance Criteria

- [ ] A family member, off the home network, can add a photo that appears in the device slideshow within one poll interval.
- [ ] No managed/paid cloud storage — all storage on the home server + device SD.
- [ ] Device makes **outbound-only** connections; no inbound ports opened to the device.
- [ ] Ingest endpoint is TLS + authenticated + rate-limited on the server.
- [ ] Photos arrive as display-ready 1024×600 PNG (no on-device JPEG decode required).
- [ ] Device degrades gracefully (keeps showing cached content) when the server is unreachable.
- [ ] A calendar/agenda screen renders upcoming events from server-provided JSON (no on-device iCal parsing).
- [ ] Recurring events (at least single + simple daily/weekly RRULE) render with correct local times.

## Implementation Plan

### Phase 0: Prerequisites
- ESP32‑C6 Wi-Fi coprocessor reflash (ESP-Hosted 2.12.0) — remote features are unreliable until stable Wi-Fi. See `docs/HARDWARE.md`.
- Note: on-device JPEG decode is **NOT** required — the server transcodes to PNG. (Removes the biggest firmware cost.)

### Phase 1: Server relay (photos)
- Ingest surface behind Caddy + Let's Encrypt: start with a simple auth'd web upload form/PWA.
- Transcode pipeline: accept JPEG/PNG/HEIC → EXIF-rotate → downscale/letterbox to 1024×600 PNG → store with an id + optional caption/sender.
- Serve `photos/manifest` (ids + hashes) and `photos/<id>.png`.

### Phase 2: Device photo sync
- Add a sync poll task (mirror NWS cadence): pull manifest → diff against `/sdcard/photos` → download new PNGs, remove deleted → existing slideshow picks them up (it already scans that dir; `.r565` cache regenerates).
- Auth with the existing OTA-token pattern; validate content-length/type before writing SD.

### Phase 3: Calendar (server + device)
- Server subscribes to one ICS URL per person (Google/Apple/Outlook secret links), parses with `recurring-ical-events`, exposes `agenda.json` (next N events: title, start/end local, all-day flag, person/color).
- Device: new FSM state renders the agenda (month or agenda view). Per-person color from the JSON. Zero parsing on-device.

### Phase 4 (optional, later): richer ingest
- Add email→relay and/or Telegram-bot→relay as alternative front doors to the same pipeline — server-side only, no firmware change.

## Risks & Considerations

- **Availability coupling** — new content depends on the server being up. Mitigate with SD-card cache; never gate safety-critical behavior on the server.
- **Internet exposure** — the ingest endpoint is the one genuine public attack surface. Requires TLS + auth + rate-limiting + upload size/type validation. Standard reverse-proxy hygiene (Caddy ~10 lines).
- **"No cloud storage" definition** — satisfied as "our own box," not a paid cloud. Make that explicit to stakeholders.
- **Wi-Fi coprocessor dependency** — everything here rides on the C6 reflash landing first.
- **RRULE/timezone edge cases** — handled server-side by mature libs, but exotic recurrences/DST still warrant test cases.
- **Push vs pull** — pull chosen deliberately (device outbound-only, firewall-simple). A LAN push to the existing `/api/assets/upload` is possible but re-exposes the device.
- **Server as SPOF/maintenance** — it's now infrastructure the clock depends on; back up the photo store and calendar config.

## Implementation Log

_None — captured as backlog, no active work._

## References

- `greenwood-azure-backend` (P1-20260225-002) — cloud counterpart; this is the self-hosted alternative.
- `web-control` (P2-20260302-005) — existing local HTTP control surface / `/api/assets/upload`.
- Existing device pieces to reuse: `components/http_api/` (file upload, bearer token), `components/sdcard/`, photos slideshow in `state_photos.cpp` + `display_scheduler.cpp`, NWS poll-task cadence in `components/nws/`.
- Research discussion (this session): Skylight feature gap, remote-ingest options (Telegram/email/tunnel), read-only ICS calendar approach.
