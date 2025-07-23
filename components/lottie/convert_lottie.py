#!/usr/bin/env python3
"""
convert_lottie.py

Scan the `json/` folder for .json Lottie files, call your existing
filetohex.py on each, and emit a C‐array plus size in `c_array/`.
"""
import sys
import subprocess
from pathlib import Path

def main():
    base = Path(__file__).parent.resolve()
    src_dir = base / "json"
    dst_dir = base / "c_array"
    hexer   = base / "filetohex.py"

    if not hexer.exists():
        print(f"Error: filetohex.py not found in {base}", file=sys.stderr)
        sys.exit(1)
    if not src_dir.is_dir():
        print(f"Error: json folder not found: {src_dir}", file=sys.stderr)
        sys.exit(1)

    dst_dir.mkdir(parents=True, exist_ok=True)

    for json_file in src_dir.glob("*.json"):
        name = json_file.stem
        print(f"Converting {json_file.name} → {name}.c")
        res = subprocess.run(
            [sys.executable, str(hexer), str(json_file)],
            capture_output=True, text=True
        )
        if res.returncode != 0:
            print(f"  filetohex failed:\n{res.stderr}", file=sys.stderr)
            continue

        data = res.stdout.strip().rstrip(',')
        out_c = dst_dir / f"{name}.c"
        with open(out_c, "w") as f:
            f.write('#include "lvgl.h"\n')
            f.write('#if LV_USE_LOTTIE\n\n')
            f.write(f'const uint8_t {name}[] = {{\n')
            f.write(data + ',\n')
            f.write('0x00  /* terminator */\n};\n\n')
            f.write(f'const size_t {name}_size = sizeof({name});\n\n')
            f.write('#endif\n')
        print(f"  → {out_c}")

if __name__ == "__main__":
    main()
