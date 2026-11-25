## @file tasks.py
## @brief Invoke tasks: device operations, asset preparation, Lottie verification, host tests and lint.
## @version 1
"""Invoke tasks for greenwood-clock.

Every repeatable command lives here rather than being hand-rolled at the
prompt: a task is discoverable (``.venv/bin/inv --list``), reviewable in git,
and runs identically for a human and for an agent.

Device tasks talk to the clock's local REST API (components/http_api). The
host defaults to the mDNS name the firmware advertises, so a DHCP lease change
does not break them; override with ``--host`` or ``GREENWOOD_HOST``.
"""

import gzip
import json
import os
import re
import subprocess
import sys
import urllib.error
import urllib.request
from datetime import datetime
from pathlib import Path

from invoke import task

REPO = Path(__file__).parent.resolve()
TMP = REPO / ".claude" / "tmp"
SECRETS_H = REPO / "components" / "secrets" / "secrets.h"
DEFAULT_HOST = os.environ.get("GREENWOOD_HOST", "greenwood-clock.local")
TIMEOUT_S = 120

#: ESP-IDF checkout used for builds. A released 5.5.x is required: v5.5-beta1 never hands
#: sdkconfig.json to the component-manager solver, so any dependency re-solve fails.
IDF_PATH = Path(
    os.environ.get("GREENWOOD_IDF_PATH", Path.home() / "esp" / "esp-idf-v5.5.6")
)

#: ESP-IDF's managed Python env — owns esp_coredump and the rest of the IDF tooling.
IDF_PYTHON = (
    Path.home() / ".espressif" / "python_env" / "idf5.5_py3.10_env" / "bin" / "python"
)
RISCV_GDB = (
    Path.home()
    / ".espressif"
    / "tools"
    / "riscv32-esp-elf-gdb"
    / "16.2_20250324"
    / "riscv32-esp-elf-gdb"
    / "bin"
    / "riscv32-esp-elf-gdb"
)

#: Host tests: tests/host/test_<name>.c -> the components/display_fsm/src/*.c it links.
#: Host test name → modules it links. A bare name is a display_fsm source; "component/name"
#: is components/<component>/<name>.c.
HOST_TESTS = {
    "snow_physics": ("snow_physics",),
    "leaf_physics": ("leaf_physics",),
    "leaf_field": ("leaf_field", "leaf_physics"),
    "bg_schedule": ("settings/bg_schedule_model",),
    "ui_contrast": ("ui_contrast",),
    "ui_theme": ("ui_theme", "ui_theme_json", "ui_contrast", "cjson/cjson/cJSON"),
    "radar_frames": ("nws/src/nws_radar_frames",),
    "conditions_merge": ("nws/src/nws_conditions_merge",),
    "sky_data": ("sky_data", "astro_calc"),
    "sched_rule": ("sched_rule", "sky_data", "astro_calc"),
    "sched_engine": ("sched_engine", "sched_rule", "sky_data", "astro_calc"),
    "sched_json": ("sched_json", "cjson/cjson/cJSON"),
    "auth_core": ("auth/auth_core",),
}

#: Include directories for the host tests.
HOST_TEST_INCLUDES = (
    REPO / "components" / "display_fsm" / "include",
    REPO / "components" / "settings",
    REPO / "components" / "nws" / "src",
    REPO / "components" / "nws" / "include",
    REPO / "components" / "display_events" / "include",
    REPO / "components" / "cjson" / "cjson",
    REPO / "components" / "auth" / "include",
    REPO / "tests" / "host" / "stubs",  # minimal lvgl.h for the pure display helpers
)

#: Log slot → URL path. Slot 0 is the live file and goes through the API so the
#: device pauses its write handle; older slots are closed and stream directly.
LOG_SLOTS = {
    0: "/api/logs/download",
    1: "/files/logs/debug_log1.log",
    2: "/files/logs/debug_log2.log",
}


## @brief Read OTA_API_TOKEN from the gitignored secrets header.
## @utility
## @version 1
## @return The bearer token string.
def _api_token():
    match = re.search(r'#define\s+OTA_API_TOKEN\s+"([^"]*)"', SECRETS_H.read_text())
    if match is None:
        raise SystemExit(f"OTA_API_TOKEN not found in {SECRETS_H}")
    return match.group(1)


## v3: always sends the admin token (the clock gates every endpoint; auth is ignored).
## @brief Perform an HTTP request against the device and return the body.
## @utility
## @version 3
## @return Response body bytes, or None when the device answered with an HTTP error.
def _request(host, path, auth=False, method="GET", payload=None):
    url = f"http://{host}{path}"
    data = None if payload is None else json.dumps(payload).encode()
    req = urllib.request.Request(url, data=data, method=method)
    if data is not None:
        req.add_header("Content-Type", "application/json")
    req.add_header("Authorization", f"Bearer {_api_token()}")  # every endpoint is gated
    body = None
    try:
        with urllib.request.urlopen(req, timeout=TIMEOUT_S) as resp:
            body = resp.read()
    except urllib.error.HTTPError as exc:
        print(
            f"{method} {url} → HTTP {exc.code}: {exc.read().decode(errors='replace')}",
            file=sys.stderr,
        )
    except urllib.error.URLError as exc:
        raise SystemExit(f"{method} {url} unreachable: {exc.reason}") from exc
    return body


