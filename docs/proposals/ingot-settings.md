# Proposal: ingot as the Greenwood Clock settings store

| | |
|---|---|
| Status | PROPOSAL, no code changes |
| ingot reviewed | `brandon-arrendondo/ingot` v1.2.0 (`Cargo.toml`), HEAD `43d0a8e` (2026-11-03) |
| Firmware | ESP32-P4, ESP-IDF v5.5.6, branch `feat/morph-snow-textshadow-2026-11-17` |
| Scope | `components/settings/`, every `settings_load`/`settings_save` call site |
| Migration | Not required up front (see Phase plan) |

ingot paths below are relative to the ingot repo. Firmware paths are relative to this repo.

---

## 1. Current state

```
            +-------------------- NVS ns "clock_cfg" -------------------+
            |  "settings"  = clock_settings_t blob (~520 B, version 6)  |
            |  "clock_anim"= u8   (separate so the blob is untouched)    |
            +-----------------------------------------------------------+
                 ^ load whole blob / save whole blob, no lock
   +-------------+--------------+----------------+-----------------+
 httpd task        LVGL cb (fsm/lvgl)    nws task         fsm_task / states
 http_api.c        state_settings.cpp   nws_location.c   display_fsm_task.cpp, state_*.cpp
```

### Pain points

| # | Problem | Evidence |
|---|---|---|
| P1 | Any schema change wipes everything, Wi-Fi included. Size check and version check both fall back to full defaults. | `components/settings/settings.c:72-77` (size == sizeof), `:88-93` (version == `SETTINGS_VERSION`), `:134-142` (falls back to defaults); `settings.h:19` |
| P2 | Because of P1, new prefs get hand-written separate NVS keys, and each one repeats the open/get/validate/commit boilerplate. | `components/settings/clock_anim_pref.h` (`@brief` explains why), `clock_anim_pref.c:36-82`; REQ-SET-002 in `docs/requirements.yaml:136` |
| P3 | Lost updates. Every writer does load, modify, save on its own task with no lock. Two concurrent writers can drop each other's field. | httpd: `http_api.c:2090` (schedule), `:2367` (settings); nws task: `components/nws/src/nws_location.c:60-72`; LVGL callbacks: `state_settings.cpp:478-491` (Wi-Fi), `:577-604` (brightness), `:684-694` (bg), `:808-818` (colour) |
| P4 | Flash wear and latency. The brightness slider rewrites the whole blob on every `LV_EVENT_VALUE_CHANGED` while dragging. | `state_settings.cpp:592-603`, `:636` |
| P5 | Readers re-read NVS. There are about 20 `settings_load` call sites, each a ~520 B NVS blob read, several on the FSM hot path (state `entry()`). | `state_radar.cpp:48`, `state_ambient.cpp:259`, `state_astronomy.cpp:156`, `display_scheduler.cpp:124`, `display_fsm_task.cpp:591/871/1087` |
| P6 | Live apply is hand-wired per field. A `settings_changes_t` bitmask and explicit `display_fsm_refresh_*` calls decide what to apply. Nothing generic says that key X changed. | `http_api.c:2342-2357`; `display_fsm_task.cpp:1000` (REFRESH_BG enqueue), `:1062`, `:1082` |
| P7 | No range validation at the store. Only `clock_anim` validates (`clock_anim_pref.c:49`). Brightness, durations and so on are trusted from the HTTP JSON. | `http_api.c` merge helpers |

---

## 2. What ingot gives us

- Static, typed key-value store. Defaults are compiled in. Lookup uses a perfect hash (`README.md`).
- `DataModel_Set*ByKey` and `DataModel_Get*ByKey`, named helpers, read-only keys, and per-key `thread_safe` (`templates/dm.c`, `docs/generated-api.rst`).
- Unchanged-value suppression for integral sets: `DM_RETURN_CODE_SET_VALUE_UNCHANGED` at `templates/dm.c:79,92`.
- A value-change callback `DataModel_Event_Callback(uint32_t key)` (`templates/dm.h:43`). The `--emit-tinyfsm` flag adds the following, which maps directly onto P6:
  - `FSM_EVENT_*` structs
  - `send_tinyfsm_event_by_key()`
  - `dm_key_has_event()`
