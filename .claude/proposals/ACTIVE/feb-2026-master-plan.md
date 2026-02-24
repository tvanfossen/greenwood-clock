---
proposal_id: "feb-2026-master-plan"
title: "Greenwood Clock — February 2026 Master Plan"
created: "2026-02-23"
updated: "2026-02-23"
status: "ACTIVE"
priority: "high"
owner: "architect"
draft: true
---

> **DRAFT** — This plan is a starting point based on the Feb 2026 repo review.
> Additional items should be added as further review uncovers gaps.
> @architect: review and annotate with APPROVED / QUESTION / BLOCKED markers before implementation begins.

# Greenwood Clock — February 2026 Master Plan

Two parallel workstreams. May be executed simultaneously. Phase 1 gates nothing in Phase 2
from a dependency standpoint, but Phase 1 will touch most of the codebase and will produce
refactored, cleaner code that Phase 2 builds on top of.

---

## Phase 1 — Pre-commit Quality Gates + Full Codebase Remediation

### Goal

Establish enforced code quality gates via pre-commit, then bring the entire codebase into
compliance. Remediation is not just cleanup — it is an opportunity to simultaneously:

- **Improve observability**: Every refactored function should have better logging than it had
  before. State transitions logged. Errors logged with full context before propagation.
- **Add Doxygen**: Every refactored function gets a Doxygen comment block. Don't add comments
  to code that isn't touched; do add them to everything that is.

### Pre-commit Hooks

`.pre-commit-config.yaml` at repo root:

```yaml
repos:
  - repo: https://github.com/brandon-arrendondo/knots
    rev: 1.0.0
    hooks:
      - id: knots
        args:
          - --mccabe-threshold=15
          - --cognitive-threshold=15
        exclude: ^(managed_components/|components/lvgl/|components/cjson/)

  - repo: https://github.com/astral-sh/ruff-pre-commit
    rev: v0.4.4
    hooks:
      - id: ruff
        args: [--fix]
      - id: ruff-format
```

**Scope**: `components/` (project-owned only). Exclude `managed_components/`, `components/lvgl/`,
`components/cjson/` — these are third-party.

**Target tools**:
- `knots` — C/C++ cognitive and cyclomatic complexity (threshold: 15)
- `ruff` — Python linting and formatting (for `tools/`)

### Remediation Approach

For every function that fails `knots`:

1. Decompose into smaller, focused functions (each one responsibility)
2. While decomposing, audit and improve logging:
   - Every `esp_err_t` return path must log the error before returning
   - State transitions (Wi-Fi connect, SNTP sync, OTA states) must log entry and exit
   - Heap size logged at key boundaries (before/after large allocations)
3. Add Doxygen block to every function touched:
   ```c
   /**
    * @brief Brief description.
    *
    * @param name Description
    * @return ESP_OK on success, ESP_ERR_x on failure
    */
   ```

### Expected High-Complexity Targets

Based on code review, the following are most likely to exceed thresholds:

| File | Reason |
|---|---|
| `components/ui/screen_manager.c` | Large settings screens, keyboard handling |
| `components/ui/ui.c` | Weather update callback, timer logic |
| `components/network/network.c` | Wi-Fi event handler, SNTP retry loop |
| `components/weather/weather.c` | HTTP parsing, JSON extraction |
| `components/ota/ota.c` | OTA state machine |
| `main/main.cpp` | `app_main()` boot sequence |

### Acceptance Criteria

- [ ] `.pre-commit-config.yaml` committed and passing
- [ ] `knots` reports zero violations across all project-owned C/C++ files
- [ ] `ruff` reports zero violations across all Python files in `tools/`
- [ ] Every refactored function has a Doxygen `@brief` comment
- [ ] Every refactored function has improved logging vs its predecessor
- [ ] `doxygen Doxyfile` runs cleanly with no warnings on touched files
- [ ] Build still passes after all changes

---

## Phase 1b — Bug Fix: HTTP Log Download Returns No Data

`GET /api/logs/download` in `components/http_api/http_api.c` returns no data or fails
silently. Root cause unknown — candidates: file not flushed before read, file locking
conflict with debug_log writer, chunked transfer not implemented, missing content headers.

This is the preferred log access workflow (`curl http://<device-ip>/api/logs/download`).
No on-device log viewer is needed.

**Fix approach**: Instrument the handler, identify failure point, fix chunked transfer,
ensure `debug_log` flushes before read.

**Acceptance criteria**:
- [ ] `GET /api/logs/download` returns complete log file contents
- [ ] `curl http://<device-ip>/api/logs/download` works reliably
- [ ] Returns error JSON if SD card not mounted or log file missing

---

## Phase 2 — Push OTA Architecture

### Goal

Desktop-initiated push OTA. No touchscreen interaction required. No on-device progress
UI — the existing OTA settings screen is to be **removed** from `screen_manager.c` as
part of this phase.

Progress is tracked entirely in the terminal via `ota_push.py` polling a device status
endpoint. Verbose, observable output preferred.