## @brief Fetch a JSON endpoint and pretty-print it.
## @utility
## @version 1
def _print_json(host, path, auth=False):
    body = _request(host, path, auth=auth)
    if body is not None:
        try:
            print(json.dumps(json.loads(body), indent=2))
        except json.JSONDecodeError:
            print(body.decode(errors="replace"))


## @brief Device health: uptime, heap, SD card, debug-log state.
## @req REQ-TOOL-001
## @version 1
@task(help={"host": "Device host or IP"})
def status(c, host=DEFAULT_HOST):
    _print_json(host, "/api/status")


## @brief Ring buffer of recent reset reasons and timestamps.
## @req REQ-TOOL-001
## @version 1
@task(help={"host": "Device host or IP"})
def boot_history(c, host=DEFAULT_HOST):
    _print_json(host, "/api/boot_history")


## @brief SD card capacity and usage.
## @req REQ-TOOL-001
## @version 1
@task(help={"host": "Device host or IP"})
def disk(c, host=DEFAULT_HOST):
    _print_json(host, "/api/disk")


## @brief Coredump partition summary (authenticated).
## @req REQ-TOOL-001
## @version 1
@task(help={"host": "Device host or IP"})
def coredump_info(c, host=DEFAULT_HOST):
    _print_json(host, "/api/coredump/info", auth=True)


## @brief Download the stored coredump and decode it against build/greenwood-clock.elf.
## @req REQ-TOOL-001
## @version 1
@task(
    help={"host": "Device host or IP", "elf": "App ELF matching the running firmware"}
)
def coredump(c, host=DEFAULT_HOST, elf=str(REPO / "build" / "greenwood-clock.elf")):
    body = _request(host, "/api/coredump", auth=True)
    if body is None:
        raise SystemExit("no coredump retrieved")
    out = TMP / f"core_{datetime.now():%Y%m%d_%H%M%S}.elf"
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(body)
    print(f"coredump: {len(body)} B → {out}")
    c.run(
        f"{IDF_PYTHON} -m esp_coredump --chip esp32p4 info_corefile --gdb {RISCV_GDB} -t raw -c {out} {elf}",
        warn=True,
    )


## @brief Current display FSM state.
## @req REQ-TOOL-001
## @version 1
@task(help={"host": "Device host or IP"})
def display_state(c, host=DEFAULT_HOST):
    _print_json(host, "/api/display/state")


## @brief Force the display FSM into a named state (weather, radar, astronomy, clock, ...).
## @req REQ-TOOL-001
## @version 1
@task(help={"host": "Device host or IP", "state": "State name, e.g. weather"})
def show(c, state, host=DEFAULT_HOST):
    body = _request(host, "/api/display/state", method="POST", payload={"state": state})
    if body is not None:
        print(body.decode(errors="replace"))


## @brief Show the device's persisted settings.
## @req REQ-TOOL-001
## @version 1
@task(help={"host": "Device host or IP"})
def settings(c, host=DEFAULT_HOST):
    _print_json(host, "/api/settings")


#: Image types bg-ingest converts.
INGEST_SUFFIXES = (".jpg", ".jpeg", ".png", ".bmp", ".webp")


## @brief File-name slug: lower case, runs of non-alphanumerics become one underscore.
## @utility
## @version 1
## @return The slug.
def _slug(name):
    return re.sub(r"[^a-z0-9]+", "_", name.lower()).strip("_")


## @brief Convert every image in a folder to a 1024x600 PNG (sdcard/backgrounds) and push it.
## @req REQ-BG-004
## @version 1
@task(
    help={
        "src": "Folder of source images (default: image_ingest)",
        "fit": "fill (scale + crop, default) or contain (letterbox)",
        "host": "Device host or IP",
    }
)
def bg_ingest(c, src="image_ingest", fit="fill", host=DEFAULT_HOST):
    out = REPO / "sdcard" / "backgrounds"
    out.mkdir(parents=True, exist_ok=True)
    for img in sorted(Path(src).iterdir()):
        if img.suffix.lower() not in INGEST_SUFFIXES:
            continue
        dest = out / f"{_slug(img.stem)}.png"
        convert = REPO / "tools" / "image_convert.py"
        c.run(f'{sys.executable} {convert} "{img}" -o "{dest}" --fit {fit}')
        push = REPO / "tools" / "file_push.py"
        c.run(f'{sys.executable} {push} "{dest}" backgrounds/{dest.name} --host {host}')
        print(f"{img.name} -> A:/backgrounds/{dest.name}")


## @brief Print the daily/events schedule, or replace it with a JSON file (sched_json.h format);
## a schedule whose profiles overlap is refused with the reason.
## v2: /api/schedule (daily profiles and events; was the weekly grid at /api/bg_schedule).
## @req REQ-TOOL-001
## @req REQ-BG-004
## @version 2
@task(
    help={
        "file": "Schedule JSON to POST (omit to print the current one)",
        "host": "Device host or IP",
    }
)
def schedule(c, file=None, host=DEFAULT_HOST):
    if file is None:
        _print_json(host, "/api/schedule")
        return
    body = _request(
        host, "/api/schedule", method="POST", payload=json.loads(Path(file).read_text())
    )
    if body is not None:
        print(body.decode(errors="replace"))