- An ESP target with a FreeRTOS mutex (`src/codegen/target.rs:57-73`).
- Generated Unity tests for defaults, round-trip and read-only behaviour (`templates/test_dm.c`). These can run under our existing `inv test-host` harness.
- **Not usable as-is for us:** persistence. It is one packed struct written with `fopen`/`fwrite` to a file, `/sdcard/dm.bin` by default (`templates/persistence_storage.c:11-33,88-147`, `persistence_storage.h:14`). The load is rejected unless `magic == sizeof(struct) ^ num_keys` (`persistence_storage.c:107-110`). So it has the same all-or-nothing failure as P1, and needs a filesystem that does not exist at our settings boot stage (see G1).

### Draft `settings.toml`

The defaults mirror `settings.c:19-47`. The location and OTA URL defaults are deliberately placeholders here. The real values stay out of docs.

```toml
[meta]
id = "gwc"
version = "1.0.0"
doc = "Greenwood Clock persistent settings"

[enums.clock_anim]
[enums.clock_anim.values]
hummingbird = 0
snowfall = 1
none = 2
leaves = 3

[[classes]]
id = "wifi"
    [[classes.keys]]
    id = "ssid"
    type = "string"
    max_size = 33          # 32-byte SSID + NUL (today: char[32] = 31 usable)
    persistent = true
    thread_safe = true
    [[classes.keys]]
    id = "password"
    type = "string"
    max_size = 65          # 64-hex PSK + NUL (today: char[64] = 63 usable)
    persistent = true
    thread_safe = true     # + `secret = true` once G6 lands
    [[classes.keys]]
    id = "hostname"
    type = "string"
    max_size = 32
    default = "greenwood-clock"
    persistent = true
    [[classes.keys]]
    id = "configured"
    type = "bool"
    persistent = true

[[classes]]
id = "location"
    [[classes.keys]]
    id = "lat_e6"
    type = "int32"         # no float type (G5); micro-degrees
    persistent = true
    [[classes.keys]]
    id = "lon_e6"
    type = "int32"
    persistent = true
    [[classes.keys]]
    id = "timezone"
    type = "string"
    max_size = 64
    default = "EST5EDT,M3.2.0/2,M11.1.0/2"
    persistent = true
    event = true

[[classes]]
id = "display"
    [[classes.keys]]
    id = "brightness"
    type = "uint8"
    default = 50
    persistent = true
    event = true           # min = 0, max = 100 once G7 lands
    [[classes.keys]]
    id = "clock_update_ms"
    type = "uint32"
    default = 60000
    persistent = true
    [[classes.keys]]
    id = "weather_update_ms"
    type = "uint32"
    default = 1800000
    persistent = true
    [[classes.keys]]
    id = "background_image"
    type = "string"
    max_size = 128
    default = "A:/backgrounds/splash.png"
    persistent = true
    event = true
    [[classes.keys]]
    id = "text_color"
    type = "uint32"
    default = 0xFFFFFF
    persistent = true
    event = true
    [[classes.keys]]
    id = "clock_anim"
    type = "uint8"
    enum = "clock_anim"
    default = 0
    persistent = true
    event = true

[[classes]]
id = "features"
    [[classes.keys]]
    id = "enable_weather"
    type = "bool"
    default = true
    persistent = true
    [[classes.keys]]
    id = "enable_touch"
    type = "bool"
    default = true
    persistent = true

[[classes]]
id = "ota"
    [[classes.keys]]
    id = "server_url"
    type = "string"
    max_size = 128
    default = "<from secrets.h>"
    persistent = true

[[classes]]
id = "nws"                 # cache written by nws task
    [[classes.keys]]
    id = "office"
    type = "string"
    max_size = 8
    persistent = true
    [[classes.keys]]
    id = "grid_x"
    type = "int32"
    persistent = true
    [[classes.keys]]
    id = "grid_y"
    type = "int32"
    persistent = true
    [[classes.keys]]
    id = "station"
    type = "string"
    max_size = 8
    persistent = true

[[classes]]
id = "schedule"            # all uint16 seconds, all persistent, all event = true
    [[classes.keys]]
    id = "weather_show_s"
    type = "uint16"
    default = 30
    persistent = true
    event = true
    [[classes.keys]]
    id = "weather_cooldown_s"
    type = "uint16"
    default = 1800
    persistent = true
    event = true
    # radar_show_s 30 / radar_cooldown_s 1800 / astro_show_s 30 /
    # astro_cooldown_s 43200 / photos_interval_s 1800 / photos_show_s 30 /
    # ambient_interval_s 2700 / ambient_show_s 30, same shape
```

