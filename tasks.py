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
HOST_TESTS = {
    "snow_physics": ("snow_physics",),
    "leaf_physics": ("leaf_physics",),
    "leaf_field": ("leaf_field", "leaf_physics"),
}

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


## @brief Perform an HTTP request against the device and return the body.
## @utility
## @version 2
## @return Response body bytes, or None when the device answered with an HTTP error.
def _request(host, path, auth=False, method="GET", payload=None):
    url = f"http://{host}{path}"
    data = None if payload is None else json.dumps(payload).encode()
    req = urllib.request.Request(url, data=data, method=method)
    if data is not None:
        req.add_header("Content-Type", "application/json")
    if auth:
        req.add_header("Authorization", f"Bearer {_api_token()}")
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


## @brief Select the clock-screen animation: hummingbird, snowfall, leaves or none.
## v2: help lists every animation; short names (leaves) map to the firmware display names.
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


## @brief Upload sdcard/icons/** (forecast icons) to the device SD card.
## @req REQ-TOOL-002
## @req REQ-WX-006
## @version 1
@task(help={"host": "Device host or IP"})
def push_icons(c, host=DEFAULT_HOST):
    staging = REPO / "sdcard"
    for src in sorted((staging / "icons").rglob("*.png")):
        remote = src.relative_to(staging).as_posix()
        c.run(
            f"{sys.executable} {REPO / 'tools' / 'file_push.py'} {src} {remote} --host {host}"
        )


## @brief Build and run the host-side C unit tests (pure-logic modules, no ESP-IDF).
## @req REQ-TOOL-004
## @version 2
@task
def test_host(c):
    out = TMP / "host_tests"
    out.mkdir(parents=True, exist_ok=True)
    inc = REPO / "components" / "display_fsm" / "include"
    for name, modules in HOST_TESTS.items():
        srcs = " ".join(
            str(REPO / "components" / "display_fsm" / "src" / f"{m}.c") for m in modules
        )
        test = REPO / "tests" / "host" / f"test_{name}.c"
        exe = out / f"test_{name}"
        c.run(
            f"gcc -std=gnu11 -Wall -Wextra -Werror -O1 -I{inc} {srcs} {test} -lm -o {exe}"
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
