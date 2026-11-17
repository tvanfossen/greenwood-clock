#!/usr/bin/env python3
"""Render static forecast-strip icons from the weather Lottie set.

For each tools/lottie_gen/output/weather/{day,night}/<condition>.json this picks
the animation's most representative frame — the one with the most visible
content (max alpha coverage), so every element is fully on screen rather than
mid-fade — and writes a small RGBA PNG to sdcard/icons/weather/{day,night}/.
Ties (within 2% of the best coverage) go to the frame nearest mid-loop.

Usage:
    .venv/bin/inv forecast-icons                       # all icons, default size
    .venv/bin/inv forecast-icons --override day/rain=12
Then push with tools/deploy_sdcard.py (sdcard/icons is included).
"""

import argparse
import sys
from pathlib import Path

from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))
from lottie_render import LottieRenderer, coverage  # noqa: E402  (ThorVG, same engine as the device)

REPO = Path(__file__).resolve().parent.parent
SRC = REPO / "tools" / "lottie_gen" / "output" / "weather"
DST = REPO / "sdcard" / "icons" / "weather"
SAMPLES = 48
RENDER_PX = 256
TIE_TOLERANCE = 0.02


## @brief Pick the most representative frame index of a Lottie animation.
## @utility
## @version 1
## @return (frame index, coverage) of the chosen frame.
def pick_frame(renderer):
    total = int(renderer.total_frames)
    step = max(1, total // SAMPLES)
    scored = [
        (i, coverage(renderer.render(i))) for i in range(1, total, step)
    ]  # frame 0 never shown on device
    best = max(score for _, score in scored)
    near_best = [(i, s) for i, s in scored if s >= best * (1.0 - TIE_TOLERANCE)]
    return min(near_best, key=lambda item: abs(item[0] - total / 2))


## @brief Render one icon and save it as a size x size RGBA PNG.
## @utility
## @version 1
## @return Chosen frame index.
def render_icon(src, dst, size, frame=None):
    renderer = LottieRenderer(src, RENDER_PX, RENDER_PX)
    if frame is None:
        frame, _ = pick_frame(renderer)
    img = renderer.render(frame).resize((size, size), Image.LANCZOS)
    dst.parent.mkdir(parents=True, exist_ok=True)
    img.save(dst, "PNG", optimize=True)
    return frame


## @brief Parse "tod/name=frame" overrides into a dict.
## @utility
## @version 1
## @return {"day/rain": 12, ...}
def parse_overrides(items):
    out = {}
    for item in items:
        key, _, value = item.partition("=")
        out[key] = int(value)
    return out


## @brief Generate every icon plus a contact sheet for review.
## @utility
## @version 1
## @return Process exit code.
def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--size", type=int, default=64)
    ap.add_argument("--override", action="append", default=[], help="tod/name=frame")
    ap.add_argument("--sheet", type=Path, help="Write a contact-sheet PNG here")
    args = ap.parse_args()
    overrides = parse_overrides(args.override)
    rendered = []
    for src in sorted(SRC.glob("*/*.json")):
        key = f"{src.parent.name}/{src.stem}"
        dst = DST / src.parent.name / f"{src.stem}.png"
        frame = render_icon(src, dst, args.size, overrides.get(key))
        print(
            f"{key:24s} frame {frame:4d} -> {dst.relative_to(REPO)} ({dst.stat().st_size} B)"
        )
        rendered.append(dst)
    if args.sheet:
        write_sheet(rendered, args.size, args.sheet)
    return 0 if rendered else 1


## @brief Tile the icons on a dark panel (matches the forecast strip) for eyeballing.
## @utility
## @version 1
def write_sheet(paths, size, out):
    cols = 6
    rows = (len(paths) + cols - 1) // cols
    sheet = Image.new("RGBA", (cols * (size + 8), rows * (size + 8)), (26, 26, 46, 255))
    for n, p in enumerate(paths):
        icon = Image.open(p)
        sheet.alpha_composite(
            icon, ((n % cols) * (size + 8) + 4, (n // cols) * (size + 8) + 4)
        )
    out.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(out)
    print(f"contact sheet -> {out}")


if __name__ == "__main__":
    sys.exit(main())