## @brief Set the background image (LVGL path, e.g. A:/backgrounds/campfire.png).
## @req REQ-TOOL-001
## @version 1
@task(help={"host": "Device host or IP", "path": "LVGL path, A: maps to /sdcard"})
def set_bg(c, path, host=DEFAULT_HOST):
    body = _request(
        host, "/api/settings", method="POST", payload={"background_image": path}
    )
    if body is not None:
        print(body.decode(errors="replace"))


#: Short CLI names -> the firmware's display names (clock_anim_from_name matches those,
#: case-insensitively). Anything else is passed through unchanged.
ANIM_NAMES = {
    "hummingbird": "Hummingbird",
    "snowfall": "Snowfall",
    "leaves": "Falling Leaves",
    "falling-leaves": "Falling Leaves",
    "none": "None",
}


## v2: help lists every animation; short names (leaves) map to the firmware display names.
## @brief Select the clock-screen animation: hummingbird, snowfall, leaves or none.
## @req REQ-TOOL-001
## @version 2
@task(
    help={
        "host": "Device host or IP",
        "name": "hummingbird | snowfall | leaves | none (or a display name, e.g. 'Falling Leaves')",
    }
)
def set_anim(c, name, host=DEFAULT_HOST):
    anim = ANIM_NAMES.get(name.lower(), name)
    body = _request(host, "/api/settings", method="POST", payload={"clock_anim": anim})
    if body is not None:
        print(body.decode(errors="replace"))


## @brief Set the global UI theme (a file stem in /sdcard/themes, e.g. default, sassafras);
## with no name, list the installed themes and show the current one.
## @req REQ-TOOL-001
## @req REQ-UI-003
## @version 1
@task(help={"host": "Device host or IP", "name": "Theme file stem (omit to list)"})
def set_theme(c, name=None, host=DEFAULT_HOST):
    if name is None:
        for path in ("/api/themes", "/api/theme"):
            body = _request(host, path)
            if body is not None:
                print(body.decode(errors="replace"))
        return
    body = _request(host, "/api/settings", method="POST", payload={"theme": name})
    if body is not None:
        print(body.decode(errors="replace"))


## @brief Stream live device logs over UDP (Ctrl+C to stop).
## @req REQ-TOOL-001
## @version 1
@task(help={"host": "Device host or IP", "out": "Also tee to this file"})
def udp_log(c, host=DEFAULT_HOST, out=None):
    cmd = f"{sys.executable} -u {REPO / 'tools' / 'udp_log_listen.py'} --host {host}"
    c.run(f"{cmd} | tee {out}" if out else cmd, pty=False)


## @brief Cached NWS weather: current conditions, forecast, alerts.
## @req REQ-TOOL-001
## @version 1
@task(help={"host": "Device host or IP"})
def weather(c, host=DEFAULT_HOST):
    for path in (
        "/api/weather/current",
        "/api/weather/forecast",
        "/api/weather/alerts",
    ):
        print(f"── {path}")
        _print_json(host, path)


## @brief List an SD card directory.
## @req REQ-TOOL-001
## @version 1
@task(help={"host": "Device host or IP", "path": "SD directory, e.g. /backgrounds"})
def ls(c, path="/", host=DEFAULT_HOST):
    c.run(f"{sys.executable} {REPO / 'tools' / 'file_list.py'} {path} --host {host}")


## @brief Download one SD card file into .claude/tmp/pull/.
## @req REQ-TOOL-001
## @version 1
@task(
    help={
        "host": "Device host or IP",
        "path": "SD path, e.g. /backgrounds/snowfall.gif",
    }
)
def pull(c, path, host=DEFAULT_HOST):
    dest = TMP / "pull" / Path(path).name
    dest.parent.mkdir(parents=True, exist_ok=True)
    c.run(
        f"{sys.executable} {REPO / 'tools' / 'file_pull.py'} {path} {dest} --host {host}"
    )


## @brief Download ALL log slots into .claude/tmp/logs/<timestamp>/.
## @req REQ-TOOL-001
## @version 1
@task(help={"host": "Device host or IP"})
def logs(c, host=DEFAULT_HOST):
    dest = TMP / "logs" / datetime.now().strftime("%Y%m%d_%H%M%S")
    dest.mkdir(parents=True, exist_ok=True)
    for slot, path in LOG_SLOTS.items():
        body = _request(host, path)
        if slot == 0 and body is None:
            # API 500s when the on-device logger has stopped; the slot file still holds its output.
            body = _request(host, "/files/logs/debug_log0.log")
        if body is None:
            continue
        out = dest / f"slot{slot}.log"
        out.write_bytes(body)
        print(f"slot {slot}: {len(body)} B → {out}")


