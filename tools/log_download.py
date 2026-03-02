#!/usr/bin/env python3
"""
Download debug log slot(s) from the Greenwood Clock.

Slot routing:
  Slot 0 (active):  GET /api/logs/download         — pauses write handle on device
  Slot 1 (prev):    GET /files/logs/debug_log1.log  — handle closed, direct stream
  Slot 2 (oldest):  GET /files/logs/debug_log2.log  — handle closed, direct stream

Usage:
    python tools/log_download.py [<local-dest>] [--host IP] [--slot 0|1|2] [--all]

Examples:
    python tools/log_download.py                           # slot 0 → debug_YYYYMMDD_HHMMSS.log
    python tools/log_download.py my.log                    # slot 0 → my.log
    python tools/log_download.py --slot 1                  # previous boot
    python tools/log_download.py --slot 2                  # two boots ago
    python tools/log_download.py --all --host 192.168.1.15 # all 3 slots with timestamped names
"""

import argparse
import sys
import urllib.request
import urllib.error
from datetime import datetime


def download_slot(host: str, slot: int, outfile: str) -> bool:
    """Download one log slot.  Returns True on success, False on 404 (slot absent)."""
    if slot == 0:
        url = f"http://{host}/api/logs/download"
    else:
        url = f"http://{host}/files/logs/debug_log{slot}.log"

    print(f"  Downloading slot {slot}: {url} → {outfile}", file=sys.stderr)
    try:
        with urllib.request.urlopen(url, timeout=120) as resp:
            data = resp.read()
    except urllib.error.HTTPError as exc:
        if exc.code == 404:
            print(f"  [SKIP] Slot {slot}: not present on device (404)", file=sys.stderr)
            return False
        body = exc.read().decode(errors="replace")
        print(f"  [ERROR] Slot {slot}: HTTP {exc.code} — {body}", file=sys.stderr)
        sys.exit(1)
    except Exception as exc:
        print(f"  [ERROR] Slot {slot}: {exc}", file=sys.stderr)
        sys.exit(1)

    with open(outfile, "wb") as f:
        f.write(data)
    print(f"  [slot {slot}] {len(data):,} bytes → {outfile}")
    return True


def main() -> None:
    ap = argparse.ArgumentParser(
        description="Download debug log slot(s) from the Greenwood Clock."
    )
    ap.add_argument(
        "local",
        nargs="?",
        help="Local destination filename for slot 0 (default: debug_YYYYMMDD_HHMMSS.log)",
    )
    ap.add_argument(
        "--host",
        default="greenwood-clock.local",
        help="Device hostname or IP (default: greenwood-clock.local)",
    )
    ap.add_argument(
        "--slot",
        type=int,
        choices=[0, 1, 2],
        default=None,
        help="Which log slot to download: 0=active, 1=previous boot, 2=two boots ago",
    )
    ap.add_argument(
        "--all",
        action="store_true",
        dest="all_slots",
        help="Download all three slots; auto-names outputs with timestamp",
    )
    args = ap.parse_args()

    if args.all_slots and args.slot is not None:
        ap.error("--all and --slot are mutually exclusive")

    ts = datetime.now().strftime("%Y%m%d_%H%M%S")

    if args.all_slots:
        print(f"Downloading all log slots from {args.host}", file=sys.stderr)
        for s in range(3):
            outfile = f"debug_log{s}_{ts}.log"
            download_slot(args.host, s, outfile)
    else:
        slot = args.slot if args.slot is not None else 0
        if slot == 0:
            outfile = args.local or f"debug_{ts}.log"
        else:
            outfile = args.local or f"debug_log{slot}_{ts}.log"
        download_slot(args.host, slot, outfile)


if __name__ == "__main__":
    main()
