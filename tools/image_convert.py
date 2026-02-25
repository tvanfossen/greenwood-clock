#!/usr/bin/env python3
"""Convert an image to a display-optimised PNG for the Greenwood Clock (1024×600).

Display specs
  Resolution  : 1024 × 600
  Color depth : RGB565 (16-bit), handled by LVGL at runtime — no pre-conversion needed
  Image format: PNG via LodePNG (CONFIG_LV_USE_LODEPNG=y)
                NOTE: JPEG is NOT supported (CONFIG_LV_USE_TJPGD is not set)

Fit modes
  fill    (default) Scale source to cover the screen, center-crop the excess.
                    Subject stays large, no black bars.
  contain           Scale source to fit entirely within the screen, black bars
                    fill any remaining space.

Output
  Standard 24-bit RGB PNG at 1024×600.  LodePNG decodes it to RGB565 at runtime.
  Typical file size: 300–600 KB for photos (much smaller than a raw 1.2 MB RGB565 BIN).

After conversion, push to SD card with:
    python tools/file_push.py <output.png> --dest backgrounds/<output.png> --host <device-ip>
Then set the background path in clock settings to "A:/backgrounds/<output.png>".

Usage:
    python tools/image_convert.py spring-flower.jpg
    python tools/image_convert.py photo.png --output bg.png --fit contain
"""

import argparse
import sys
from pathlib import Path

TARGET_W = 1024
TARGET_H = 600


def convert(src: Path, dst: Path, fit: str) -> None:
    try:
        from PIL import Image
    except ImportError:
        sys.exit("Pillow is required: pip install Pillow")

    img = Image.open(src).convert("RGB")
    src_w, src_h = img.size
    print(f"Source : {src_w}×{src_h}  ({src.suffix.lstrip('.').upper()})")

    if fit == "fill":
        scale = max(TARGET_W / src_w, TARGET_H / src_h)
        scaled_w = round(src_w * scale)
        scaled_h = round(src_h * scale)
        img = img.resize((scaled_w, scaled_h), Image.LANCZOS)
        left = (scaled_w - TARGET_W) // 2
        top = (scaled_h - TARGET_H) // 2
        img = img.crop((left, top, left + TARGET_W, top + TARGET_H))
        print(
            f"Fit    : fill  (scale {scale:.3f}×, cropped {scaled_w-TARGET_W}px wide / {scaled_h-TARGET_H}px tall)"
        )
    else:  # contain
        scale = min(TARGET_W / src_w, TARGET_H / src_h)
        scaled_w = round(src_w * scale)
        scaled_h = round(src_h * scale)
        img = img.resize((scaled_w, scaled_h), Image.LANCZOS)
        canvas = Image.new("RGB", (TARGET_W, TARGET_H), (0, 0, 0))
        paste_x = (TARGET_W - scaled_w) // 2
        paste_y = (TARGET_H - scaled_h) // 2
        canvas.paste(img, (paste_x, paste_y))
        img = canvas
        print(
            f"Fit    : contain  (scale {scale:.3f}×, bars {paste_x}px left/right, {paste_y}px top/bottom)"
        )

    img.save(dst, "PNG", optimize=True)
    size_kb = dst.stat().st_size / 1024
    print(f"Output : {dst}  ({TARGET_W}×{TARGET_H}, {size_kb:.0f} KB)")


def main() -> None:
    ap = argparse.ArgumentParser(
        description="Convert an image to a 1024×600 PNG for the Greenwood Clock display."
    )
    ap.add_argument("input", type=Path, help="Source image (JPEG, PNG, BMP, …)")
    ap.add_argument(
        "--output",
        "-o",
        type=Path,
        default=None,
        help="Output PNG path (default: <input-stem>.converted.png)",
    )
    ap.add_argument(
        "--fit",
        choices=["fill", "contain"],
        default="fill",
        help="fill=scale+crop to cover screen (default); contain=scale to fit with black bars",
    )
    args = ap.parse_args()

    src: Path = args.input
    if not src.exists():
        sys.exit(f"File not found: {src}")

    dst: Path = args.output or src.with_name(src.stem + ".converted.png")
    if dst.suffix.lower() != ".png":
        sys.exit(
            "Output must be a .png file (LVGL on this device does not support JPEG)"
        )

    convert(src, dst, args.fit)


if __name__ == "__main__":
    main()