## @brief Capture the live framebuffer to a PNG under .claude/tmp/.
## @req REQ-TOOL-001
## @version 1
@task(help={"host": "Device host or IP", "out": "Output PNG path"})
def screenshot(c, host=DEFAULT_HOST, out=None):
    out = out or TMP / f"screen_{datetime.now():%Y%m%d_%H%M%S}.png"
    c.run(f"{sys.executable} {REPO / 'tools' / 'screenshot.py'} --host {host} -o {out}")


## @brief Record the next rendered frames (half resolution) to PNGs and a contact sheet; with --state, across that forced state change.
## @req REQ-API-003
## @version 1
@task(
    help={
        "host": "Device host or IP",
        "state": "Force this state; recording starts at the change (else immediately)",
        "frames": "Frames to record (1-24)",
        "out": "Output directory (default .claude/tmp/capture_<state>_<time>)",
    }
)
def capture(c, state=None, frames=16, host=DEFAULT_HOST, out=None):
    out = out or TMP / f"capture_{state or 'now'}_{datetime.now():%H%M%S}"
    state_arg = f"--state {state}" if state else ""
    c.run(
        f"{sys.executable} {REPO / 'tools' / 'frame_capture.py'} --host {host} "
        f"--out {out} --frames {frames} {state_arg}"
    )


## @brief Reboot the device.
## @req REQ-TOOL-001
## @version 1
@task(help={"host": "Device host or IP"})
def reboot(c, host=DEFAULT_HOST):
    c.run(f"{sys.executable} {REPO / 'tools' / 'device_reboot.py'} --host {host}")


## @brief Incremental firmware build through ESP-IDF (sources export.sh first).
## @utility
## @version 2
@task
def build(c):
    export = IDF_PATH / "export.sh"
    c.run(f"bash -c '. {export} >/dev/null 2>&1 && cd {REPO} && idf.py build'")


## @brief Push build/greenwood-clock.bin to the device over HTTP OTA (token from secrets.h).
## @req REQ-TOOL-001
## @version 1
@task(help={"host": "Device host or IP"})
def ota(c, host=DEFAULT_HOST):
    binary = REPO / "build" / "greenwood-clock.bin"
    env = {"OTA_API_TOKEN": _api_token()}  # env, not argv: keeps the token out of `ps`
    c.run(
        f"{sys.executable} {REPO / 'tools' / 'ota_push.py'} --host {host} {binary}",
        env=env,
    )


## @brief Render static forecast icons from the weather Lottie set into sdcard/icons/.
## @req REQ-TOOL-002
## @req REQ-WX-006
## @version 1
@task(
    iterable=["override"],
    help={"size": "Icon px", "override": "tod/name=frame (repeatable)"},
)
def forecast_icons(c, size=64, override=None):
    sheet = TMP / "forecast_icons_sheet.png"
    extra = " ".join(f"--override {o}" for o in (override or []))
    c.run(
        f"{sys.executable} {REPO / 'tools' / 'forecast_icons.py'} --size {size} --sheet {sheet} {extra}"
    )


## @brief Pull every Lottie the device uses, flag drift from sdcard/ staging, render-audit all frames.
## @req REQ-TOOL-003
## @version 1
@task(
    help={
        "host": "Device host or IP",
        "local": "Audit repo staging copies instead of pulling",
    }
)
def lottie_audit(c, host=DEFAULT_HOST, local=False):
    staging = REPO / "sdcard"
    rel_paths = [
        p.relative_to(staging) for p in sorted((staging / "lottie").rglob("*.json"))
    ]
    rel_paths.append(Path("lottie/hummingbird.json"))
    root = staging if local else TMP / "device_lottie"
    if not local:
        _pull_lottie(host, rel_paths, root, staging)
    c.run(
        f"{sys.executable} {REPO / 'tools' / 'lottie_audit.py'} --root {root} {root / 'lottie'}",
        warn=True,
    )


## @brief Map of file name -> size from the device's directory listing.
## @utility
## @version 1
## @return {name: bytes}; empty when the listing fails.
def _listing_sizes(host, remote_dir):
    body = _request(host, f"/files/{remote_dir}")
    entries = json.loads(body).get("files", []) if body else []
    return {e["name"]: e["size"] for e in entries if e.get("type") == "file"}


## @brief GET a device file, retrying until its length matches the listed size.
## @utility
## @version 1
## @return File bytes, or None when missing or still truncated after retries (both reported).
def _fetch_verified(host, rel, expected, tries=4):
    # Transfers from the device intermittently end early with no HTTP error (seen:
    # 1536 / 2048 of 23855 B), so a body is only trusted when it matches the listing.
    body = None
    for _ in range(tries):
        body = _request(host, f"/files/{rel.as_posix()}")
        if body is None or expected is None or len(body) == expected:
            break
        print(f"TRUNCATED transfer {rel}: {len(body)}/{expected} B — retrying")
    if body is None or expected is None:
        print(f"MISSING on device: {rel}")
        return None
    if len(body) != expected:
        print(f"GAVE UP {rel}: still {len(body)}/{expected} B after {tries} tries")
        return None
    return body


