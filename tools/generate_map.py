#!/usr/bin/env python3
"""Generate a dark-themed static map PNG for the radar overlay background.

Fetches from ArcGIS MapServer using the exact same bounding box and projection
(EPSG:4326 equirectangular) as the NWS radar overlay, ensuring perfect alignment.

Usage:
    python tools/generate_map.py --lat 43.366 --lon -85.851
    python tools/generate_map.py --lat 43.366 --lon -85.851 --bbox-deg 2.0
    python tools/generate_map.py --lat 43.366 --lon -85.851 --output local.png
"""

import argparse
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

OUTPUT_W = 1024
OUTPUT_H = 600

# Must match RADAR_BBOX_DEG in radar_view.c and nws_radar.c
DEFAULT_BBOX_DEG = 2.0

# ArcGIS MapServer basemap services (no API key, same projection as NWS radar)
BASEMAP_SERVICES = {
    "dark-gray": (
        "https://services.arcgisonline.com/ArcGIS/rest/services/"
        "Canvas/World_Dark_Gray_Base/MapServer/export"
    ),
    "dark-gray-ref": (
        "https://services.arcgisonline.com/ArcGIS/rest/services/"
        "Canvas/World_Dark_Gray_Reference/MapServer/export"
    ),
    "imagery": (
        "https://services.arcgisonline.com/ArcGIS/rest/services/"
        "World_Imagery/MapServer/export"
    ),
    "topo": (
        "https://services.arcgisonline.com/ArcGIS/rest/services/"
        "World_Topo_Map/MapServer/export"
    ),
}

USER_AGENT = "greenwood-clock-map-gen/1.0"


def fetch_arcgis_map(
    service_url: str,
    bbox: tuple[float, float, float, float],
    transparent: bool = False,
) -> Image.Image:
    """Fetch a map image from ArcGIS MapServer export endpoint.

    bbox: (lon_min, lat_min, lon_max, lat_max) in EPSG:4326
    transparent: if True, request transparent background (for overlay layers)
    """
    lon_min, lat_min, lon_max, lat_max = bbox
    url = (
        f"{service_url}"
        f"?bbox={lon_min:.4f},{lat_min:.4f},{lon_max:.4f},{lat_max:.4f}"
        f"&bboxSR=4326&imageSR=4326"
        f"&size={OUTPUT_W},{OUTPUT_H}"
        f"&format=png32&transparent={'true' if transparent else 'false'}&f=image"
    )

    req = urllib.request.Request(url, headers={"User-Agent": USER_AGENT})
    with urllib.request.urlopen(req, timeout=60) as resp:
        data = resp.read()
        content_type = resp.headers.get("Content-Type", "")

    if "image" not in content_type and len(data) < 1000:
        print(f"  Warning: response may not be an image (Content-Type: {content_type})")
        print(f"  Response: {data[:200]}")

    return Image.open(BytesIO(data))


def generate_map(lat: float, lon: float, bbox_deg: float) -> Image.Image:
    """Generate a 1024x600 map using ArcGIS dark basemap + reference labels."""
    bbox = (
        lon - bbox_deg,  # lon_min
        lat - bbox_deg,  # lat_min
        lon + bbox_deg,  # lon_max
        lat + bbox_deg,  # lat_max
    )

    print(
        f"  Bbox: lon=[{bbox[0]:.4f}, {bbox[2]:.4f}] lat=[{bbox[1]:.4f}, {bbox[3]:.4f}]"
    )

    # Fetch dark gray basemap (land, water, boundaries)
    print("  Fetching dark gray basemap...")
    base = fetch_arcgis_map(BASEMAP_SERVICES["dark-gray"], bbox)

    # Fetch reference overlay (labels, borders, roads)
    print("  Fetching reference labels...")
    try:
        ref = fetch_arcgis_map(
            BASEMAP_SERVICES["dark-gray-ref"], bbox, transparent=True
        )
        # Composite: reference layer on top of base (uses alpha)
        ref = ref.convert("RGBA")
        base = base.convert("RGBA")
        base = Image.alpha_composite(base, ref)
    except Exception as e:
        print(f"  Warning: reference layer failed ({e}), using base only")

    # Convert to RGB for PNG output (no alpha needed for basemap)
    result = base.convert("RGB")

    if result.size != (OUTPUT_W, OUTPUT_H):
        result = result.resize((OUTPUT_W, OUTPUT_H), Image.LANCZOS)

    return result


def main():
    parser = argparse.ArgumentParser(
        description="Generate dark map PNG for radar overlay (ArcGIS, EPSG:4326)"
    )
    parser.add_argument("--lat", type=float, required=True, help="Center latitude")
    parser.add_argument("--lon", type=float, required=True, help="Center longitude")
    parser.add_argument(
        "--bbox-deg",
        type=float,
        default=DEFAULT_BBOX_DEG,
        help=f"Bounding box half-size in degrees (default: {DEFAULT_BBOX_DEG})",
    )
    parser.add_argument(
        "--output",
        default="local.png",
        help="Output filename (default: local.png)",
    )
    args = parser.parse_args()

    print(
        f"Generating {OUTPUT_W}x{OUTPUT_H} map at ({args.lat}, {args.lon}) "
        f"bbox=±{args.bbox_deg}°"
    )
    img = generate_map(args.lat, args.lon, args.bbox_deg)

    output_path = Path(args.output)
    img.save(output_path, "PNG", optimize=True)
    print(f"Saved to {output_path} ({output_path.stat().st_size:,} bytes)")
    print("\nDeploy to device:")
    print(f"  python tools/file_push.py {output_path} /maps/local.png")


if __name__ == "__main__":
    main()
