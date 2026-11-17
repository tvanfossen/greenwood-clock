#!/usr/bin/env python3
## @file lottie_audit.py
## @brief Host tool: render every frame of each Lottie with ThorVG and report load errors and blank frames.
## @version 1
"""Render EVERY frame of every Lottie file with ThorVG and report problems.

A file passes when it loads, every frame renders without error, and no frame
is blank (alpha coverage below BLANK_PX). Per-file stats: frame count,
min / median / max coverage, blank frame ranges.

Usage:
    .venv/bin/inv lottie-audit                 # pulls the device's copies first
    tools/lottie_audit.py DIR_OR_FILE [...]    # audit local files
"""

import argparse
import statistics
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from lottie_render import LottieRenderer, coverage  # noqa: E402

RENDER_PX = 96
BLANK_PX = 0.5


## @brief Collapse sorted frame numbers into "a-b" ranges.
## @dg_internal
## @version 1
## @return Comma-separated ranges string.
def ranges(frames):
    out, start, prev = [], None, None
    for f in frames:
        if start is None:
            start = prev = f
        elif f == prev + 1:
            prev = f
        else:
            out.append(f"{start}-{prev}" if prev != start else str(start))
            start = prev = f
    if start is not None:
        out.append(f"{start}-{prev}" if prev != start else str(start))
    return ",".join(out)


## @brief Render every frame of one file.
## @req REQ-TOOL-003
## @version 1
## @return dict with ok flag, frame count, coverage stats, blank frames, error text.
def audit_file(path):
    result = {"path": path, "ok": False, "frames": 0, "blank": [], "error": ""}
    try:
        r = LottieRenderer(path, RENDER_PX, RENDER_PX)
        total = int(r.total_frames)
        first = (
            1 if total > 1 else 0
        )  # lv_lottie starts at frame 1 (skips junk frame 0)
        covs = {f: coverage(r.render(f)) for f in range(first, total)}
    except (
        Exception
    ) as exc:  # report every failure mode; the audit must not stop on one file
        result["error"] = f"{type(exc).__name__}: {exc}"
        return result
    result.update(
        frames=total,
        blank=[f for f, c in covs.items() if c < BLANK_PX],
        cmin=min(covs.values()) if covs else 0.0,
        cmed=statistics.median(covs.values()) if covs else 0.0,
        cmax=max(covs.values()) if covs else 0.0,
    )
    result["ok"] = total > 0 and not result["blank"]
    return result


## @brief Expand CLI args into a sorted list of .json files.
## @dg_internal
## @version 1
## @return List of Paths.
def collect(args):
    files = []
    for a in map(Path, args):
        files.extend(sorted(a.rglob("*.json")) if a.is_dir() else [a])
    return files


## @brief Print one result row.
## @dg_internal
## @version 1
def report(res, root):
    name = str(res["path"].relative_to(root)) if root else str(res["path"])
    if res["error"]:
        print(f"FAIL  {name:44s} {res['error']}")
        return
    status = "OK  " if res["ok"] else "FAIL"
    blank = f" blank frames: {ranges(res['blank'])}" if res["blank"] else ""
    print(
        f"{status}  {name:44s} {res['frames']:4d} frames  coverage min/med/max "
        f"{res['cmin']:7.0f}/{res['cmed']:7.0f}/{res['cmax']:7.0f}{blank}"
    )


## @brief Audit all inputs; exit 1 if any file fails.
## @req REQ-TOOL-003
## @version 1
## @return Process exit code.
def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("paths", nargs="+")
    ap.add_argument("--root", type=Path, help="Print paths relative to this directory")
    args = ap.parse_args()
    files = collect(args.paths)
    results = [audit_file(f) for f in files]
    for res in results:
        report(res, args.root)
    failed = sum(not r["ok"] for r in results)
    print(f"\n{len(results) - failed}/{len(results)} files render on every frame")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
