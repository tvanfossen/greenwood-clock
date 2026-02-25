#!/usr/bin/env python3
"""
Trigger a remote reboot of the Greenwood Clock over HTTP.

Endpoint: POST /debug/reboot
Usage:    python tools/device_reboot.py [--host greenwood-clock.local]

The device sends the response before rebooting (500 ms delay), so this
script will receive a {"status":"rebooting"} confirmation.
"""

import argparse
import sys
import urllib.request


def main() -> None:
    ap = argparse.ArgumentParser(description="Trigger a remote reboot of the device.")
    ap.add_argument(
        "--host",
        default="greenwood-clock.local",
        help="Device hostname or IP (default: greenwood-clock.local)",
    )
    args = ap.parse_args()

    url = f"http://{args.host}/debug/reboot"
    print(f"Sending reboot request to {url}", file=sys.stderr)

    req = urllib.request.Request(url, data=b"", method="POST")
    try:
        with urllib.request.urlopen(req, timeout=5) as resp:
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