## @brief Visual vet: frame-to-frame jump + loop-seam detection and contact sheets per Lottie.
## @req REQ-TOOL-003
## @version 1
@task(help={"local": "Vet repo staging copies instead of the pulled device copies"})
def lottie_vet(c, local=False):
    root = REPO / "sdcard" if local else TMP / "device_lottie"
    if not (root / "lottie").exists():
        raise SystemExit(
            f"{root / 'lottie'} missing — run `inv lottie-audit` first to pull device copies"
        )
    sheets = TMP / "lottie_vet"
    c.run(
        f"{sys.executable} {REPO / 'tools' / 'lottie_vet.py'} --root {root} --out {sheets} {root / 'lottie'}",
        warn=True,
    )


## @brief Structural test of generated Lottie files (monotonic keyframe times).
## @req REQ-LOTTIE-002
## @version 1
@task
def test_lottie(c):
    c.run(f"{sys.executable} {REPO / 'tests' / 'test_lottie_keyframes.py'}")


## @brief Download each device Lottie into @p root and report differences vs the staging copy.
## @req REQ-TOOL-003
## @version 1
def _pull_lottie(host, rel_paths, root, staging):
    sizes = {}
    for rel in rel_paths:
        parent = rel.parent.as_posix()
        if parent not in sizes:
            sizes[parent] = _listing_sizes(host, parent)
        body = _fetch_verified(host, rel, sizes[parent].get(rel.name))
        if body is None:
            continue
        out = root / rel
        out.parent.mkdir(parents=True, exist_ok=True)
        out.write_bytes(body)
        local = staging / rel
        if local.exists() and local.read_bytes() != body:
            print(f"DRIFT: device {rel} differs from sdcard/{rel}")


## @brief Upload every file matching @p pattern under sdcard/<subdir> to the same path on the device SD card.
## @req REQ-TOOL-001
## @version 1
def _push_staging_tree(c, subdir, pattern, host):
    staging = REPO / "sdcard"
    for src in sorted((staging / subdir).rglob(pattern)):
        remote = src.relative_to(staging).as_posix()
        c.run(
            f"{sys.executable} {REPO / 'tools' / 'file_push.py'} {src} {remote} --host {host}"
        )


#: Clock fonts: (point size, original 1 bpp font whose line metrics they keep, glyph selector).
#: Time digits only at 256/128 pt; printable ASCII at 48 pt (AM/PM + weekday/month names).
CLOCK_FONTS = (
    (256, "nunito_256", "--symbols '0123456789:'"),
    (128, "nunito_128", "--symbols '0123456789:'"),
    (48, "nunito_48", "--range 0x20-0x7E"),
)
FONTS_DIR = REPO / "components" / "fonts"


## @brief Copy `.line_height` / `.base_line` from the original font and mark the new font static_bitmap.
## @utility
## @version 1
## @return Patched C source text.
def _patch_clock_font(src, original):
    import re

    for field in ("line_height", "base_line"):
        value = re.search(rf"\.{field} = (\d+),", original).group(1)
        src, n = re.subn(rf"\.{field} = \d+,", f".{field} = {value},", src, count=1)
        assert n == 1, field
    src, n = re.subn(
        r"(\.dsc = &font_dsc,[^\n]*\n)", r"\1    .static_bitmap = 1,\n", src, count=1
    )
    assert n == 1, "static_bitmap"
    return src


## @brief Generate the 8 bpp static-bitmap clock fonts (time digits; 48 pt printable ASCII) from Nunito-ExtraBold.
## The 1 bpp fonts made LVGL expand each whole glyph for every dirty area touching the digits
## (snowfall redraws ~120 small areas per frame); 8 bpp static glyphs blend straight from flash
## and are anti-aliased. Line metrics are copied from the original fonts so layout is unchanged.
## v2: also a 48 pt printable-ASCII font for the AM/PM and date labels.
## @req REQ-CLOCK-001
## @req REQ-ANIM-004
## @version 2
@task
def gen_fonts(c):
    conv = REPO / "tools" / "fonts" / "node_modules" / ".bin" / "lv_font_conv"
    if not conv.exists():
        c.run(f"npm ci --prefix {REPO / 'tools' / 'fonts'} --no-fund --no-audit")
    ttf = FONTS_DIR / "fonts" / "Nunito-ExtraBold.ttf"
    for size, original, glyphs in CLOCK_FONTS:
        name = f"nunito_clock_{size}"
        out = FONTS_DIR / "c_array" / f"{name}.c"
        c.run(
            f"{conv} --font {ttf} --size {size} --bpp 8 --no-compress {glyphs} "
            f"--format lvgl --lv-font-name {name} -o {out}"
        )
        orig = (FONTS_DIR / "c_array" / f"{original}.c").read_text()
        out.write_text(_patch_clock_font(out.read_text(), orig))
        print(
            f"{name}: {out.stat().st_size:,} B (metrics from {original}, static_bitmap)"
        )


## @brief Upload sdcard/themes/*.json (the UI themes) to the device SD card. Apply a pushed
## theme with `inv settings` / the settings screen; the device lists themes on each re-apply.
## @req REQ-TOOL-002
## @req REQ-UI-003
## @version 1
@task(help={"host": "Device host or IP"})
def push_themes(c, host=DEFAULT_HOST):
    _push_staging_tree(c, "themes", "*.json", host)