Notes:
- `version` and `wifi_configured` stop being special. `version` is gone because per-key storage needs no version.
- String capacity is fine. `max_size` includes the NUL: `StringStorage_SetKey` rejects `len >= max_size` (`templates/string_storage.c:76-78`), which matches our `char[N]` semantics today. One behaviour change: an over-long value is now **rejected** where `snprintf` silently truncated it before.

### Call-site mapping

| Today | With ingot |
|---|---|
| `boot_settings_init` → `settings_init` + `settings_load` (`main/main.cpp:490-511`) | `nvs_flash_init`, `DataModel_Initialize(cb)`, `DataModel_LoadPersistentKeys()` (NVS backend, G1). Same boot stage, same OTA-window position. |
| `settings_load(&cfg)`, then read one field (P5) | `DataModel_Get_GWC_DISPLAY_BRIGHTNESS()`. RAM read, no NVS. |
| load → modify → `settings_save` (P3) | `DataModel_Set_GWC_DISPLAY_BRIGHTNESS(v)` plus a per-key persist (G2). There is nothing to merge, so the lost update is gone. |
| Wi-Fi ssid+password+configured save (`state_settings.cpp:483-489`) | Batched set + one commit (G3). |
| `settings_changes_t` + `display_fsm_refresh_*` (`http_api.c:2342-2357`) | Setter → callback → enqueue `{SETTING_CHANGED, key}` → `fsm_task` calls `send_tinyfsm_event_by_key(key)` → states `react(FSM_EVENT_GWC_DISPLAY_BACKGROUND_IMAGE)`. |
| `clock_anim_pref_get/set` | `DataModel_Get/Set_GWC_DISPLAY_CLOCK_ANIM`, which can read the **existing** `clock_cfg/clock_anim` u8 if G1 allows explicit NVS key names. |
| `GET /api/settings` (`http_api.c:2249`) | Unchanged JSON, built from getters. |

---

## 3. Gaps and repairs needed in ingot

Severity: **B** = blocker for this firmware, **S** = should fix before the blob moves, **N** = nice to have.

