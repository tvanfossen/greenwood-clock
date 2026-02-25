#!/usr/bin/env python3
"""
Download the active debug log from the Greenwood Clock.

Endpoint: GET /api/logs/download
Usage:    python tools/log_download.py [<local-dest>] [--host greenwood-clock.local]

This uses /api/logs/download (not /files/logs/debug_log*.log directly).
The device handler calls debug_log_pause_for_read() before opening the file,
which avoids the FAT32 single-handle conflict on the active log slot.

Example:
    python tools/log_download.py
    python tools/log_download.py debug_$(date +%Y%m%d_%H%M%S).log
"""

import argparse
import sys
import urllib.request
from datetime import datetime


def main() -> None:
    ap = argparse.ArgumentParser(
        description="Download the active debug log from the device."
    )
    ap.add_argument(
        "local",
        nargs="?",
        help="Local destination filename (default: debug_YYYYMMDD_HHMMSS.log)",
    )
    ap.add_argument(
        "--host",
        default="greenwood-clock.local",
        help="Device hostname or IP (default: greenwood-clock.local)",
    )
    args = ap.parse_args()

    url = f"http://{args.host}/api/logs/download"
    local_dest = args.local or f"debug_{datetime.now().strftime('%Y%m%d_%H%M%S')}.log"

    print(f"Downloading active log from {url} → {local_dest}", file=sys.stderr)
    try:
        with urllib.request.urlopen(url, timeout=120) as resp:
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
