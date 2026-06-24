#!/usr/bin/env python3
"""
Sync all SD card content from the repo to the device.

Walks a manifest of (local_dir → remote_prefix) mappings plus individual
files.  Pushes every file via POST /files/<path>.  Subsumes deploy_www.py.

Usage:
    python tools/deploy_sdcard.py [--host greenwood-clock.local] [--dry-run]
"""

import argparse
import sys
import urllib.request
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parent.parent

# (local_dir_relative_to_project, remote_prefix_on_sdcard)
DIR_MAPPINGS = [
    ("sdcard/backgrounds", "backgrounds"),
    ("sdcard/www", "www"),
    ("sdcard/maps", "maps"),
    ("tools/lottie_gen/output/weather", "lottie/weather"),
    ("tools/lottie_gen/output/astro", "lottie/astro"),
    ("tools/lottie_gen/output/surprise", "lottie/surprise"),
    ("tools/lottie_gen/output/ui", "lottie/ui"),
    ("tools/lottie_gen/output/ambient", "lottie/ambient"),
]

# (local_path_relative_to_project, remote_path_on_sdcard)
FILE_MAPPINGS = [
    ("components/lottie/json/hummingbird.json", "lottie/hummingbird.json"),
]


def collect_files():
    """Yield (local_abs_path, remote_path) for every file to deploy."""
    for local_dir_rel, remote_prefix in DIR_MAPPINGS:
        local_dir = PROJECT_ROOT / local_dir_rel
        if not local_dir.is_dir():
            print(f"  WARN: source dir missing: {local_dir_rel}", file=sys.stderr)
            continue
        for path in sorted(local_dir.rglob("*")):
            if not path.is_file():
                continue
            rel = path.relative_to(local_dir)
            yield path, f"{remote_prefix}/{rel}"

    for local_rel, remote_path in FILE_MAPPINGS:
        local = PROJECT_ROOT / local_rel
        if local.is_file():
            yield local, remote_path
        else:
            print(f"  WARN: source file missing: {local_rel}", file=sys.stderr)


def push_file(host, local_path, remote_path):
    """Upload a single file. Returns (bytes_sent, http_status)."""
    url = f"http://{host}/files/{remote_path}"
    data = local_path.read_bytes()
    req = urllib.request.Request(
        url,
        data=data,
        headers={"Content-Type": "application/octet-stream"},
        method="POST",
    )
    with urllib.request.urlopen(req, timeout=60) as resp:
        resp.read()
        return len(data), resp.status


def main():
    ap = argparse.ArgumentParser(description="Sync all SD card content to device")
    ap.add_argument(
        "--host",
        default="greenwood-clock.local",
        help="Device hostname or IP (default: greenwood-clock.local)",
    )
    ap.add_argument(
        "--dry-run",
        action="store_true",
        help="List files without uploading",
    )
    args = ap.parse_args()

    files = list(collect_files())
    if not files:
        print("Nothing to deploy.")
        sys.exit(1)

    total_bytes = 0
    errors = 0

    print(f"Deploying {len(files)} files to {args.host}...")
    for local_path, remote_path in files:
        size = local_path.stat().st_size
        if args.dry_run:
            print(f"  {size:>8,} B  /sdcard/{remote_path}")
            total_bytes += size
            continue
        try:
            sent, status = push_file(args.host, local_path, remote_path)
            print(f"  {sent:>8,} B  /sdcard/{remote_path}  ({status})")
            total_bytes += sent
        except urllib.request.HTTPError as exc:
            body = exc.read().decode(errors="replace").strip()
            print(f"  ERROR  /sdcard/{remote_path}: HTTP {exc.code} {body}")
            errors += 1
        except Exception as exc:
            print(f"  ERROR  /sdcard/{remote_path}: {exc}")
            errors += 1

    print(f"\n{len(files) - errors}/{len(files)} files, {total_bytes:,} bytes total")
    if errors:
        print(f"{errors} error(s)")
        sys.exit(1)


if __name__ == "__main__":
    main()
