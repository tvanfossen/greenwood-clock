#!/usr/bin/env python3
"""
Upload a file to the SD card on the Greenwood Clock.

Endpoint: POST /files/<path>  (body: raw file bytes)
Usage:    python tools/file_push.py <local-file> <remote-path> [--host greenwood-clock.local]

Example:
    python tools/file_push.py background.gif wallpapers/background.gif
    # uploads to /sdcard/wallpapers/background.gif on the device
"""

import argparse
import sys
import urllib.request


def main() -> None:
    ap = argparse.ArgumentParser(description="Upload a file to the device SD card.")
    ap.add_argument("local", help="Local file path to upload")
    ap.add_argument(
        "remote", help="Destination path on SD card (e.g. 'wallpapers/bg.gif')"
    )
    ap.add_argument(
        "--host",
        default="greenwood-clock.local",
        help="Device hostname or IP (default: greenwood-clock.local)",
    )
    args = ap.parse_args()

    remote_path = args.remote.lstrip("/")
    url = f"http://{args.host}/files/{remote_path}"

    try:
        with open(args.local, "rb") as f:
            data = f.read()
    except OSError as exc:
        print(f"Error reading {args.local}: {exc}", file=sys.stderr)
        sys.exit(1)

    print(f"Uploading {args.local} ({len(data):,} bytes) → {url}", file=sys.stderr)
    req = urllib.request.Request(
        url,
        data=data,
        headers={"Content-Type": "application/octet-stream"},
        method="POST",
    )
    try:
        with urllib.request.urlopen(req, timeout=60) as resp:
            body = resp.read().decode(errors="replace")
            print(f"HTTP {resp.status}: {body.strip()}")
    except urllib.request.HTTPError as exc:
        print(
            f"HTTP {exc.code}: {exc.read().decode(errors='replace')}", file=sys.stderr
        )
        sys.exit(1)
    except Exception as exc:
        print(f"Error: {exc}", file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()
