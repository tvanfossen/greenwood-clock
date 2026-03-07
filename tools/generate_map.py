#!/usr/bin/env python3
"""Generate a dark-themed static map PNG from OpenStreetMap tiles.

Fetches OSM tiles for a given lat/lon at a specified zoom level,
composites them into a 1024x600 PNG suitable for the radar overlay background.

Usage:
    python tools/generate_map.py --lat 43.366 --lon -85.851 --zoom 8
    python tools/generate_map.py --lat 43.366 --lon -85.851 --zoom 8 --output local.png
"""

import argparse
import math
import sys
from io import BytesIO
from pathlib import Path

try:
    from PIL import Image
except ImportError:
    print("ERROR: Pillow is required. Install with: pip install Pillow")
    sys.exit(1)

try:
    import urllib.request
except ImportError:
    pass

TILE_SIZE = 256
OUTPUT_W = 1024
OUTPUT_H = 600

# Tile servers (no API key required)
TILE_SERVERS = {
    "carto-dark": "https://basemaps.cartocdn.com/dark_all/{z}/{x}/{y}.png",
    "carto-voyager": "https://basemaps.cartocdn.com/rastertiles/voyager/{z}/{x}/{y}.png",
    "osm": "https://tile.openstreetmap.org/{z}/{x}/{y}.png",
}

USER_AGENT = "greenwood-clock-map-gen/1.0"


def latlon_to_tile(lat: float, lon: float, zoom: int) -> tuple[float, float]:
    """Convert lat/lon to fractional tile coordinates at given zoom."""
    n = 2**zoom
    x = (lon + 180.0) / 360.0 * n
    lat_rad = math.radians(lat)
    y = (
        (1.0 - math.log(math.tan(lat_rad) + 1.0 / math.cos(lat_rad)) / math.pi)
        / 2.0
        * n
    )
    return x, y


def fetch_tile(server_url: str, z: int, x: int, y: int) -> Image.Image:
    """Fetch a single map tile."""
    url = server_url.format(z=z, x=x, y=y)
    req = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    with urllib.request.urlopen(req, timeout=30) as resp:
        data = resp.read()
    return Image.open(BytesIO(data))


def generate_map(
    lat: float, lon: float, zoom: int, server: str = "carto-dark"
) -> Image.Image:
    """Generate a 1024x600 map centered on lat/lon."""
    server_url = TILE_SERVERS.get(server, TILE_SERVERS["carto-dark"])

    # Find center tile coordinates
    cx, cy = latlon_to_tile(lat, lon, zoom)

    # How many tiles we need to cover 1024x600
    tiles_x = math.ceil(OUTPUT_W / TILE_SIZE) + 2  # extra margin
    tiles_y = math.ceil(OUTPUT_H / TILE_SIZE) + 2

    # Tile range
    start_tx = int(cx) - tiles_x // 2
    start_ty = int(cy) - tiles_y // 2
    end_tx = start_tx + tiles_x
    end_ty = start_ty + tiles_y

    # Pixel offset for center alignment
    offset_x = int((cx - int(cx)) * TILE_SIZE)
    offset_y = int((cy - int(cy)) * TILE_SIZE)

    # Composite image (larger than final, will be cropped)
    comp_w = tiles_x * TILE_SIZE
    comp_h = tiles_y * TILE_SIZE
    composite = Image.new("RGB", (comp_w, comp_h), (10, 10, 30))

    n_tiles = 2**zoom
    fetched = 0
    for tx in range(start_tx, end_tx):
        for ty in range(start_ty, end_ty):
            # Wrap tile coordinates
            wrapped_tx = tx % n_tiles
            if ty < 0 or ty >= n_tiles:
                continue

            px = (tx - start_tx) * TILE_SIZE
            py = (ty - start_ty) * TILE_SIZE

            try:
                tile = fetch_tile(server_url, zoom, wrapped_tx, ty)
                composite.paste(tile, (px, py))
                fetched += 1
            except Exception as e:
                print(f"  Warning: tile {zoom}/{wrapped_tx}/{ty} failed: {e}")

    print(f"  Fetched {fetched} tiles at zoom {zoom}")

    # Crop to 1024x600 centered on the lat/lon point
    center_px = (int(cx) - start_tx) * TILE_SIZE + offset_x
    center_py = (int(cy) - start_ty) * TILE_SIZE + offset_y

    left = center_px - OUTPUT_W // 2
    top = center_py - OUTPUT_H // 2
    right = left + OUTPUT_W
    bottom = top + OUTPUT_H

    # Clamp
    left = max(0, left)
    top = max(0, top)
    right = min(comp_w, right)
    bottom = min(comp_h, bottom)

    result = composite.crop((left, top, right, bottom))

    # Ensure exact output size
    if result.size != (OUTPUT_W, OUTPUT_H):
        final = Image.new("RGB", (OUTPUT_W, OUTPUT_H), (10, 10, 30))
        final.paste(result, (0, 0))
        result = final

    return result


def main():
    parser = argparse.ArgumentParser(
        description="Generate dark map PNG for radar overlay"
    )
    parser.add_argument("--lat", type=float, required=True, help="Center latitude")
    parser.add_argument("--lon", type=float, required=True, help="Center longitude")
    parser.add_argument("--zoom", type=int, default=8, help="Zoom level (default: 8)")
    parser.add_argument(
        "--server",
        choices=list(TILE_SERVERS.keys()),
        default="carto-dark",
        help="Tile server (default: carto-dark)",
    )
    parser.add_argument(
        "--output",
        default="local.png",
        help="Output filename (default: local.png)",
    )
    args = parser.parse_args()

    print(
        f"Generating {OUTPUT_W}x{OUTPUT_H} map at ({args.lat}, {args.lon}) zoom={args.zoom}"
    )
    img = generate_map(args.lat, args.lon, args.zoom, args.server)

    output_path = Path(args.output)
    img.save(output_path, "PNG")
    print(f"Saved to {output_path} ({output_path.stat().st_size:,} bytes)")
    print("\nDeploy to device:")
    print(f"  python tools/file_push.py {output_path} /maps/local.png")


if __name__ == "__main__":
    main()