| ID | Sev | Gap, verified against ingot source | Proposed change to ingot |
|---|---|---|---|
| G1 | **B** | Persistence is `stdio` file only (`templates/persistence_storage.c:93,134`), with default path `/sdcard/dm.bin` (`persistence_storage.h:14`). At our settings stage SD is optional and SPIFFS is not mounted (it mounts after `boot_await_launch`). Wi-Fi credentials must load before `boot_network_early`. | Add a pluggable backend: generate calls to `dm_persist_read(key, buf, len)` and `dm_persist_write(key, buf, len)` (weak or consumer-provided), and ship an `nvs` backend for `esp-*` targets. Per key: `nvs_get_u8/u16/u32/i32/str` and the matching setters. Per model: an NVS namespace (`[meta] nvs_namespace`, at most 15 chars). |
| G2 | **B** | Save is all-or-nothing. `DataModel_SavePersistentKeys` syncs and writes the whole struct (`persistence_storage.c:128-147`). There is no "persist this key". | `DM_RETURN_CODE DataModel_PersistKey(uint32_t key)`, plus an opt-in `persist_on_set = true` per key (or per model) that writes through after a successful, changed set. |
| G3 | **B** | Schema evolution: magic `sizeof ^ num_keys` (`persistence_storage.c:25,107-110`) rejects the whole file on any change, which is exactly P1. It also misses layout changes that keep the size (reorder, type swap of equal width). | Comes free with per-key G1. For the file backend, use a TLV format `{key_id, len, bytes}` so that unknown keys are skipped and missing keys keep their defaults. Load result reports counts (loaded / defaulted / rejected) instead of one magic code. |
| G4 | **B** | Persistent identity is unstable. The key id is `key_index.unwrap_or(key_pos)` (`src/codegen/mod.rs:600,708`), so inserting a key shifts later ids. NVS key names are limited to 15 chars, and ours run to 18 (`weather_cooldown_s`). | Per-key `nvs_key = "..."` attribute (validated as at most 15 chars and unique in the namespace). Otherwise require pinned `key_index` on every `persistent` key and derive the NVS name `k%08x`. Validation error if a persistent key has neither. |
| G5 | S | No `float` type. Type code 7 is reserved (`src/model/schema.rs:160`). lat/lon are floats today. | Add `float` (dm_val_t `fval`, `nvs` stored as u32 bits). Workaround until then: `int32` micro-degrees, as in the draft. |
| G6 | S | No secrets handling. The Wi-Fi password would be an ordinary key: `DataModel_PrintPersistentKeys` dumps every value (`persistence_storage.c:152-169`), `dm_full.yaml` carries defaults, and the generated tests print values. | `secret = true`: excluded from print/dump and manifest defaults, getter only via copy-out (G8), and documented as "store in an encrypted NVS partition" for the nvs backend. |
| G7 | S | No range or enum validation on set or load. `SetIntegralTypeByKey` only checks read-only and type (`templates/dm.c:55-119`), so `enum =` is documentary. We lose the `clock_anim` check (`clock_anim_pref.c:49`). | `min`/`max` attributes, plus implicit enum-membership checks, enforced in `Set*` (new `DM_RETURN_CODE_VALUE_OUT_OF_RANGE`) and on load (an out-of-range value is replaced by the default). |
| G8 | S | Torn string reads. `DataModel_GetStringByKey` returns a pointer into live storage (`string_storage.c:93`), and the mutex is released before the caller reads it (`dm.c:193-214`). httpd writing `background_image` while `fsm_task` reads it is a real race. | `DataModel_CopyStringByKey(key, char *buf, size_t len)` performed under the mutex. Make the helpers use it. |
| G9 | S | Event semantics: (a) string sets fire the callback even when the value is unchanged (`dm.c:220-249`, no compare), unlike integral sets; (b) `LoadPersistentKeys` fires a callback for every loaded key (`SyncToStorage` goes through `Set*`); (c) the callback is fixed at `DataModel_Initialize`, and calling it again re-creates the mutex (`dm.c:30-42`). | (a) compare before set and return `SET_VALUE_UNCHANGED`; (b) load writes storage directly with no events, or takes a `fire_events` flag; (c) add `DataModel_SetEventCallback()`. |
| G10 | S | No multi-key atomicity. SSID, password and configured must change together. Save syncs key by key, each under its own lock (`persistence_storage.c:68-86`). | `DataModel_BeginBatch()` / `DataModel_CommitBatch()`: hold the mutex, defer events, and do one backend commit (one `nvs_commit`). |
| G11 | S | No ESP-IDF packaging. The generated `CMakeLists.txt` is a host Unity build only (`templates/CMakeLists.txt`), and `docs/build-integration.rst` says to symlink. | `--emit-idf-component`: `CMakeLists.txt` with `idf_component_register(SRCS ... INCLUDE_DIRS . REQUIRES freertos nvs_flash)`. Optional `idf_component.yml`. |
| G12 | N | Codegen in the build. ingot is a Rust binary. Release ships Linux x86_64 and Windows tarballs (`.github/workflows/release.yml`). | `ingot --check` (exit non-zero if the output dir differs from what would be generated) so we can commit generated code and gate drift in pre-commit. Determinism already has a test (`tests/codegen_determinism.rs`). |
| G13 | N | The tinyfsm seam is hard-wired: `#include "fsmlist.hpp"` plus a `send_tinyfsm_event(E)` template (`templates/dm_key_events_wrapper.cpp:11,18`). It fits, but `send_tinyfsm_event_by_key` must run on `fsm_task` (see §4). | `--tinyfsm-seam-header <name>`. Document the "callback enqueues key, consumer task dispatches" pattern as the recommended RTOS usage. |
| G14 | N | Docs: the README says "per-key mutex support", but the ESP target uses one global `dm_mutex` (`target.rs:66`) taken only for `thread_safe` keys. | Fix the wording. A global mutex is fine for us. |

Checked and dropped:
- **String lengths.** `max_size` is `usize`, sizes are `uint16_t` (`string_storage.c:61`), and 128 works. Not a gap.

---

## 4. Integration design (firmware side, after G1-G4)

```
 setter task (httpd / LVGL cb / nws)          fsm_task
 ---------------------------------           -----------------------------
 DataModel_Set_X(v)
   -> mutex, store, unlock
   -> persist_on_set: nvs_set + commit
   -> cb(key)
        if dm_key_has_event(key):
          display_fsm_send_event(           xQueueReceive
            {SETTING_CHANGED, .key=key}) -->  send_tinyfsm_event_by_key(key)
                                               -> DisplayFsm::dispatch(FSM_EVENT_X{})
                                                  (LVGL lock per state rules)
```

