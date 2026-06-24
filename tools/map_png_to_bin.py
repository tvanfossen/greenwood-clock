#!/usr/bin/env python3
"""Convert a map PNG into the raw BGRA binary the radar widget reads.

Output format (matches `radar_view.c` map.bin loader at line 101–219):
  [u16 width][u16 height][u32 stride][BGRA pixel data, stride bytes per row]

Pixel order is BGRA little-endian — this is the on-wire/in-memory
representation of LVGL ARGB8888 on a little-endian host. Stride is the
file's row stride; LVGL re-strides to its own 64-byte-aligned value
when loading.

Usage:
    python tools/map_png_to_bin.py sdcard/maps/local.png sdcard/maps/local.bin
"""

import argparse
import struct
import sys
from pathlib import Path

try:
    from PIL import Image
except ImportError:
    print(
        "ERROR: Pillow is required. Install with: pip install Pillow", file=sys.stderr
    )
    sys.exit(1)


def main() -> None:
    ap = argparse.ArgumentParser(description="PNG → raw BGRA map binary.")
    ap.add_argument("input_png", help="Source PNG (1024x600 expected)")
    ap.add_argument("output_bin", help="Destination .bin path")
    args = ap.parse_args()

    img = Image.open(args.input_png).convert("RGBA")
    w, h = img.size
    if w > 65535 or h > 65535:
        sys.exit(f"ERROR: dimensions too large for u16 header ({w}x{h})")
    stride = w * 4  # tight rows; loader copies to LVGL-padded stride

    out_path = Path(args.output_bin)
    out_path.parent.mkdir(parents=True, exist_ok=True)

    raw_rgba = img.tobytes()  # RGBA contiguous
    row = bytearray(stride)
    with out_path.open("wb") as f:
        f.write(struct.pack("<HHI", w, h, stride))
        for y in range(h):
            src_off = y * stride
            for x in range(w):
                r = raw_rgba[src_off + x * 4 + 0]
                g = raw_rgba[src_off + x * 4 + 1]
                b = raw_rgba[src_off + x * 4 + 2]
                a = raw_rgba[src_off + x * 4 + 3]
                d = x * 4
                row[d + 0] = b
                row[d + 1] = g
                row[d + 2] = r
                row[d + 3] = a
            f.write(row)

    total = 8 + stride * h
    print(f"Wrote {out_path} — {w}x{h}, stride={stride}, total={total} bytes")


if __name__ == "__main__":
    main()
