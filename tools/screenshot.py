#!/usr/bin/env python3
"""
tools/screenshot.py — Capture the device's live framebuffer and save it as PNG.

Pulls GET /api/screenshot, which returns an ASCII header line
"w h stride cf\n" followed by the raw framebuffer pixels (the REAL on-screen
image, including any SW/PPA composite artifacts). Decodes RGB565 (cf=18) or
ARGB8888 (cf=16) and writes a PNG.

Usage:
    python tools/screenshot.py [--host 192.168.1.15] [-o screen.png]
"""

import argparse
import struct
import sys
import urllib.request

# LVGL color-format enum values we handle.
CF_RGB565 = 18
CF_ARGB8888 = 16
CF_XRGB8888 = 17


def rgb565_to_rgb(buf, w, h, stride):
    out = bytearray(w * h * 3)
    for y in range(h):
        row = y * stride
        for x in range(w):
            px = buf[row + x * 2] | (buf[row + x * 2 + 1] << 8)
            r = (px >> 11) & 0x1F
            g = (px >> 5) & 0x3F
            b = px & 0x1F
            o = (y * w + x) * 3
            out[o] = (r << 3) | (r >> 2)
            out[o + 1] = (g << 2) | (g >> 4)
            out[o + 2] = (b << 3) | (b >> 2)
    return bytes(out)


def argb8888_to_rgb(buf, w, h, stride):
    # LVGL ARGB8888 is BGRA in memory (little-endian).
    out = bytearray(w * h * 3)
    for y in range(h):
        row = y * stride
        for x in range(w):
            o = (y * w + x) * 3
            p = row + x * 4
            out[o] = buf[p + 2]  # R
            out[o + 1] = buf[p + 1]  # G
            out[o + 2] = buf[p]  # B
    return bytes(out)


def main():
    ap = argparse.ArgumentParser(description="Capture device framebuffer to PNG.")
    ap.add_argument("--host", default="greenwood-clock.local")
    ap.add_argument("-o", "--out", default="screen.png")
    args = ap.parse_args()

    url = f"http://{args.host}/api/screenshot"
    print(f"Fetching {url} ...")
    with urllib.request.urlopen(url, timeout=20) as r:
        data = r.read()

    nl = data.index(b"\n")
    w, h, stride, cf = (int(v) for v in data[:nl].split())
    pixels = data[nl + 1 :]
    print(f"  {w}x{h} stride={stride} cf={cf} ({len(pixels)} bytes)")

    if cf == CF_RGB565:
        rgb = rgb565_to_rgb(pixels, w, h, stride)
    elif cf in (CF_ARGB8888, CF_XRGB8888):
        rgb = argb8888_to_rgb(pixels, w, h, stride)
    else:
        print(f"Unsupported color format cf={cf}", file=sys.stderr)
        sys.exit(1)

    try:
        from PIL import Image

        Image.frombytes("RGB", (w, h), rgb).save(args.out)
    except ImportError:
        # Minimal PNG writer (no PIL): zlib-compressed truecolor.
        import zlib

        raw = bytearray()
        for y in range(h):
            raw.append(0)  # filter type 0
            raw.extend(rgb[y * w * 3 : (y + 1) * w * 3])

        def chunk(tag, payload):
            c = tag + payload
            return (
                struct.pack(">I", len(payload))
                + c
                + struct.pack(">I", zlib.crc32(c) & 0xFFFFFFFF)
            )

        png = b"\x89PNG\r\n\x1a\n"
        png += chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
        png += chunk(b"IDAT", zlib.compress(bytes(raw), 6))
        png += chunk(b"IEND", b"")
        with open(args.out, "wb") as f:
            f.write(png)

    print(f"Saved {args.out}")


if __name__ == "__main__":
    main()
