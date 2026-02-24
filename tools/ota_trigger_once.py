#!/usr/bin/env python3
"""
tools/ota_trigger_once.py — One-time helper: serve new firmware for the
device's LEGACY pull-OTA mechanism (old firmware, pre-push-OTA).

Run this after building, then on the device:
  Settings → Software Update → enter URL → press "Check for Update"

After the device reboots with new firmware, use tools/ota_push.py instead.

Usage:
    python tools/ota_trigger_once.py [--port PORT] [--firmware PATH]
"""

import argparse
import http.server
import os
import socket
import socketserver
import sys

DEFAULT_PORT = 8000
DEFAULT_FIRMWARE = "build/greenwood-clock.bin"
SERVE_PATH = "/greenwood-clock.bin"
CHUNK_SIZE = 4096


def get_local_ip() -> str:
    try:
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
            s.connect(("8.8.8.8", 80))
            return s.getsockname()[0]
    except Exception:  # noqa: BLE001
        return "127.0.0.1"


class FirmwareHandler(http.server.BaseHTTPRequestHandler):
    """Serve a single firmware binary at SERVE_PATH (GET and HEAD)."""

    firmware_path: str = DEFAULT_FIRMWARE

    def _send_firmware_headers(self, size: int) -> None:
        self.send_response(200)
        self.send_header("Content-Type", "application/octet-stream")
        self.send_header("Content-Length", str(size))
        self.send_header(
            "Content-Disposition", 'attachment; filename="greenwood-clock.bin"'
        )
        self.end_headers()

    def do_GET(self) -> None:  # noqa: N802
        if self.path != SERVE_PATH:
            self.send_error(404)
            return
        if not os.path.exists(self.firmware_path):
            self.send_error(404, "Firmware not found — run idf.py build first")
            return
        size = os.path.getsize(self.firmware_path)
        self._send_firmware_headers(size)
        sent = 0
        with open(self.firmware_path, "rb") as f:
            while chunk := f.read(CHUNK_SIZE):
                self.wfile.write(chunk)
                sent += len(chunk)
        print(f"  [SENT] {sent:,} bytes to {self.client_address[0]}")

    def do_HEAD(self) -> None:  # noqa: N802
        if self.path == SERVE_PATH and os.path.exists(self.firmware_path):
            self._send_firmware_headers(os.path.getsize(self.firmware_path))
        else:
            self.send_error(404)

    def log_message(self, fmt: str, *args: object) -> None:
        print(f"  [{self.client_address[0]}] {fmt % args}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.strip().splitlines()[0])
    parser.add_argument("--port", type=int, default=DEFAULT_PORT)
    parser.add_argument("--firmware", default=DEFAULT_FIRMWARE)
    args = parser.parse_args()

    # Resolve paths relative to project root
    script_dir = os.path.dirname(os.path.abspath(__file__))
    project_root = os.path.dirname(script_dir)
    os.chdir(project_root)

    firmware = os.path.abspath(args.firmware)
    if not os.path.exists(firmware):
        print(f"[ERROR] Firmware not found: {firmware}")
        print("  Build first:  idf.py build")
        sys.exit(1)

    FirmwareHandler.firmware_path = firmware
    ip = get_local_ip()
    base_url = f"http://{ip}:{args.port}"

    print("=" * 60)
    print("One-time OTA trigger — legacy pull-OTA server")
    print("=" * 60)
    print(f"  Firmware : {firmware}")
    print(f"  Size     : {os.path.getsize(firmware):,} bytes")
    print(f"  URL      : {base_url}")
    print()
    print("On the device:")
    print("  1. Settings  →  Software Update")
    print(f"  2. Enter URL : {base_url}")
    print("  3. Press     : Check for Update")
    print()
    print("Device will download, flash, and reboot automatically.")
    print("After reboot, use  tools/ota_push.py  for future updates.")
    print()
    print("Press Ctrl+C to stop")
    print("=" * 60)

    try:
        with socketserver.TCPServer(("", args.port), FirmwareHandler) as httpd:
            httpd.serve_forever()
    except KeyboardInterrupt:
        print("\n[INFO] Server stopped")
        sys.exit(0)
    except OSError as exc:
        print(f"\n[ERROR] {exc}")
        sys.exit(1)


if __name__ == "__main__":
    main()