## v2: the WebAssembly panel (index.html, panel.js, panel.wasm); the old web panel is gone.
## v3: and their gzipped twins (*.gz).
## @brief Build the phone panel and upload it (sdcard/www/panel/) to the device SD card.
## @req REQ-TOOL-002
## @req REQ-UI-002
## @version 3
@task(help={"host": "Device host or IP"})
def push_www(c, host=DEFAULT_HOST):
    panel_build(c)
    for pattern in ("*.html", "*.js", "*.wasm", "*.gz"):
        _push_staging_tree(c, "www", pattern, host)


## v2: shares _push_staging_tree with push-lottie.
## @brief Upload sdcard/icons/** (forecast icons) to the device SD card.
## @req REQ-TOOL-002
## @req REQ-WX-006
## @version 2
@task(help={"host": "Device host or IP"})
def push_icons(c, host=DEFAULT_HOST):
    _push_staging_tree(c, "icons", "*.png", host)


## @brief Upload sdcard/lottie/<group>/** (generated Lotties, default: weather + ui) to the device SD card.
## @req REQ-TOOL-002
## @req REQ-LOTTIE-002
## @version 1
@task(
    help={
        "host": "Device host or IP",
        "groups": "Comma-separated sdcard/lottie subdirs to push (default: weather,ui)",
    }
)
def push_lottie(c, host=DEFAULT_HOST, groups="weather,ui"):
    for group in groups.split(","):
        _push_staging_tree(c, f"lottie/{group.strip()}", "*.json", host)


## @brief Source path of a host-test module ("name" in display_fsm, or "component/name").
## @utility
## @version 1
def _host_module_src(module):
    if "/" in module:
        return REPO / "components" / f"{module}.c"
    return REPO / "components" / "display_fsm" / "src" / f"{module}.c"


## v3: modules may come from other components; include dirs from HOST_TEST_INCLUDES.
## @brief Build and run the host-side C unit tests (pure-logic modules, no ESP-IDF).
## @req REQ-TOOL-004
## @version 3
@task
def test_host(c):
    out = TMP / "host_tests"
    out.mkdir(parents=True, exist_ok=True)
    incs = " ".join(f"-I{d}" for d in HOST_TEST_INCLUDES)
    for name, modules in HOST_TESTS.items():
        srcs = " ".join(str(_host_module_src(m)) for m in modules)
        test = REPO / "tests" / "host" / f"test_{name}.c"
        exe = out / f"test_{name}"
        c.run(
            f"gcc -std=gnu11 -Wall -Wextra -Werror -O1 {incs} {srcs} {test} -lm -o {exe}"
        )
        c.run(str(exe))


## @brief Run every pre-commit hook over the whole tree.
## @utility
## @version 1
@task
def lint(c):
    subprocess.run(
        [str(REPO / ".venv" / "bin" / "pre-commit"), "run", "--all-files"], check=True
    )


#: Host LVGL build: the fork compiled with the device's own sdkconfig (tools/lvgl_host).
LVGL_HOST_DIR = TMP / "lvgl_host"
LVGL_HOST_DEFINE = '-DLV_CONF_KCONFIG_EXTERNAL_INCLUDE=\\"lv_host_kconfig.h\\"'
#: Firmware sources the theme preview links: the theme code and the real widgets.
THEME_PREVIEW_SRCS = (
    "components/display_fsm/src/ui_theme.c",
    "components/display_fsm/src/ui_theme_json.c",
    "components/display_fsm/src/ui_theme_lvgl.c",
    "components/display_fsm/src/ui_theme_bind.c",
    "components/display_fsm/src/theme_editor.c",
    "components/display_fsm/src/theme_editor_tracks.c",
    "components/display_fsm/src/theme_editor_preview.c",
    "components/display_fsm/src/ui_contrast.c",
    "components/display_fsm/src/weather_widgets.c",
    "components/display_fsm/src/alert_banner.c",
    "components/display_fsm/src/alert_card.c",
    "components/display_fsm/src/sky_data.c",
    "components/display_fsm/src/sky_panel.c",
    "components/display_fsm/src/sky_dial.c",
    "components/display_fsm/src/sched_rule.c",
    "components/display_fsm/src/phone_view.c",
    "components/display_fsm/src/settings_app.c",
    "components/display_fsm/src/settings_sched.c",
    "components/display_fsm/src/settings_theme.c",
    "components/display_fsm/src/settings_pickers.c",
    "components/display_fsm/src/sched_engine.c",
    "components/display_fsm/src/sched_json.c",
    "components/display_fsm/src/sched_ui.c",
    "tools/panel/settings_mock.c",
    "components/display_fsm/src/astro_calc.c",
    "components/display_fsm/src/settings_frame.c",
    "components/display_fsm/src/clock_widget.c",
    "components/display_fsm/src/text_roll.c",
    "components/display_fsm/src/refresh_probe.c",
    "components/cjson/cjson/cJSON.c",
    "components/fonts/c_array/nunito_48.c",
    "components/fonts/c_array/nunito_128.c",
    "components/fonts/c_array/nunito_256.c",
    "components/fonts/c_array/nunito_clock_256.c",
    "components/fonts/c_array/nunito_clock_128.c",
    "components/fonts/c_array/nunito_clock_48.c",
    "tools/theme_preview/preview.c",
)
#: Include path for host builds against the real LVGL (stubs last, so real headers win).
LVGL_HOST_INCLUDES = (
    LVGL_HOST_DIR,
    REPO / "components" / "lvgl",
    REPO / "components",  # "lvgl/lvgl.h", as the firmware build resolves it (fonts)
    REPO / "components" / "display_fsm" / "include",
    REPO / "components" / "nws" / "include",
    REPO / "components" / "settings",
    REPO / "components" / "display_events" / "include",
    REPO / "components" / "cjson" / "cjson",
    REPO / "tools" / "lvgl_host" / "stubs",
    REPO
    / "tests"
    / "host"
    / "stubs",  # esp_err.h (its lvgl.h is shadowed by the real one)
)


