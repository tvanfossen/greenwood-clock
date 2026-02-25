#!/usr/bin/env python3
"""
Download an arbitrary file from the SD card on the Greenwood Clock.

Endpoint: GET /files/<path>
Usage:    python tools/file_pull.py <remote-path> [<local-dest>] [--host greenwood-clock.local]

Example:
    python tools/file_pull.py logs/debug_log0.log
    python tools/file_pull.py wallpapers/bg.gif ./bg.gif

Note: For the active debug log use tools/log_download.py instead — it calls
      the /api/logs/download endpoint which pauses the write handle correctly.
"""

import argparse
import sys
import urllib.request
import os


def main() -> None:
    ap = argparse.ArgumentParser(description="Download a file from the device SD card.")
    ap.add_argument(
        "remote", help="Remote path on SD card (e.g. 'logs/debug_log0.log')"
    )
    ap.add_argument(
        "local", nargs="?", help="Local destination (default: basename of remote)"
    )
    ap.add_argument(
        "--host",
        default="greenwood-clock.local",
        help="Device hostname or IP (default: greenwood-clock.local)",
    )
    args = ap.parse_args()

    remote_path = args.remote.lstrip("/")
    url = f"http://{args.host}/files/{remote_path}"
    local_dest = args.local or os.path.basename(remote_path)

    print(f"Downloading {url} → {local_dest}", file=sys.stderr)
    try:
        with urllib.request.urlopen(url, timeout=60) as resp:
            data = resp.read()
        with open(local_dest, "wb") as f:
            f.write(data)
        print(f"Saved {len(data):,} bytes to {local_dest}")
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
