#!/usr/bin/env python3
"""
convert_png_to_c.py

Scans a source‐PNG directory and emits LVGL C‐array files (RGB565) into a target directory.
"""

import os
import sys
from pathlib import Path
from LVGLImage import LVGLImage, ColorFormat, CompressMethod

def convert_all(base_dir: Path, out_dir: Path, color_format=ColorFormat.RGB565A8):
    base_dir = base_dir.expanduser().resolve()
    out_dir = out_dir.expanduser().resolve()
    if not base_dir.is_dir():
        print(f"Error: source directory {base_dir} does not exist", file=sys.stderr)
        sys.exit(1)
    out_dir.mkdir(parents=True, exist_ok=True)

    pngs = list(base_dir.glob("*.png"))
    if not pngs:
        print(f"No PNGs found in {base_dir}", file=sys.stderr)
        return

    for png in pngs:
        img = LVGLImage().from_png(
            str(png),
            cf=color_format,
            background=0xffffff,
            rgb565_dither=True
        )
        out_file = out_dir / f"{png.stem}.c"
        img.to_c_array(str(out_file), compress=CompressMethod.NONE)
        print(f"Converted {png.name} → {out_file.name}")

def main():
    import argparse
    p = argparse.ArgumentParser(
        description="Convert PNGs to LVGL RGB565 C‐arrays"
    )
    p.add_argument("src_dir", help="directory containing .png files")
    p.add_argument("dst_dir", help="output directory for .c files")
    args = p.parse_args()

    convert_all(Path(args.src_dir), Path(args.dst_dir))

if __name__ == "__main__":
    main()