## v2: the compiler, archiver, output folder and extra flags are parameters (the panel's
## @brief Build (incrementally) the LVGL fork for the host from the device's sdkconfig.h.
## WebAssembly build uses emcc).
## @utility
## @version 2
## @return Path to liblvgl.a.
def _lvgl_host_lib(c, out_dir=None, cc="gcc", ar="ar", flags=""):
    from concurrent.futures import ThreadPoolExecutor

    out_dir = out_dir or LVGL_HOST_DIR
    sdk = REPO / "build" / "config" / "sdkconfig.h"
    if not sdk.exists():
        raise SystemExit("build/config/sdkconfig.h missing: run `inv build` once first")
    kconf = LVGL_HOST_DIR / "lv_host_kconfig.h"
    gen = REPO / "tools" / "lvgl_host" / "gen_kconfig.py"
    c.run(f"{sys.executable} {gen} {sdk} {kconf} {REPO / 'sdcard'}")
    obj_dir = out_dir / "obj"
    obj_dir.mkdir(parents=True, exist_ok=True)
    srcs = sorted((REPO / "components" / "lvgl" / "src").rglob("*.c"))
    incs = f"-I{LVGL_HOST_DIR} -I{REPO / 'components' / 'lvgl'}"

    def build(src):
        obj = obj_dir / (src.relative_to(REPO).as_posix().replace("/", "_") + ".o")
        if obj.exists() and obj.stat().st_mtime > max(
            src.stat().st_mtime, kconf.stat().st_mtime
        ):
            return obj
        subprocess.run(
            f"{cc} -c -O2 -w {LVGL_HOST_DEFINE} {flags} {incs} {src} -o {obj}",
            shell=True,
            check=True,
        )
        return obj

    with ThreadPoolExecutor(max_workers=os.cpu_count()) as pool:
        objs = list(pool.map(build, srcs))
    lib = out_dir / "liblvgl.a"
    if not lib.exists() or max(o.stat().st_mtime for o in objs) > lib.stat().st_mtime:
        c.run(f"{ar} rcs {lib} {' '.join(str(o) for o in objs)}")
    return lib


#: The phone panel (the clock's settings UI as WebAssembly): Emscripten, its LVGL build and
#: the firmware sources it links.
EMCC = Path.home() / "emsdk" / "upstream" / "emscripten" / "emcc"
EMAR = Path.home() / "emsdk" / "upstream" / "emscripten" / "emar"
PANEL_LVGL_DIR = TMP / "lvgl_wasm"
PANEL_FLAGS = "-sUSE_SDL=2 -DCONFIG_LV_USE_SDL=1"
PANEL_INCLUDES = (REPO / "tools" / "panel",)
PANEL_SRCS = (
    "components/display_fsm/src/ui_theme.c",
    "components/display_fsm/src/ui_theme_json.c",
    "components/display_fsm/src/ui_theme_lvgl.c",
    "components/display_fsm/src/ui_theme_bind.c",
    "components/display_fsm/src/ui_contrast.c",
    "components/display_fsm/src/settings_frame.c",
    "components/display_fsm/src/settings_app.c",
    "components/display_fsm/src/settings_sched.c",
    "components/display_fsm/src/settings_theme.c",
    "components/display_fsm/src/settings_pickers.c",
    "components/display_fsm/src/theme_editor.c",
    "components/display_fsm/src/theme_editor_tracks.c",
    "components/display_fsm/src/theme_editor_preview.c",
    "components/display_fsm/src/sched_rule.c",
    "components/display_fsm/src/sched_engine.c",
    "components/display_fsm/src/sched_json.c",
    "components/display_fsm/src/sched_ui.c",
    "components/display_fsm/src/sky_data.c",
    "components/display_fsm/src/astro_calc.c",
    "components/cjson/cjson/cJSON.c",
    "components/fonts/c_array/nunito_48.c",
    "tools/panel/settings_mock.c",
    "tools/panel/settings_http.c",
    "tools/panel/panel_main.c",
)
PANEL_OUT = REPO / "sdcard" / "www" / "panel"


