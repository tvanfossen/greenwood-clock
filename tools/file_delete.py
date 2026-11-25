#!/usr/bin/env python3
## @file file_delete.py
## @brief Host tool: delete a file or empty directory on the device SD card via DELETE /files/<path>.
## @version 1
"""
Delete a file or empty directory from the SD card on the Greenwood Clock.

Endpoint: DELETE /files/<path>
Usage:    python tools/file_delete.py <remote-path> [--host greenwood-clock.local]

Example:
    python tools/file_delete.py logs/debug_log0.log
    python tools/file_delete.py wallpapers/old_bg.gif

Note: Directories must be empty. Recursive delete is intentionally unsupported
      to prevent accidental SD card wipes.
"""

import argparse
import sys
import urllib.request

from device_http import device_urlopen


## v2: sends the admin token (device_http): the clock answers 401 without it.
## @brief CLI entry: send DELETE /files/<remote> and print the HTTP reply; exit 1 on an HTTP or network error.
## @version 2
## @req REQ-TOOL-001
def main() -> None:
    ap = argparse.ArgumentParser(description="Delete a file from the device SD card.")
    ap.add_argument(
        "remote", help="Remote path on SD card to delete (e.g. 'logs/debug_log0.log')"
    )
    ap.add_argument(
        "--host",
        default="greenwood-clock.local",
        help="Device hostname or IP (default: greenwood-clock.local)",
    )
    args = ap.parse_args()

    remote_path = args.remote.lstrip("/")
    url = f"http://{args.host}/files/{remote_path}"

    print(f"Deleting {url}", file=sys.stderr)
    req = urllib.request.Request(url, method="DELETE")
    try:
        with device_urlopen(req, timeout=15) as resp:
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
