#!/usr/bin/env python3
"""
List a directory on the SD card of the Greenwood Clock.

Endpoint: GET /files/<path>  (returns JSON when path is a directory)
Usage:    python tools/file_list.py [<remote-dir>] [--host greenwood-clock.local]

Example:
    python tools/file_list.py
    python tools/file_list.py logs
"""

import argparse
import sys
import urllib.request
import json


def main() -> None:
    ap = argparse.ArgumentParser(description="List a directory on the device SD card.")
    ap.add_argument(
        "remote",
        nargs="?",
        default="",
        help="Remote directory path (default: root of SD card)",
    )
    ap.add_argument(
        "--host",
        default="greenwood-clock.local",
        help="Device hostname or IP (default: greenwood-clock.local)",
    )
    args = ap.parse_args()

    remote_path = args.remote.strip("/")
    url = (
        f"http://{args.host}/files/{remote_path}"
        if remote_path
        else f"http://{args.host}/files/"
    )

    try:
        with urllib.request.urlopen(url, timeout=15) as resp:
            data = json.loads(resp.read().decode())
    except urllib.request.HTTPError as exc:
        print(
            f"HTTP {exc.code}: {exc.read().decode(errors='replace')}", file=sys.stderr
        )
        sys.exit(1)
    except Exception as exc:
        print(f"Error: {exc}", file=sys.stderr)
        sys.exit(1)

    files = data.get("files", [])
    if not files:
        print("(empty directory)")
        return

    prefix = f"/{remote_path}/" if remote_path else "/"
    for entry in files:
        size_str = (
            f"{entry['size']:>10,} B" if entry["type"] == "file" else "         <dir>"
        )
        print(f"{size_str}  {prefix}{entry['name']}")


if __name__ == "__main__":
    main()
