#!/usr/bin/env python3
"""
tools/ota_push.py — Push OTA firmware to a Greenwood Clock device.

Discovers the device via mDNS (greenwood-clock.local) or an explicit --host,
streams the firmware binary to POST /ota, and polls GET /ota/status for live
state transitions.

Usage:
    python tools/ota_push.py [options] <firmware.bin>

Options:
    --host HOST      Device IP or hostname (default: greenwood-clock.local)
    --port PORT      Device HTTP port (default: 80)
    --token TOKEN    OTA API token (overrides OTA_API_TOKEN env / .env)
    --timeout SECS   Max seconds to wait for flash + reboot (default: 120)

Environment / .env:
    OTA_API_TOKEN    Bearer token checked against secrets.h OTA_API_TOKEN

Examples:
    OTA_API_TOKEN=secret python tools/ota_push.py build/greenwood-clock.bin
    python tools/ota_push.py --host 192.168.1.100 build/greenwood-clock.bin
"""

import argparse
import http.client
import json
import os
import socket
import sys
import threading
import time

# ──────────────────────────────────────────────────────────────────────────────
# Constants
# ──────────────────────────────────────────────────────────────────────────────

DEFAULT_HOST = "greenwood-clock.local"
DEFAULT_PORT = 80
DEFAULT_TIMEOUT = 120
UPLOAD_CHUNK = 16 * 1024  # 16 KB upload chunks
POLL_INTERVAL = 1.0  # seconds between status polls
POLL_CONNECT_TIMEOUT = 3  # seconds for status connection

# Terminal colours (degrade gracefully if not a TTY)
_TTY = sys.stdout.isatty()
GREEN = "\033[32m" if _TTY else ""
RED = "\033[31m" if _TTY else ""
YELLOW = "\033[33m" if _TTY else ""
RESET = "\033[0m" if _TTY else ""
ERASE_LINE = "\033[2K\r" if _TTY else "\n"


# ──────────────────────────────────────────────────────────────────────────────
# Token resolution
# ──────────────────────────────────────────────────────────────────────────────


def _load_dotenv(path: str) -> dict:
    """Return key=value pairs from a .env file, ignoring comments and blanks."""
    result = {}
    if not os.path.exists(path):
        return result
    with open(path) as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#") or "=" not in line:
                continue
            key, _, val = line.partition("=")
            result[key.strip()] = val.strip().strip("\"'")
    return result


def resolve_token(cli_token: str | None) -> str:
    """Return the OTA API token from CLI arg, env, or .env file."""
    if cli_token:
        return cli_token
    token = os.environ.get("OTA_API_TOKEN")
    if token:
        return token
    dotenv = _load_dotenv(os.path.join(os.path.dirname(__file__), "..", ".env"))
    token = dotenv.get("OTA_API_TOKEN")
    if token:
        return token
    print(f"{RED}[ERROR] OTA_API_TOKEN not set.{RESET}")
    print("  Set the environment variable, add it to .env, or pass --token.")
    sys.exit(1)


# ──────────────────────────────────────────────────────────────────────────────
# Host resolution
# ──────────────────────────────────────────────────────────────────────────────


def resolve_host(host: str) -> str:
    """Resolve host to an IP address; exit on failure."""
    try:
        ip = socket.gethostbyname(host)
        if ip != host:
            print(f"  Resolved {host} → {ip}")
        return ip
    except socket.gaierror as exc:
        print(f"{RED}[ERROR] Cannot resolve '{host}': {exc}{RESET}")
        print("  Is the device on the local network?")
        sys.exit(1)


# ──────────────────────────────────────────────────────────────────────────────
# Progress bar
# ──────────────────────────────────────────────────────────────────────────────

BAR_WIDTH = 40


def _render_bar(done: int, total: int) -> str:
    pct = done / total if total else 0
    filled = int(BAR_WIDTH * pct)
    bar = "█" * filled + "░" * (BAR_WIDTH - filled)
    done_mb = done / (1024 * 1024)
    total_mb = total / (1024 * 1024)
    return f"  [{bar}] {pct:5.1%}  {done_mb:.2f}/{total_mb:.2f} MB"


def print_progress(done: int, total: int) -> None:
    sys.stdout.write(ERASE_LINE + _render_bar(done, total))
    sys.stdout.flush()


# ──────────────────────────────────────────────────────────────────────────────
# Status poller (background thread)
# ──────────────────────────────────────────────────────────────────────────────

TERMINAL_STATES = {"rebooting", "error"}


class StatusPoller(threading.Thread):
    """Polls GET /ota/status and stores the last observed state."""

    def __init__(self, host: str, port: int, stop_event: threading.Event) -> None:
        super().__init__(daemon=True)
        self.host = host
        self.port = port
        self.stop_event = stop_event
        self.last_state: str = "idle"
        self.last_status: dict = {}
        self._prev_state: str = ""

    def _fetch_status(self) -> dict | None:
        try:
            conn = http.client.HTTPConnection(
                self.host, self.port, timeout=POLL_CONNECT_TIMEOUT
            )
            conn.request("GET", "/ota/status")
            resp = conn.getresponse()
            body = resp.read().decode()
            conn.close()
            if resp.status == 200:
                return json.loads(body)
        except Exception:  # noqa: BLE001
            pass
        return None

    def _print_state_change(self, state: str, status: dict) -> None:
        if state == self._prev_state:
            return
        self._prev_state = state
        colour = (
            GREEN if state == "rebooting" else (RED if state == "error" else YELLOW)
        )
        msg = f"\n  {colour}[OTA]{RESET} state → {state}"
        if state == "receiving" and status.get("total_bytes"):
            msg += f"  ({status['total_bytes']:,} bytes expected)"
        if state == "error" and status.get("error"):
            msg += f"  — {status['error']}"
        print(msg)

    def run(self) -> None:
        while not self.stop_event.is_set():
            status = self._fetch_status()
            if status:
                self.last_state = status.get("state", "idle")
                self.last_status = status
                self._print_state_change(self.last_state, status)
                if self.last_state in TERMINAL_STATES:
                    break
            time.sleep(POLL_INTERVAL)


