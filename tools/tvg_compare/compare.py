#!/usr/bin/env python3
"""Compare ThorVG 0.15.3 (device) vs 1.x renders of the same Lottie files.

Inputs come from tvg_harness runs (see `inv tvg-compare`): per-version frame
dumps (<n>_<name>.rgba) and timing TSVs. Per file it reports frame counts, the
worst per-frame visible difference between engines (fraction of pixels whose
RGBA difference >= 48/255, the same threshold as tools/lottie_vet.py), and
mean per-frame render time for each engine.
"""

import argparse
import sys
from pathlib import Path

from PIL import Image, ImageChops

POP_LEVEL = 48


## @brief Parse a harness timing TSV into {file: (frames, failures, mean_us, max_us)}.
## @utility
## @version 1
## @return Mapping keyed by input path.
def read_tsv(path):
    out = {}
    for line in Path(path).read_text().splitlines():
        parts = line.split("\t")
        if len(parts) == 5:
            out[parts[0]] = (
                int(parts[1]),
                int(parts[2]),
                float(parts[3]),
                float(parts[4]),
            )
    return out


## @brief What a viewer sees: the frame composited over a fixed background.
## @utility
## @version 1
## @return Opaque RGB image.
def visible(img):
    # Straight-alpha output leaves arbitrary RGB in fully transparent pixels and each
    # ThorVG version leaves different values there; only composited pixels are comparable.
    bg = Image.new("RGBA", img.size, (40, 40, 56, 255))
    return Image.alpha_composite(bg, img).convert("RGB")


## @brief Worst per-frame visible-difference fraction between two frame dumps.
## @utility
## @version 2
## @return (worst fraction, frame index of worst, frames compared).
def worst_diff(a_path, b_path, size):
    fb = size * size * 4
    a, b = a_path.read_bytes(), b_path.read_bytes()
    n = min(len(a), len(b)) // fb
    worst, at = 0.0, -1
    for i in range(n):
        ia = visible(Image.frombytes("RGBA", (size, size), a[i * fb : (i + 1) * fb]))
        ib = visible(Image.frombytes("RGBA", (size, size), b[i * fb : (i + 1) * fb]))
        hist = ImageChops.difference(ia, ib).convert("L").histogram()
        frac = sum(hist[POP_LEVEL:]) / float(size * size)
        if frac > worst:
            worst, at = frac, i + 1
    return worst, at, n


## @brief Print the comparison table and totals.
## @utility
## @version 2
## @return Process exit code (1 if frame counts differ or any render failed).
def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument(
        "--old", type=Path, required=True, help="frames dir of the device version"
    )
    ap.add_argument(
        "--new", type=Path, required=True, help="frames dir of the new version"
    )
    ap.add_argument("--old-time", type=Path, required=True)
    ap.add_argument("--new-time", type=Path, required=True)
    ap.add_argument("--size", type=int, default=96)
    args = ap.parse_args()
    t_old, t_new = read_tsv(args.old_time), read_tsv(args.new_time)
    bad, sum_old, sum_new = 0, 0.0, 0.0
    print(
        f"{'file':42s} {'frames':>11s} {'worst diff':>16s} {'old us':>8s} {'new us':>8s} {'speedup':>7s}"
    )
    keys = list(t_old)  # TSV order == harness argv order (argv index = dump prefix - 3)
    for dump in sorted(
        args.old.glob("*.rgba"), key=lambda p: int(p.name.split("_", 1)[0])
    ):
        key = keys[int(dump.name.split("_", 1)[0]) - 3]
        fo, eo, mo, _ = t_old[key]
        fn, en, mn, _ = t_new[key]
        worst, at, _ = worst_diff(dump, args.new / dump.name, args.size)
        sum_old += mo * fo
        sum_new += mn * fn
        bad += (fo != fn) or eo or en
        label = "/".join(Path(key).parts[-3:])
        print(
            f"{label:42s} {fo:5d}/{fn:<5d} {100 * worst:6.2f}% @f{at:<5d} {mo:8.0f} {mn:8.0f} {mo / mn if mn else 0:6.2f}x"
        )
    print(
        f"\nTotal render time, all frames: old {sum_old / 1e6:.2f} s, new {sum_new / 1e6:.2f} s "
        f"({sum_old / sum_new:.2f}x)"
    )
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