- The callback runs **on the setter's task** after the mutex is released (`templates/dm.c:113-116`). It must only enqueue: no LVGL, no NVS. This matches the LVGL lock rule in CLAUDE.md.
- `display_events.h` gains one type, `DISPLAY_EVT_SETTING_CHANGED`, with a `uint32_t key` payload. Our `fsmlist.hpp` defines `send_tinyfsm_event` as `DisplayFsm::dispatch`.
- Before the FSM queue exists (the launch window), `display_fsm_send_event` returns false. Brightness still applies at display init, as today (`display_fsm_task.cpp:1062-1066`).
- Generated code goes in `components/settings_dm/generated/`, committed, and added to the `knots` and `clew-guard` excludes in `.pre-commit-config.yaml` and `.clew.yaml` next to `tinyfsm.hpp`. A thin hand-written `settings_dm.c` (NVS init, callback, legacy import) stays under both gates.

---

## 5. Phased adoption

| Phase | Content | Needs |
|---|---|---|
| 0 | Land G1-G4 (+G11) in ingot, with ingot-side Unity tests on host. No firmware change. | ingot |
| 1 | **New prefs only.** Bring up the ingot component holding `clock_anim` (same `clock_cfg/clock_anim` u8, so no data change) and any new pref from now on. `clock_settings_t` is untouched. | G1, G4, G11 |
| 2 | Event path: `DISPLAY_EVT_SETTING_CHANGED`, which retires `settings_changes_t` for the migrated keys. | G9 |
| 3 | Move the blob fields key by key, starting with the hot readers (schedule, display) and ending with Wi-Fi. Optional one-shot import: if the `settings` v6 blob exists, copy it into the keys, then erase it. | G2, G3, G7, G8, G10 |
| 4 | Delete `settings.c/.h` and `clock_anim_pref.c/.h`. Update REQ-SET-001/002. | n/a |

### Risks

| Risk | Mitigation |
|---|---|
| A bad load before `boot_await_launch` bricks the device (CLAUDE.md OTA window) | NVS per-key reads are type- and length-checked by NVS. G7 rejects out-of-range values. Phase 1 changes no existing key. |
| A losing import in phase 3 leaves the device off Wi-Fi (recoverable only on the touchscreen) | Import keeps the legacy blob until all keys have been written and committed. Erase it in the next release. |
| NVS fills up: about 40 entries instead of 2 | Strings occupy ~1 entry per 32 B. Measure `nvs_get_stats` before and after phase 3. The partition size is in `partitions.csv`. |
| Generated code drifts from `settings.toml` | G12 `--check` in pre-commit. |
| More flash writes with per-key write-through | Writes now happen only on change (G9a). The slider can persist on `LV_EVENT_RELEASED` instead of `VALUE_CHANGED`. |

### Verification: acceptance criteria (observable behaviour)

- **Given** Wi-Fi is configured, **when** a firmware build that adds or removes a settings key is OTA'd, **then** the device rejoins Wi-Fi and serves `GET /api/settings` with the previous brightness, background and colour (REQ-SET-002 generalised to every key).
- **Given** `POST /api/settings {"brightness":30}` and `POST /api/display/schedule {...}` sent concurrently, **when** both return 200, **then** after a reboot both values are present.
- **Given** `POST /api/settings {"brightness":150}`, **then** the response is 4xx and `GET` still returns the old value.
- **Given** a background is set over HTTP, **then** the clock screen shows it without a reboot, and it is still shown after a reboot.
- **Given** a clean NVS (erase-flash), **then** the first boot shows the default background and animation and offers Wi-Fi setup.
- Host: the ingot-generated Unity suite plus a `test_settings_dm.c` against a fake `dm_persist_*` backend, run via `inv test-host`: round-trip, add-key/remove-key reload, out-of-range load and unchanged-set-no-event.

---

## 6. Open questions

1. Phase 3 import: do we want the one-shot v6 blob import, or is re-entering Wi-Fi on the touchscreen acceptable?
2. Should G1 be an NVS-only backend in ingot, or a generic `dm_persist_*` seam with NVS as one implementation? The seam is more work for Brandon but keeps ingot target-agnostic.
3. Write-through per key (G2 `persist_on_set`) versus an explicit `DataModel_PersistKey` at call sites: which default?
4. Is NVS encryption (flash encryption + `nvs_keys` partition) in scope for the Wi-Fi password, or is G6 limited to "never printed or exposed"?
5. Codegen: commit the generated output and gate drift (proposed), or run ingot from CMake (needs the ingot binary on every build machine)?
6. Do lat/lon remain user settings at all, or move to `secrets.h`/compile time? This decides whether G5 matters.
