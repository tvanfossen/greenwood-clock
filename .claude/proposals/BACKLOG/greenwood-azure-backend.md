---
version: 1.0.0
type: proposal
schema_version: 1
id: P1-20260225-002
title: "Greenwood Clock Azure Backend — Weather Proxy and Telemetry"
priority: P1
component: backend
author: Tristan VanFossen
author_email: vanfosst@gmail.com
created: 2026-02-25
updated: 2026-02-25
tags: [azure, dotnet, backend, telemetry, weather, firmware]
completed_date: null
scoped_files:
  - components/weather/
  - components/http_api/http_api.c
  - components/secrets/secrets.h
  - main/main.cpp
depends_on: []
blocks: []
---

# Greenwood Clock Azure Backend — Weather Proxy and Telemetry

## Problem Statement

The ESP32-P4 clock calls Weatherbit directly: API key lives on device, no caching, device IP
can be rate-limited. The device has no telemetry pipeline — uptime, heap, WiFi signal, and
display state are invisible without a serial connection.

Moving weather lookups to an Azure backend solves both: the device gets one stable HTTP
dependency, API keys stay off firmware, and the backend adds hourly caching. Telemetry
ingestion is additive — the clock reports metrics every 5 minutes, stored in Azure SQL,
queryable via REST.

**Deadline: March 8, 2026** (Embla Medical application close — demonstrates .NET/C#, Azure
Functions, API Management, MSSQL, and IoT system integration to the hiring panel).

## Architecture

```
┌──────────────────┐                         ┌───────────────────────────┐
│  ESP32-P4 Clock  │  POST /api/telemetry ─► │  Azure API Management     │
│  (greenwood-clock)│                         │  (Consumption tier)       │
│                  │  GET /api/weather    ◄─  │          │                │
│  Reports:        │                         │  Azure Functions (.NET 8) │
│  - WiFi RSSI     │  every 5 min            │  Entity Framework Core    │
│  - uptime        │                         │  Azure SQL (MSSQL)        │
│  - heap free     │                         │          │                │
│  - display state │                         │  ┌───────▼───────┐       │
│                  │                         │  │  Weatherbit    │       │
│  Consumes:       │                         │  │  (cached 1hr)  │       │
│  - weather data  │                         │  └───────────────┘       │
└──────────────────┘                         └───────────────────────────┘
```

**New repo**: `tvanfossen/greenwood-api` (backend). Firmware changes in this repo.

**Data flow:**
- Clock → Azure: telemetry POST every 5 minutes
- Azure → Weatherbit: cached lookup, refreshed at most hourly
- Clock ← Azure: weather GET (condition data only — icons handled in LVGL, see P1-20260225-001)
- API keys (Weatherbit, device auth) live in Azure App Settings, never on device

## Open Decisions (Resolve Before Starting)

| Decision | Options | Recommendation |
|---|---|---|
| Weather response schema | Pass-through Weatherbit JSON \| normalized subset | **Normalized subset** — backend defines a clean contract, firmware drops unused fields and `icon_url`. Condition code drives LVGL Lottie selection locally. |
| Icon handling | None — dropped | Backend returns condition code (`weather_code` int). Firmware maps to Lottie file (P1-20260225-001). `weather_fetch_icon()` removed entirely from firmware. |
| Location (lat/lon) | Backend App Setting \| device passes as query params | **Backend App Setting** — aligns with stated goal of getting credentials/config off device. `GET /api/weather` takes no params. |
| Azure SQL free tier | Free Offer (new subs only) \| serverless pay-as-go (~$5/mo) | Confirm subscription eligibility before planning around free tier. |
| MCP tool server | Dropped | Not needed for this use case. |

## Acceptance Criteria

**Backend (greenwood-api):**
- [ ] `GET /api/weather` returns normalized condition data for configured location
- [ ] Weather data cached in Azure SQL, refreshed at most once per hour
- [ ] Weatherbit API key lives in Azure App Settings only — not in code, not in git
- [ ] `POST /api/telemetry` accepts clock payload, persists to Azure SQL
- [ ] `GET /api/telemetry/latest` returns most recent reading
- [ ] `GET /api/telemetry?from={iso}&to={iso}` returns historical range
- [ ] `GET /api/telemetry/stats` returns min/max/avg over configurable period
- [ ] `GET /api/health` returns 200 with SQL connectivity check
- [ ] Swagger/OpenAPI spec auto-generated and browsable
- [ ] API key auth validated at API Management gateway level
- [ ] Rate limiting policy on all device-facing routes (100 req/min)
- [ ] EF Core code-first migrations — schema managed in code, applied in CI
- [ ] GitHub Actions: build → test → deploy on push to main
- [ ] xUnit tests cover weather cache logic, telemetry validation, query ranges