# ──────────────────────────────────────────────────────────────────────────────
# OTA upload
# ──────────────────────────────────────────────────────────────────────────────


def stream_firmware(
    host: str, port: int, firmware_path: str, token: str, timeout: int
) -> tuple[int, str]:
    """Stream firmware to POST /ota.  Returns (http_status, response_body)."""
    file_size = os.path.getsize(firmware_path)
    conn = http.client.HTTPConnection(host, port, timeout=timeout)
    conn.putrequest("POST", "/ota")
    conn.putheader("Authorization", f"Bearer {token}")
    conn.putheader("Content-Type", "application/octet-stream")
    conn.putheader("Content-Length", str(file_size))
    conn.endheaders()

    sent = 0
    with open(firmware_path, "rb") as f:
        while True:
            chunk = f.read(UPLOAD_CHUNK)
            if not chunk:
                break
            conn.send(chunk)
            sent += len(chunk)
            print_progress(sent, file_size)

    sys.stdout.write("\n")
    sys.stdout.flush()

    try:
        resp = conn.getresponse()
        body = resp.read().decode()
        conn.close()
        return resp.status, body
    except Exception:  # noqa: BLE001
        # Device may reboot before the response arrives — treat as ambiguous
        return 0, ""


# ──────────────────────────────────────────────────────────────────────────────
# Main
# ──────────────────────────────────────────────────────────────────────────────


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Push OTA firmware to a Greenwood Clock device",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument(
        "firmware", help="Path to firmware binary (e.g. build/greenwood-clock.bin)"
    )
    parser.add_argument(
        "--host", default=DEFAULT_HOST, help=f"Device host/IP (default: {DEFAULT_HOST})"
    )
    parser.add_argument(
        "--port",
        type=int,
        default=DEFAULT_PORT,
        help=f"Device HTTP port (default: {DEFAULT_PORT})",
    )
    parser.add_argument(
        "--token", default=None, help="OTA API token (overrides OTA_API_TOKEN env)"
    )
    parser.add_argument(
        "--timeout",
        type=int,
        default=DEFAULT_TIMEOUT,
        help=f"Upload+flash timeout in seconds (default: {DEFAULT_TIMEOUT})",
    )
    return parser.parse_args()


def validate_firmware(path: str) -> int:
    """Return file size or exit with error."""
    if not os.path.exists(path):
        print(f"{RED}[ERROR] Firmware not found: {path}{RESET}")
        sys.exit(1)
    size = os.path.getsize(path)
    if size == 0:
        print(f"{RED}[ERROR] Firmware file is empty: {path}{RESET}")
        sys.exit(1)
    return size


def evaluate_result(http_status: int, body: str, poller: StatusPoller) -> bool:
    """Return True on success, False on failure."""
    # Explicit success: device sent {"status":"rebooting"} before restarting
    if http_status == 200 and "rebooting" in body:
        return True
    # Explicit failure: server returned an error
    if http_status >= 400:
        print(
            f"\n{RED}[ERROR] Server returned HTTP {http_status}: {body.strip()}{RESET}"
        )
        return False
    # Ambiguous: connection dropped — use status poller state
    if poller.last_state == "rebooting":
        return True
    if poller.last_state == "error":
        print(
            f"\n{RED}[ERROR] OTA state is ERROR: {poller.last_status.get('error', '')}{RESET}"
        )
        return False
    # Connection dropped mid-upload or other unexpected state
    print(
        f"\n{RED}[ERROR] Unexpected outcome — HTTP {http_status}, last state: {poller.last_state}{RESET}"
    )
    return False


def main() -> None:
    args = parse_args()
    token = resolve_token(args.token)
    file_size = validate_firmware(args.firmware)

    print("=" * 60)
    print("Greenwood Clock OTA Push")
    print("=" * 60)
    print(f"  Firmware : {args.firmware} ({file_size:,} bytes)")
    print(f"  Target   : {args.host}:{args.port}")
    print(f"  Timeout  : {args.timeout}s")
    print("=" * 60)

    ip = resolve_host(args.host)
    print()
    print("Starting status poller...")

    stop_event = threading.Event()
    poller = StatusPoller(ip, args.port, stop_event)
    poller.start()

    print("Uploading firmware...")
    t_start = time.monotonic()
    try:
        http_status, body = stream_firmware(
            ip, args.port, args.firmware, token, args.timeout
        )
    except OSError as exc:
        print(f"\n{RED}[ERROR] Upload failed: {exc}{RESET}")
        stop_event.set()
        sys.exit(1)

    elapsed = time.monotonic() - t_start
    stop_event.set()
    poller.join(timeout=3.0)

    success = evaluate_result(http_status, body, poller)

    print()
    if success:
        print(
            f"{GREEN}[OK] OTA complete in {elapsed:.1f}s — device is rebooting.{RESET}"
        )
        sys.exit(0)
    else:
        print(f"{RED}[FAIL] OTA failed after {elapsed:.1f}s.{RESET}")
        sys.exit(1)


if __name__ == "__main__":
    main()
