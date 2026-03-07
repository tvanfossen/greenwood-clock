"""Render moon_cycle.json — handles scale, anchor, alpha matte, ip/op."""

import json
import math
from PIL import Image, ImageDraw

with open(
    "/home/tvanfossen/Projects/greenwood-clock/tools/lottie_gen/output/astro/moon_cycle.json"
) as f:
    data = json.load(f)

W, H = data["w"], data["h"]
op = data["op"]
layers = data["layers"]
scale = 4


def lerp_kf(kfs, frame, dim):
    for i in range(len(kfs) - 1):
        t0, t1 = kfs[i]["t"], kfs[i + 1]["t"]
        if t0 <= frame <= t1:
            frac = (frame - t0) / (t1 - t0) if t1 != t0 else 0
            s0, s1 = kfs[i]["s"], kfs[i + 1]["s"]
            return [
                s0[j] + frac * (s1[j] - s0[j])
                for j in range(min(dim, len(s0), len(s1)))
            ]
    if frame <= kfs[0]["t"]:
        return kfs[0]["s"][:dim]
    return kfs[-1]["s"][:dim]


def get_val(prop, frame, dim=3):
    if isinstance(prop, dict) and prop.get("a") == 1:
        return lerp_kf(prop["k"], frame, dim)
    if isinstance(prop, dict):
        k = prop.get("k", [0] * dim)
        return k[:dim] if isinstance(k, list) else [k] * dim
    return prop[:dim] if isinstance(prop, list) else [prop] * dim


phases = {
    0: "new_moon",
    30: "waxing_crescent",
    60: "first_quarter",
    90: "waxing_gibbous",
    120: "full_moon",
    150: "waning_gibbous",
    180: "third_quarter",
    210: "waning_crescent",
    240: "new_moon_end",
}

out = "/home/tvanfossen/Projects/greenwood-clock/tools/lottie_gen/.claude/tmp"
for frame, name in phases.items():
    img = Image.new("RGBA", (W * scale, H * scale), (30, 30, 30, 255))

    # Build matte masks (one per td=1 layer, keyed by index)
    matte_masks = {}
    for idx, layer in enumerate(layers):
        if layer.get("td") == 1:
            mask = Image.new("L", img.size, 0)
            mdraw = ImageDraw.Draw(mask)
            pos = get_val(layer["ks"]["p"], frame)
            for shape in layer["shapes"]:
                if shape["ty"] == "el":
                    sz = get_val(shape["s"], frame, 2)
                    mcx, mcy = pos[0] * scale, pos[1] * scale
                    mrw, mrh = sz[0] * scale / 2, sz[1] * scale / 2
                    mdraw.ellipse(
                        [mcx - mrw, mcy - mrh, mcx + mrw, mcy + mrh], fill=255
                    )
            matte_masks[idx] = mask

    # Render layers back to front
    for idx, layer in enumerate(layers):
        if layer.get("td") == 1:
            continue

        # Check ip/op visibility
        lip = layer.get("ip", 0)
        lop = layer.get("op", op)
        if frame < lip or frame >= lop:
            continue

        pos = get_val(layer["ks"]["p"], frame)
        anchor = get_val(layer["ks"]["a"], frame)
        scl = get_val(layer["ks"]["s"], frame)
        sx, sy = scl[0] / 100, scl[1] / 100

        for shape in layer["shapes"]:
            if shape["ty"] == "el":
                sz = get_val(shape["s"], frame, 2)
                # Shape center in local coords
                sp = get_val(shape["p"], frame, 2)

                # Apply transform: world = pos + scale * (local - anchor)
                local_x = sp[0] - anchor[0]
                local_y = sp[1] - anchor[1]
                world_cx = (pos[0] + sx * local_x) * scale
                world_cy = (pos[1] + sy * local_y) * scale
                rw = sz[0] * abs(sx) * scale / 2
                rh = sz[1] * abs(sy) * scale / 2

                color = (200, 200, 200, 255)
                for s2 in layer["shapes"]:
                    if s2["ty"] == "fl":
                        c = s2["c"]["k"]
                        color = (int(c[0] * 255), int(c[1] * 255), int(c[2] * 255), 255)

                if rw < 0.5 or rh < 0.5:
                    continue

                # Check for alpha matte
                matte_idx = idx - 1
                if layer.get("tt") == 1 and matte_idx in matte_masks:
                    temp = Image.new("RGBA", img.size, (0, 0, 0, 0))
                    tdraw = ImageDraw.Draw(temp)
                    tdraw.ellipse(
                        [world_cx - rw, world_cy - rh, world_cx + rw, world_cy + rh],
                        fill=color,
                    )
                    masked = Image.new("RGBA", img.size, (0, 0, 0, 0))
                    masked.paste(temp, mask=matte_masks[matte_idx])
                    img = Image.alpha_composite(img, masked)
                else:
                    draw = ImageDraw.Draw(img)
                    draw.ellipse(
                        [world_cx - rw, world_cy - rh, world_cx + rw, world_cy + rh],
                        fill=color,
                    )

    path = f"{out}/moon_{frame:03d}_{name}.png"
    img.save(path)
    illum = (1 - math.cos(2 * math.pi * frame / op)) / 2.0
    print(
        f"  {name:20s} f={frame:3d} illum={illum:.0%} sx_wax={100*(1+math.cos(2*math.pi*frame/op))/2:.1f}%"
    )