**Firmware (this repo):**
- [ ] Direct Weatherbit call removed from `components/weather/`
- [ ] `weather_fetch_icon()` removed — icons handled by LVGL Lottie (P1-20260225-001)
- [ ] `WEATHER_API_KEY` removed from `components/secrets/secrets.h`
- [ ] `GET /api/weather` client added — pulls from Azure backend, maps `weather_code` to UI
- [ ] `POST /api/telemetry` client added — reports every 5 minutes via dedicated task
- [ ] Azure endpoint URL and API key in NVS settings (not baked into firmware — must survive endpoint URL changes without reflash)
- [ ] Retry on failure with exponential backoff, max 3 attempts; failure logs error, clock continues

## Normalized Weather Response Schema

Backend returns this to the device. Drops unused fields and icon URL entirely.

```json
{
  "temp_c": 12.4,
  "feels_like_c": 10.1,
  "humidity": 72,
  "wind_spd_ms": 4.2,
  "wind_dir_deg": 270,
  "wind_cdir": "W",
  "description": "Light rain",
  "weather_code": 500,
  "city_name": "Grand Rapids",
  "ob_time": "2026-02-25 14:00",
  "sunrise": "07:22",
  "sunset": "18:45",
  "cached_at": "2026-02-25T14:05:00Z"
}
```

`weather_code` is the Weatherbit condition code integer. Firmware maps it to a Lottie
animation file path (e.g. `"A:/lottie/rain.json"`). `cached_at` lets the device log cache age.

Firmware `weather_data_t` will be trimmed to match — remove `icon_url`, `solar_*`,
`elev_angle`, `h_angle`, `station`, `dew_point_c`, `precip_mm`, `pressure_mb`, `clouds`,
`visibility_km`, `uv_index`, `aqi` if not rendered on screen. Audit `ui.c` first.

## Implementation Plan

### Phase 1: Azure Functions + Weather Proxy

Stand up the Functions project and implement weather caching.

- Azure Functions project (.NET 8, isolated worker model)
- `GET /api/weather` — returns normalized response schema defined above
- Weatherbit current-conditions endpoint: `https://api.weatherbit.io/v2.0/current?lat=X&lon=Y&key=...`
- Location (lat/lon) in Azure App Settings — not in code
- Cache in Azure SQL: store raw Weatherbit JSON + `fetched_at` timestamp
- Cache policy: serve cached if `fetched_at` < 1 hour ago, else refresh
- Stale-on-error: if Weatherbit fails during refresh, return last cached data with `cached_at` in response
- Swagger/OpenAPI via Swashbuckle
- EF Core with Azure SQL — code-first, migration for `weather_cache` table
- Header-based API key auth (validated properly at APIM in Phase 3)

### Phase 2: Telemetry Ingestion + Query

Add telemetry endpoints alongside weather proxy.

- `POST /api/telemetry` — validate, store
- `GET /api/telemetry/latest` — most recent row
- `GET /api/telemetry?from={iso}&to={iso}` — range query, EF Core parameterized
- `GET /api/telemetry/stats` — min/max/avg per field over period
- `GET /api/health` — SQL ping, returns 200/503
- EF Core migration: `telemetry` table
- Input validation: required fields, sane ranges (RSSI -120 to 0, heap > 0, etc.)

**Telemetry payload from clock:**

```json
{
  "device_id": "greenwood-clock",
  "timestamp_utc": "2026-02-25T12:00:00Z",
  "wifi_rssi_dbm": -58,
  "uptime_sec": 86400,
  "heap_free_bytes": 204800,
  "display_state": "clock"
}
```

**Valid `display_state` values**: `"clock"`, `"settings"`, `"weather"`, `"ota"`, `"splash"`.
Backend rejects unknown values with 400. Firmware must use this exact set.

### Phase 3: Azure API Management

Put APIM in front of the Functions (Consumption tier — $3.50/million calls, ~$0 at this volume).

- Import Functions API via OpenAPI spec
- Rate limiting policy: 100 req/min per subscription key
- API key validation at gateway level
- Versioning: `/api/v1/` prefix on all routes
- Functions not exposed directly — APIM is the only entry point
- **Note**: APIM takes 30-45 minutes to provision. Create the resource before starting Phase 3 code.