### Flow

```
Dev Desktop                              Greenwood Clock (port 80)
───────────                              ─────────────────────────
idf.py build
python tools/ota_push.py ──POST /ota──▶  Validate bearer token
                                         Spawn OTA task
                         ◀── 200 ──────  {"status": "accepted"}
         poll loop:
         GET /ota/status ──────────────▶  Returns current OTA state
                         ◀── 200 ──────  {"status": "receiving", "progress": 46}
                         ◀── 200 ──────  {"status": "flashing"}
                         ◀── 200 ──────  {"status": "rebooting"}
         (device reboots — poll ends)
```

### Components

#### 2a. Device: `POST /ota` + `GET /ota/status` in `http_api`

**`POST /ota`** — trigger flash:
```
POST /ota
Authorization: Bearer <OTA_API_TOKEN>
Content-Type: application/octet-stream
Body: raw firmware binary
```
- Validates bearer token against `OTA_API_TOKEN` from `secrets.h`
- Spawns OTA task (must not block httpd handler thread)
- Returns `{"status": "accepted"}` immediately
- Returns `{"error": "unauthorized"}` on bad/missing token
- Returns `{"error": "busy"}` if OTA already in progress

**`GET /ota/status`** — poll state machine:
```json
{"status": "idle"}
{"status": "receiving",  "progress": 46, "bytes_received": 1234567, "bytes_total": 3800000}
{"status": "validating"}
{"status": "flashing"}
{"status": "rebooting"}
{"status": "error", "message": "..."}
```
- No auth required (read-only, no sensitive data)
- State persisted in a static struct in `http_api.c`, updated by OTA task callbacks

#### 2b. Desktop: `tools/ota_push.py`

```python
# tools/ota_push.py
# Usage:
#   python tools/ota_push.py                        # mDNS auto-discover
#   python tools/ota_push.py --host 192.168.1.50    # explicit IP
#   python tools/ota_push.py --firmware custom.bin  # custom binary
```

- Reads `OTA_API_TOKEN` from environment variable or `.env` file
- Discovers device via mDNS (`greenwood-clock.local`) if no `--host`
- POSTs binary to `POST /ota` with streaming upload + upload progress bar in terminal
- Polls `GET /ota/status` every 1s, printing each state transition verbosely:
  ```
  [ota] Connecting to greenwood-clock.local...
  [ota] Uploading firmware (3.8 MB)... ████████████████░░░░ 78%
  [ota] Upload complete. Waiting for device...
  [ota] status: receiving  (1234567 / 3800000 bytes)
  [ota] status: validating
  [ota] status: flashing
  [ota] status: rebooting
  [ota] Device rebooting. Done.
  ```
- Exits 0 on clean reboot, 1 on error or timeout (60s max poll)

#### 2c. Device: Remove OTA settings screen

The existing `create_ota_settings()` screen and `SCREEN_OTA_SETTINGS` in
`components/ui/screen_manager.c` should be removed. OTA is now fully desktop-driven.
The Settings menu entry for "Software Update" is also removed.

#### 2d. Token configuration

`components/secrets/secrets.h` (gitignored):
```c
#define OTA_API_TOKEN "your-token-here"
```

Desktop `.env` (gitignored):
```
OTA_API_TOKEN=your-token-here
```

### Acceptance Criteria

- [ ] `POST /ota` with valid token returns `{"status": "accepted"}` and begins flash
- [ ] `POST /ota` without token returns HTTP 401
- [ ] `GET /ota/status` returns correct state at each OTA phase
- [ ] `tools/ota_push.py --host <ip>` prints verbose progress and device reboots
- [ ] mDNS auto-discovery works
- [ ] Rollback works if new firmware fails to mark valid
- [ ] OTA settings screen removed from `screen_manager.c`
- [ ] `ruff` passes on `ota_push.py`
- [ ] Doxygen on all new/modified `http_api` handlers
- [ ] Every OTA state transition logged on device at INFO level

---

## Sequencing

```
Phase 1 (pre-commit + remediation)
    ├── .pre-commit-config.yaml already committed
    ├── Run knots across codebase → enumerate violations
    ├── Refactor violations (observability + Doxygen as you go)
    └── All checks green

Phase 1b (log download bug fix) — can run in parallel with Phase 1
    └── Fix GET /api/logs/download

Phase 2 (push OTA) — can start in parallel with Phase 1 refactoring
    ├── Add POST /ota + GET /ota/status to http_api
    ├── Remove OTA settings screen from screen_manager.c
    ├── Write ota_push.py
    ├── Configure shared token
    └── End-to-end test
```

Phase 1 will produce cleaner `ota.c`, `http_api.c`, and `screen_manager.c`.
If starting simultaneously, coordinate to avoid conflicts on those files.

---

## Out of Scope (This Plan)

- PPA hardware acceleration fix (tracked in `docs/PPA_STATUS.md`)
- Secure HTTPS OTA (future enhancement to Phase 2)
- Lottie animation verification
