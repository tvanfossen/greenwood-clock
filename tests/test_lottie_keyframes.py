## @file test_lottie_keyframes.py
## @brief Structural test: every animated property in the generated Lottie set has strictly increasing keyframe times.
## v2: docstring no longer claims a loop-seam check it does not perform.
## @version 2
"""Structural tests for generated Lottie files.

A Lottie player interpolates between consecutive keyframes in time order; a
keyframe whose time goes backwards makes the value jump (seen as the cloud
'jump' at frame 199->200 in partly_cloudy). This test checks ONE property: every
animated property has strictly increasing keyframe times. It does not check that
loops end where they start; loop seams are checked visually by `inv lottie-vet`.

Run: .venv/bin/inv test-lottie
"""

import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent / "tools" / "lottie_gen" / "output"


## @brief Yield (json_path, keyframe_list) for every animated property in a Lottie tree.
## @req REQ-LOTTIE-002
## @version 1
def animated_props(node, path="$"):
    if isinstance(node, dict):
        if node.get("a") == 1 and isinstance(node.get("k"), list):
            yield path, node["k"]
        for key, value in node.items():
            yield from animated_props(value, f"{path}.{key}")
    elif isinstance(node, list):
        for i, value in enumerate(node):
            yield from animated_props(value, f"{path}[{i}]")


## @brief Problems in one file: non-increasing keyframe times.
## @req REQ-LOTTIE-002
## @version 1
## @return List of human-readable problem strings.
def check_file(path):
    data = json.loads(path.read_text())
    problems = []
    for prop, kfs in animated_props(data):
        times = [k.get("t") for k in kfs if isinstance(k, dict) and "t" in k]
        bad = [i for i in range(1, len(times)) if times[i] <= times[i - 1]]
        if bad:
            problems.append(f"{prop}: times {times}")
    return problems


## @brief Check every generated file; print failures; exit 1 on any.
## @req REQ-LOTTIE-002
## @version 1
## @return Process exit code.
def main():
    files = sorted(ROOT.rglob("*.json"))
    failed = 0
    for f in files:
        problems = check_file(f)
        if problems:
            failed += 1
            print(f"FAIL {f.relative_to(ROOT)}")
            for p in problems:
                print(f"     {p}")
    print(f"{len(files) - failed}/{len(files)} files have monotonic keyframe times")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
