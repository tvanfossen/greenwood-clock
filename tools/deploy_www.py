#!/usr/bin/env python3
## @file deploy_www.py
## @brief Host tool: upload the web control panel (index.html, style.css, app.js) to /sdcard/www/ via POST /files/.
## @version 1
"""Deploy web control page files to the clock's SD card via HTTP API."""

import argparse
import sys
import urllib.request
from pathlib import Path


WWW_FILES = [
    ("sdcard/www/index.html", "www/index.html"),
    ("sdcard/www/style.css", "www/style.css"),
    ("sdcard/www/app.js", "www/app.js"),
]


## @brief POST one local file's bytes to /files/<remote_path> on the device and print the size and HTTP status.
## v2: drop the unused response-body variable (ruff F841); the response is still drained.
## @version 2
## @req REQ-TOOL-002
def push_file(host: str, local_path: str, remote_path: str) -> None:
    url = f"http://{host}/files/{remote_path}"
    with open(local_path, "rb") as f:
        data = f.read()

    req = urllib.request.Request(
        url,
        data=data,
        headers={"Content-Type": "application/octet-stream"},
        method="POST",
    )
    with urllib.request.urlopen(req, timeout=60) as resp:
        resp.read()  # drain the response before the connection closes
        print(f"  {len(data):>6,} bytes -> /sdcard/{remote_path}  ({resp.status})")


## @brief CLI entry: push every WWW_FILES entry to the device, skipping missing files; exit 1 if any file failed.
## @version 1
## @req REQ-TOOL-002
def main():
    parser = argparse.ArgumentParser(description="Deploy web UI to clock SD card")
    parser.add_argument(
        "--host",
        default="greenwood-clock.local",
        help="Device hostname or IP (default: greenwood-clock.local)",
    )
    args = parser.parse_args()

    project_root = Path(__file__).parent.parent
    ok = True

    print(f"Deploying to {args.host}...")
    for local_rel, remote_path in WWW_FILES:
        local = project_root / local_rel
        if not local.exists():
            print(f"  SKIP: {local_rel} (not found)")
            ok = False
            continue
        try:
            push_file(args.host, str(local), remote_path)
        except Exception as e:
            print(f"  ERROR: {local_rel} -> {e}")
            ok = False

    if ok:
        print(f"\nDone. Open http://{args.host}/www/index.html")
    else:
        print("\nSome files failed to deploy.")
        sys.exit(1)


if __name__ == "__main__":
    main()
