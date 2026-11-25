#!/usr/bin/env python3
## @file frame_capture.py
## @brief Host tool: record the clock's next frames (optionally across a forced state change) and write them as PNGs plus a labelled contact sheet.
## @version 1
"""
tools/frame_capture.py — Record rendered frames from the device and save them.

POST /api/debug/capture arms the device recorder (half-resolution RGB565 frames,
taken at each render). With --state the recording starts at that state change,
which this tool then forces through POST /api/display/state. GET
/api/debug/capture returns the frames once recorded.

Output: <out>/frame_NN.png and <out>/sheet.png (frames in order, each labelled
with its index, time since the first frame and the gap to the previous one).

Usage:
    frame_capture.py --host H --out DIR [--state weather] [--frames 16]
"""

import argparse
import json
import struct
import time
import urllib.error
import urllib.request

from device_http import device_urlopen
from pathlib import Path

from PIL import Image, ImageDraw

SHEET_COLS = 4


## v2: sends the admin token (device_http): the clock answers 401 without it.
## @brief Send one HTTP request to the device.
## @version 2
## @req REQ-TOOL-001
## @return (status, body bytes).
def request(host, path, method="GET", payload=None):
    data = None if payload is None else json.dumps(payload).encode()
    req = urllib.request.Request(f"http://{host}{path}", data=data, method=method)
    if data is not None:
        req.add_header("Content-Type", "application/json")
    with device_urlopen(req, timeout=60) as resp:
        return resp.status, resp.read()


## @brief Poll GET /api/debug/capture until the recording is ready.
## @version 1
## @req REQ-TOOL-001
## @return The raw recording bytes.
def wait_for_recording(host, timeout_s=20.0):
    deadline = time.monotonic() + timeout_s
    status, body = request(host, "/api/debug/capture")
    while status == 202 and time.monotonic() < deadline:
        time.sleep(0.5)
        status, body = request(host, "/api/debug/capture")
    if status != 200:
        raise SystemExit(f"recording not ready (HTTP {status})")
    return body


## @brief Split a recording into (timestamps, list of RGB images).
## @version 1
## @req REQ-TOOL-001
## @return (times in ms relative to the first frame, images).
def decode(body):
    nl = body.index(b"\n")
    count, w, h = (int(v) for v in body[:nl].split())
    off = nl + 1
    t_us = struct.unpack_from(f"<{count}q", body, off)
    off += 8 * count
    frame_bytes = w * h * 2
    images = []
    for i in range(count):
        raw = body[off + i * frame_bytes : off + (i + 1) * frame_bytes]
        images.append(Image.frombytes("RGB", (w, h), raw, "raw", "BGR;16"))
    times = [(t - t_us[0]) / 1000.0 for t in t_us]
    return times, images


## @brief Lay the frames out in a labelled grid.
## @version 1
## @req REQ-TOOL-001
## @return The contact sheet image.
def contact_sheet(times, images):
    w, h = images[0].size
    rows = (len(images) + SHEET_COLS - 1) // SHEET_COLS
    sheet = Image.new("RGB", (SHEET_COLS * w, rows * h), (255, 0, 255))
    draw = ImageDraw.Draw(sheet)
    for i, img in enumerate(images):
        x, y = (i % SHEET_COLS) * w, (i // SHEET_COLS) * h
        sheet.paste(img, (x, y))
        gap = times[i] - times[i - 1] if i else 0.0
        label = f"#{i} t={times[i]:.0f}ms +{gap:.0f}"
        draw.rectangle((x, y, x + 8 * len(label) + 8, y + 16), fill=(0, 0, 0))
        draw.text((x + 4, y + 2), label, fill=(255, 255, 0))
    return sheet


## @brief CLI entry: arm, optionally force a state, download, write PNGs and the sheet.
## @version 1
## @req REQ-TOOL-001
def main():
    ap = argparse.ArgumentParser(description="Record device frames to PNGs.")
    ap.add_argument("--host", default="greenwood-clock.local")
    ap.add_argument("--out", required=True)
    ap.add_argument("--state", help="force this display state; recording starts there")
    ap.add_argument("--frames", type=int, default=16)
    args = ap.parse_args()

    trigger = "transition" if args.state else "now"
    try:
        request(
            args.host,
            f"/api/debug/capture?frames={args.frames}&trigger={trigger}",
            "POST",
        )
    except urllib.error.HTTPError as exc:
        raise SystemExit(
            f"arm failed: HTTP {exc.code} {exc.read().decode(errors='replace')}"
        )
    if args.state:
        request(args.host, "/api/display/state", "POST", {"state": args.state})
    times, images = decode(wait_for_recording(args.host))
    if not images:
        raise SystemExit("recording has no frames (unsupported framebuffer?)")

    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    for i, img in enumerate(images):
        img.save(out / f"frame_{i:02d}.png")
    contact_sheet(times, images).save(out / "sheet.png")
    gaps = [b - a for a, b in zip(times, times[1:])]
    print(f"{len(images)} frames over {times[-1]:.0f} ms -> {out}")
    if gaps:
        print(
            f"frame gaps ms: min {min(gaps):.0f}  max {max(gaps):.0f}  "
            f"mean {sum(gaps) / len(gaps):.0f}"
        )


if __name__ == "__main__":
    main()
