#!/usr/bin/env python3
"""Visual vet of Lottie animations: frame-to-frame jumps, loop seams, contact sheets.

For each file, renders every frame the device shows (frame 1..N-1; lv_lottie
skips frame 0) with ThorVG and measures the mean absolute RGBA difference
between consecutive frames. A smooth animation has consecutive diffs of similar
size; a glitch (a shape popping, a layer vanishing for a frame, a bad keyframe)
shows up as a spike far above that file's typical step. The loop seam is the
diff from the last frame back to frame 1 — it should look like any other step.

Output: a table, plus one contact sheet per file (every Nth frame, spikes and
the seam pair outlined in red) under --out for eyeballing.

Usage:
    .venv/bin/inv lottie-vet            # device copies pulled by `inv lottie-audit`
    tools/lottie_vet.py --out DIR PATH [PATH ...]
"""

import argparse
import statistics
import sys
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw

sys.path.insert(0, str(Path(__file__).resolve().parent))
from lottie_render import LottieRenderer  # noqa: E402

RENDER_PX = 128
POP_LEVEL = (
    48  # per-pixel change (0..255 luma of the RGBA diff) that is clearly visible
)
POP_AREA = 0.002  # ...over at least 0.2% of the frame in ONE frame step is visible, and
ANOMALY = 4.0  # ...more than 4x the file's own median step is anomalous (= a pop/jump).
# Visible alone flags legitimate fast motion (rain, confetti); anomalous alone flags invisible
# drift on near-static files. A pop must be both.
# Mean-difference spikes were tried first and flagged invisible global opacity drift on
# very smooth files (fog/aurora steps of ~2/255); a visible pop needs a localised large change.
SHEET_COLS = 10
SHEET_TILE = 96


## @brief Fraction of pixels whose RGBA difference exceeds POP_LEVEL between two frames.
## @utility
## @version 3
## @return Changed-area fraction (0..1).
def frame_diff(a, b):
    # Composite first: transparent pixels can carry arbitrary RGB in straight-alpha output.
    bg = Image.new("RGBA", a.size, (40, 40, 56, 255))
    d = ImageChops.difference(
        Image.alpha_composite(bg, a), Image.alpha_composite(bg, b)
    ).convert("L")
    hist = d.histogram()
    return sum(hist[POP_LEVEL:]) / float(a.width * a.height)


## @brief Render frames 1..N-1 of one file.
## @utility
## @version 1
## @return List of PIL images (index 0 is device frame 1).
def render_all(path):
    r = LottieRenderer(path, RENDER_PX, RENDER_PX)
    total = int(r.total_frames)
    return [r.render(f) for f in range(1 if total > 1 else 0, total)]


## @brief Visible-change area per frame step, at the loop seam, and the steps that pop.
## @utility
## @version 3
## @return dict with steps, median, max, seam, spikes, seam_bad.
def analyse(frames):
    steps = [frame_diff(frames[i], frames[i + 1]) for i in range(len(frames) - 1)]
    seam = frame_diff(frames[-1], frames[0]) if len(frames) > 1 else 0.0
    median = statistics.median(steps) if steps else 0.0
    limit = max(POP_AREA, ANOMALY * median)
    return {
        "steps": steps,
        "median": median,
        "max": max(steps) if steps else 0.0,
        "seam": seam,
        "spikes": [i for i, s in enumerate(steps) if s > limit],
        "seam_bad": seam > limit,
    }


## @brief Contact sheet: every k-th frame, with spike pairs and the seam pair outlined.
## @utility
## @version 1
def write_sheet(frames, res, out):
    marked = set()
    for i in res["spikes"]:
        marked.update((i, i + 1))
    if res["seam_bad"]:
        marked.update((0, len(frames) - 1))
    stride = max(1, len(frames) // (SHEET_COLS * 4))
    picks = sorted(set(range(0, len(frames), stride)) | marked)
    rows = (len(picks) + SHEET_COLS - 1) // SHEET_COLS
    sheet = Image.new(
        "RGBA", (SHEET_COLS * SHEET_TILE, rows * SHEET_TILE), (40, 40, 56, 255)
    )
    draw = ImageDraw.Draw(sheet)
    for n, idx in enumerate(picks):
        x, y = (n % SHEET_COLS) * SHEET_TILE, (n // SHEET_COLS) * SHEET_TILE
        sheet.alpha_composite(
            frames[idx].resize((SHEET_TILE - 8, SHEET_TILE - 8)), (x + 4, y + 4)
        )
        draw.text((x + 4, y + 2), str(idx + 1), fill=(255, 255, 255, 255))
        if idx in marked:
            draw.rectangle(
                [x + 1, y + 1, x + SHEET_TILE - 2, y + SHEET_TILE - 2],
                outline=(255, 40, 40, 255),
                width=2,
            )
    out.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(out)


## @brief One table row.
## @utility
## @version 2
## @return Row text.
def row(name, res):
    flag = "SPIKE" if res["spikes"] else ("SEAM " if res["seam_bad"] else "ok   ")
    spikes = ",".join(str(i + 1) + "->" + str(i + 2) for i in res["spikes"][:8])
    more = "…" if len(res["spikes"]) > 8 else ""
    return (
        f"{flag} {name:44s} visible-change area/step: med {100 * res['median']:5.2f}% "
        f"max {100 * res['max']:5.2f}%  seam {100 * res['seam']:5.2f}%  {spikes}{more}"
    )


## @brief Vet every input file; exit 1 if any spike or bad seam is found.
## @utility
## @version 1
## @return Process exit code.
def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("paths", nargs="+", type=Path)
    ap.add_argument("--out", type=Path, required=True, help="Contact-sheet directory")
    ap.add_argument("--root", type=Path, help="Print paths relative to this directory")
    args = ap.parse_args()
    files = [
        f
        for p in args.paths
        for f in (sorted(p.rglob("*.json")) if p.is_dir() else [p])
    ]
    flagged = 0
    for f in files:
        name = str(f.relative_to(args.root)) if args.root else f.name
        frames = render_all(f)
        res = analyse(frames)
        write_sheet(frames, res, args.out / (name.replace("/", "__") + ".png"))
        print(row(name, res))
        flagged += bool(res["spikes"] or res["seam_bad"])
    print(
        f"\n{len(files) - flagged}/{len(files)} files smooth with clean loop seams; sheets in {args.out}"
    )
    return 1 if flagged else 0


if __name__ == "__main__":
    sys.exit(main())