### Phase 4: Firmware Changes (this repo)

Replace on-device weather call, remove icon fetch, add telemetry.

**Weather component:**
- Remove `weather_build_url()`, `weather_check_preconditions()`, `WEATHER_API_KEY` usage
- Remove `weather_fetch_icon()` entirely — Lottie handles icons (P1-20260225-001)
- Replace `weather_fetch()` with Azure backend call: `GET /api/v1/weather`
- Update `weather_data_t`: remove `icon_url`, solar fields, and any fields not rendered in `ui.c`
- Add `int weather_code` and `char cached_at[25]` fields
- Map `weather_code` → Lottie file path in a lookup table

**Telemetry component (new):**
- `components/telemetry/telemetry.c` — dedicated FreeRTOS task (not a timer callback)
- Task loop: collect metrics → POST to Azure → wait 5 minutes → repeat
- Collects: `esp_wifi_sta_get_ap_info()` for RSSI, `esp_get_free_heap_size()`, `esp_timer_get_time()/1e6` for uptime, display state enum (string form)
- If WiFi disconnected at collection time: skip POST, log warning, continue
- Retry: up to 3 attempts with 2s backoff, then drop (fire-and-forget — telemetry loss acceptable)
- Task stack: 4096 bytes, priority 2 (below LVGL, above idle)

**Settings:**
- Add `azure_endpoint[128]` and `azure_api_key[64]` to `clock_settings_t`
- Expose in settings UI or via `POST /settings` HTTP API
- Default: empty (telemetry and Azure weather silently disabled until configured)

### Phase 5: CI/CD + Tests

- GitHub Actions: `dotnet build` → `dotnet test` → deploy to Azure Functions on push to main
- Deploy via `azure/functions-action` (publish profile in GitHub Secret)
- EF Core migrations applied as part of deploy (`dotnet ef database update`)
- xUnit tests:
  - Weather cache: fresh hit, stale refresh, Weatherbit failure → serve stale
  - Telemetry validation: missing fields, out-of-range RSSI, unknown display_state
  - Query range: from/to filtering, stats aggregation correctness
- Integration test: POST telemetry → GET latest → assert round-trip fields match
- README: architecture diagram, local dev setup (`func start`, LocalDB or Docker SQL)

## Risks & Considerations

- **Azure SQL free tier eligibility**: Free Offer (100K vCore-seconds/month) is one-per-subscription for accounts created after Nov 2023. Confirm before planning. If unavailable, serverless minimum is ~$5/month.
- **APIM Consumption tier cost**: ~$3.50/million calls. At ~9,000 calls/month this is effectively $0, but it's not free — the proposal previously said it was.
- **Azure SQL serverless cold-start**: Auto-pause triggers after 1 hour of inactivity. First query after pause takes ~5s. 5-minute telemetry interval keeps it warm during active hours; overnight pause is expected. Weather GET at 6am may be slow first-call.
- **Stale weather on provider failure**: Backend serves last cached data. Device should log `cached_at` age so stale data is visible in debug logs.
- **Weatherbit field schema**: Current firmware parses `{"data":[{...}]}` wrapper. Backend normalizes this to the flat schema defined above — firmware JSON parsing changes required. Audit `ui.c` before finalizing which fields to drop from `weather_data_t`.
- **NVS for Azure credentials**: Azure endpoint URL and API key must survive firmware updates without reconfiguration. Using NVS (same mechanism as WiFi credentials) means a device reflash doesn't clear them, but `idf.py erase-flash` does.
- **Telemetry task LVGL lock discipline**: Telemetry task must never acquire LVGL port lock. Reading `display_state` must go through an atomic/mutex-protected variable set by the UI task, not by calling LVGL functions directly.

## Implementation Log

*Entries added as work progresses*

## References

- `dotnet_project_proposal.md` — original architecture proposal and JD alignment table
- `components/weather/weather.c` — existing Weatherbit client to be replaced
- `components/weather/weather.h` — `weather_data_t` struct to be trimmed
- `components/secrets/secrets.h` — `WEATHER_API_KEY` to be removed
- `components/settings/settings.h` — `clock_settings_t` to be extended with Azure fields
- Azure Functions .NET 8 isolated worker documentation
- Azure API Management Consumption tier limits and pricing
- Weatherbit API v2.0 current-conditions endpoint and condition code table
- P1-20260225-001 (PPA accelerator) — prerequisite for Lottie weather animations