## @brief Build the phone panel: the clock's LVGL settings screens compiled to WebAssembly
## (Emscripten, SDL2 canvas) into sdcard/www/panel/ (index.html, panel.js, panel.wasm).
## v2: the settings app over the sample backend; tools/panel headers.
## v3: the clock backend (settings_http; -sFETCH) beside the sample one.
## v4: exports panel_upload_done and ccall (the upload's answer).
## v5: gzipped twins of panel.js and panel.wasm (the clock sends them to browsers that take gzip).
## @req REQ-UI-002
## @version 5
@task
def panel_build(c):
    if not EMCC.exists():
        raise SystemExit(f"{EMCC} missing: install emsdk in ~/emsdk")
    lib = _lvgl_host_lib(c, PANEL_LVGL_DIR, cc=EMCC, ar=EMAR, flags=PANEL_FLAGS)
    PANEL_OUT.mkdir(parents=True, exist_ok=True)
    incs = " ".join(f"-I{d}" for d in LVGL_HOST_INCLUDES + PANEL_INCLUDES)
    srcs = " ".join(str(REPO / s) for s in PANEL_SRCS)
    compat = REPO / "tools" / "lvgl_host" / "stubs" / "host_compat.h"
    c.run(
        f"{EMCC} -std=gnu11 -O2 -w -include {compat} {LVGL_HOST_DEFINE} {PANEL_FLAGS} {incs} "
        f"{srcs} {lib} -sALLOW_MEMORY_GROWTH=1 -sINITIAL_MEMORY=33554432 -sFETCH=1 "
        f"-sEXPORTED_FUNCTIONS=_main,_panel_upload_done -sEXPORTED_RUNTIME_METHODS=ccall "
        f"-o {PANEL_OUT / 'panel.js'}"
    )
    (PANEL_OUT / "index.html").write_text(
        (REPO / "tools" / "panel" / "index.html").read_text()
    )
    for name in ("panel.js", "panel.wasm"):
        raw = (PANEL_OUT / name).read_bytes()
        (PANEL_OUT / f"{name}.gz").write_bytes(gzip.compress(raw, 9, mtime=0))
    for f in sorted(PANEL_OUT.iterdir()):
        print(f"{f.relative_to(REPO)}: {f.stat().st_size:,} B")


## @brief End-to-end test of the built phone panel against a stand-in clock (pairing by the
## QR code's link, the state load, an authenticated schedule save, a reused code refused).
## @req REQ-UI-005
## @version 1
@task
def panel_e2e(c):
    c.run(
        f"{sys.executable} {REPO / 'tools' / 'panel' / 'mock_clock.py'} "
        f"{REPO / 'sdcard' / 'www'} {TMP / 'panel_shots'}"
    )


## @brief Screenshot the built phone panel in headless Firefox (desktop and phone landscape),
## tapping through tools/panel/shots.json, into .claude/tmp/panel_shots/.
## @req REQ-UI-002
## @version 1
@task(help={"steps": "Steps JSON (default tools/panel/shots.json)"})
def panel_shots(c, steps=None):
    steps = steps or REPO / "tools" / "panel" / "shots.json"
    c.run(
        f"{sys.executable} {REPO / 'tools' / 'panel' / 'shots.py'} {PANEL_OUT} "
        f"{TMP / 'panel_shots'} {steps}"
    )


## @brief Render every theme in sdcard/themes with the firmware's own widgets and the real
## LVGL (host build), as PNGs in .claude/tmp/theme_preview/ — review before any OTA.
## v2: --decisions renders the open clock-text options.
## v3: --decisions removed (all decided).
## v4: links the settings app and the sample backend (tools/panel headers).
## @req REQ-TOOL-004
## @req REQ-UI-003
## @version 4
@task(
    help={
        "background": "LVGL path of the background behind the weather scene ('-': none)",
        "themes": "Comma-separated theme stems (default: every file in sdcard/themes)",
    }
)
def theme_preview(c, background="A:/backgrounds/winter_afternoon.png", themes=None):
    from PIL import Image

    lib = _lvgl_host_lib(c)
    out = TMP / "theme_preview"
    out.mkdir(parents=True, exist_ok=True)
    exe = out / "theme_preview"
    incs = " ".join(f"-I{d}" for d in LVGL_HOST_INCLUDES + (REPO / "tools" / "panel",))
    srcs = " ".join(str(REPO / s) for s in THEME_PREVIEW_SRCS)
    compat = REPO / "tools" / "lvgl_host" / "stubs" / "host_compat.h"
    c.run(
        f"gcc -std=gnu11 -O1 -g -w -include {compat} {LVGL_HOST_DEFINE} {incs} {srcs} {lib} -lm -o {exe}"
    )
    stems = (
        themes.split(",")
        if themes
        else sorted(p.stem for p in (REPO / "sdcard" / "themes").glob("*.json"))
    )
    files = " ".join(str(REPO / "sdcard" / "themes" / f"{s}.json") for s in stems)
    c.run(f"{exe} {out} {background} {files}")
    for ppm in sorted(out.glob("*.ppm")):
        Image.open(ppm).save(ppm.with_suffix(".png"))
        ppm.unlink()
        print(ppm.with_suffix(".png"))
