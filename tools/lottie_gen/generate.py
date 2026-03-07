#!/usr/bin/env python3
"""Generate Lottie animation JSON files for the Greenwood Clock.

Uses programmatic generation instead of hand-writing JSON to ensure
correct keyframe math, proper staggering, and seamless loops.
"""

import json
import os
import math

OUTPUT_DIR = os.path.join(os.path.dirname(__file__), "output")

# --- Arctic Observatory Palette ---
GOLDEN = [0.914, 0.769, 0.416, 1]  # #E9C46A
STEEL_BLUE = [0.29, 0.435, 0.647, 1]  # #4A6FA5
CORAL = [0.906, 0.435, 0.318, 1]  # #E76F51
TEAL = [0.176, 0.545, 0.545, 1]  # #2D8B8B
CLOUD_LIGHT = [0.82, 0.855, 0.9, 1]  # Day cloud light
CLOUD_MID = [0.545, 0.647, 0.769, 1]  # Day cloud mid
CLOUD_DARK = [0.29, 0.373, 0.502, 1]  # Dark storm cloud
CLOUD_VERY_DARK = [0.227, 0.310, 0.435, 1]
SNOW_WHITE = [0.98, 0.98, 1.0, 1]
NIGHT_CLOUD = [0.227, 0.310, 0.431, 1]
MOON_SILVER = [0.98, 0.98, 0.98, 1]
LAVENDER = [0.643, 0.565, 0.761, 1]
NIGHT_SKY = [0.102, 0.102, 0.243, 1]

# --- Easing Presets ---
EASE_IN_OUT = {"i": {"x": [0.42], "y": [0]}, "o": {"x": [0.58], "y": [1]}}
EASE_IN_OUT_3 = {
    "i": {"x": [0.42, 0.42, 0.42], "y": [0, 0, 0]},
    "o": {"x": [0.58, 0.58, 0.58], "y": [1, 1, 1]},
}
EASE_IN_OUT_2 = {
    "i": {"x": [0.42, 0.42], "y": [0, 0]},
    "o": {"x": [0.58, 0.58], "y": [1, 1]},
}
LINEAR = {"i": {"x": [0], "y": [0]}, "o": {"x": [1], "y": [1]}}
LINEAR_2 = {"i": {"x": [0, 0], "y": [0, 0]}, "o": {"x": [1, 1], "y": [1, 1]}}
SOFT_DECEL = {"i": {"x": [0.25], "y": [0.1]}, "o": {"x": [0.25], "y": [1]}}


def kf(t, s, easing=None):
    """Create a keyframe. If easing is None, this is the last keyframe."""
    frame = {"t": t, "s": s if isinstance(s, list) else [s]}
    if easing:
        frame.update(easing)
    return frame


def kf_pos(t, s, easing=None, to=None, ti=None):
    """Create a position keyframe with spatial tangents."""
    frame = {"t": t, "s": s}
    if easing:
        frame["i"] = easing["i"]
        frame["o"] = easing["o"]
    if to:
        frame["to"] = to
    if ti:
        frame["ti"] = ti
    return frame


def static(val):
    """Static (non-animated) property."""
    return {"a": 0, "k": val}


def animated(keyframes):
    """Animated property."""
    return {"a": 1, "k": keyframes}


def layer_transform(pos, anchor=None, scale=None, rotation=None, opacity=None):
    """Build layer transform object."""
    ks = {
        "p": pos,
        "a": anchor or static([0, 0, 0]),
        "s": scale or static([100, 100, 100]),
        "r": rotation or static(0),
        "o": opacity or static(100),
    }
    return ks


def shape_layer(name, shapes, ks, ip=0, op=240, st=0):
    """Create a shape layer."""
    return {
        "ty": 4,
        "nm": name,
        "sr": 1,
        "ks": ks,
        "ao": 0,
        "shapes": shapes,
        "ip": ip,
        "op": op,
        "st": st,
    }


def ellipse(name, pos, size):
    return {
        "ty": "el",
        "nm": name,
        "p": static(pos),
        "s": static(size) if isinstance(size[0], (int, float)) else size,
    }


def ellipse_animated_size(name, pos, size_anim):
    return {"ty": "el", "nm": name, "p": static(pos), "s": size_anim}


def rect(name, pos, size, roundness=0):
    r = {"ty": "rc", "nm": name, "p": static(pos), "s": static(size)}
    if roundness:
        r["r"] = static(roundness)
    return r


def fill(name, color, opacity=100):
    f = {"ty": "fl", "nm": name, "c": static(color), "r": 1}
    f["o"] = static(opacity) if isinstance(opacity, (int, float)) else opacity
    return f


def stroke(name, color, width, opacity=100, lc=2, lj=2):
    return {
        "ty": "st",
        "nm": name,
        "c": static(color),
        "o": static(opacity),
        "w": static(width),
        "lc": lc,
        "lj": lj,
    }


def path_shape(name, vertices, in_pts=None, out_pts=None, closed=False):
    n = len(vertices)
    return {
        "ty": "sh",
        "nm": name,
        "ks": {
            "a": 0,
            "k": {
                "c": closed,
                "v": vertices,
                "i": in_pts or [[0, 0]] * n,
                "o": out_pts or [[0, 0]] * n,
            },
        },
    }


def group(name, items, transform=None):
    """Shape group with optional transform."""
    it = list(items)
    if transform is None:
        transform = {
            "ty": "tr",
            "p": static([0, 0]),
            "a": static([0, 0]),
            "s": static([100, 100]),
            "r": static(0),
            "o": static(100),
        }
    it.append(transform)
    return {"ty": "gr", "nm": name, "it": it}


def group_transform(pos=None, rotation=0, scale=None, opacity=100):
    tr = {"ty": "tr"}
    tr["p"] = pos or static([0, 0])
    tr["a"] = static([0, 0])
    tr["s"] = scale or static([100, 100])
    tr["r"] = static(rotation) if isinstance(rotation, (int, float)) else rotation
    tr["o"] = static(opacity) if isinstance(opacity, (int, float)) else opacity
    return tr


def lottie(name, w, h, op, layers):
    return {
        "v": "5.7.0",
        "nm": name,
        "ddd": 0,
        "fr": 60,
        "ip": 0,
        "op": op,
        "w": w,
        "h": h,
        "layers": layers,
    }


def write_lottie(subpath, data):
    path = os.path.join(OUTPUT_DIR, subpath)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w") as f:
        json.dump(data, f, indent=2)
    size = os.path.getsize(path)
    print(f"  {subpath:45s} {size:6d}B  layers={len(data['layers'])}")


# =============================================================================
# CLOUD BUILDERS
# =============================================================================


def make_cloud_shapes(color, lobe_specs):
    """Create cloud from overlapping ellipses.
    lobe_specs: list of (x, y, w, h) tuples.
    """
    shapes = []
    for i, (x, y, w, h) in enumerate(lobe_specs):
        shapes.append(ellipse(f"lobe-{i+1}", [x, y], [w, h]))
    shapes.append(fill("cloud-fill", color))
    return shapes


def cloud_layer(
    name, pos_start, pos_mid, lobes, color, op, opacity=85, bob_y=0, lobe_breathe=False
):
    """Cloud with horizontal drift and optional vertical bob."""
    half = op // 2
    if bob_y:
        pos_kf = [
            kf_pos(0, pos_start, EASE_IN_OUT_2, to=[2, -bob_y / 6, 0], ti=[0, 0, 0]),
            kf_pos(
                half // 2,
                [pos_mid[0] * 0.6 + pos_start[0] * 0.4, pos_start[1] - bob_y, 0],
                EASE_IN_OUT_2,
                to=[2, 0, 0],
                ti=[0, -bob_y / 6, 0],
            ),
            kf_pos(half, pos_mid, EASE_IN_OUT_2, to=[0, bob_y / 6, 0], ti=[2, 0, 0]),
            kf_pos(
                half + half // 2,
                [pos_mid[0] * 0.4 + pos_start[0] * 0.6, pos_start[1] + bob_y * 0.5, 0],
                EASE_IN_OUT_2,
                to=[-2, 0, 0],
                ti=[0, bob_y / 6, 0],
            ),
            kf_pos(op, pos_start),
        ]
    else:
        pos_kf = [
            kf_pos(0, pos_start, EASE_IN_OUT_2, to=[3, 0, 0], ti=[0, 0, 0]),
            kf_pos(half, pos_mid, EASE_IN_OUT_2, to=[0, 0, 0], ti=[3, 0, 0]),
            kf_pos(op, pos_start),
        ]

    shapes = []
    for i, (x, y, w, h) in enumerate(lobes):
        if lobe_breathe:
            dw, dh = w * 0.06, h * 0.06
            offset = i * (op // len(lobes))
            size_kf = animated(
                [
                    kf(0, [w, h], EASE_IN_OUT_2),
                    kf((op // 3 + offset) % op, [w + dw, h + dh], EASE_IN_OUT_2),
                    kf(
                        (2 * op // 3 + offset) % op,
                        [w - dw * 0.5, h - dh * 0.5],
                        EASE_IN_OUT_2,
                    ),
                    kf(op, [w, h]),
                ]
            )
            shapes.append(ellipse_animated_size(f"lobe-{i+1}", [x, y], size_kf))
        else:
            shapes.append(ellipse(f"lobe-{i+1}", [x, y], [w, h]))
    shapes.append(fill("cloud-fill", color))

    return shape_layer(
        name, shapes, layer_transform(animated(pos_kf), opacity=static(opacity)), op=op
    )


# =============================================================================
# RAIN DROP BUILDER
# =============================================================================


def rain_drop_layer(
    name,
    x,
    y_start,
    y_end,
    drop_w,
    drop_h,
    color,
    opacity_peak,
    op,
    st_offset=0,
    fall_dur=None,
):
    """Single rain drop that falls linearly with fade in/out.
    fall_dur: frames for the drop to complete its fall (varies speed).
              Defaults to op if not set.
    """
    if fall_dur is None:
        fall_dur = op
    fade_in = int(fall_dur * 0.1)
    fade_out = int(fall_dur * 0.1)
    hold_end = fall_dur - fade_out

    shapes = [
        rect("drop", [0, 0], [drop_w, drop_h], roundness=drop_w / 2),
        fill("drop-fill", color),
    ]

    pos_kf = [
        kf_pos(0, [x, y_start, 0], LINEAR_2, to=[0, 0, 0], ti=[0, 0, 0]),
        kf_pos(fall_dur, [x, y_end, 0]),
    ]

    o_kf = [
        kf(0, [0], SOFT_DECEL),
        kf(fade_in, [opacity_peak], LINEAR),
        kf(hold_end, [opacity_peak], EASE_IN_OUT),
        kf(fall_dur, [0]),
    ]

    return shape_layer(
        name,
        shapes,
        layer_transform(animated(pos_kf), opacity=animated(o_kf)),
        op=op,
        st=st_offset,
    )


# =============================================================================
# SNOWFLAKE BUILDER
# =============================================================================


def snowflake_layer(
    name,
    x_center,
    y_start,
    y_end,
    drift_x,
    size,
    color,
    opacity_peak,
    op,
    st_offset=0,
    fall_dur=None,
):
    """Single snowflake drifting down with horizontal sway.
    fall_dur: frames for the flake to complete its fall (varies speed).
              Defaults to op if not set.
    """
    if fall_dur is None:
        fall_dur = op

    shapes = [
        ellipse("flake", [0, 0], [size, size]),
        fill("flake-fill", color),
    ]

    # Sinusoidal horizontal drift via 4-point path
    q = fall_dur // 4
    pos_kf = [
        kf_pos(
            0,
            [x_center, y_start, 0],
            EASE_IN_OUT_2,
            to=[drift_x * 0.3, 0, 0],
            ti=[0, 0, 0],
        ),
        kf_pos(
            q,
            [x_center + drift_x, y_start + (y_end - y_start) * 0.25, 0],
            EASE_IN_OUT_2,
            to=[0, 0, 0],
            ti=[drift_x * 0.3, 0, 0],
        ),
        kf_pos(
            2 * q,
            [x_center, y_start + (y_end - y_start) * 0.5, 0],
            EASE_IN_OUT_2,
            to=[-drift_x * 0.3, 0, 0],
            ti=[0, 0, 0],
        ),
        kf_pos(
            3 * q,
            [x_center - drift_x * 0.6, y_start + (y_end - y_start) * 0.75, 0],
            EASE_IN_OUT_2,
            to=[0, 0, 0],
            ti=[-drift_x * 0.3, 0, 0],
        ),
        kf_pos(fall_dur, [x_center + drift_x * 0.3, y_end, 0]),
    ]

    fade_in = int(fall_dur * 0.08)
    fade_out = int(fall_dur * 0.12)
    o_kf = [
        kf(0, [0], SOFT_DECEL),
        kf(fade_in, [opacity_peak], LINEAR),
        kf(fall_dur - fade_out, [opacity_peak], EASE_IN_OUT),
        kf(fall_dur, [0]),
    ]

    # Gentle rotation
    r_kf = [
        kf(0, [0], EASE_IN_OUT),
        kf(fall_dur, [45 if drift_x > 0 else -30]),
    ]

    return shape_layer(
        name,
        shapes,
        layer_transform(
            animated(pos_kf), opacity=animated(o_kf), rotation=animated(r_kf)
        ),
        op=op,
        st=st_offset,
    )


# =============================================================================
# SUN BUILDERS
# =============================================================================


def sun_ray_groups(n_rays, ray_length, ray_width, ray_dist, color):
    """Create N ray groups, each rotated manually (no repeater)."""
    groups = []
    for i in range(n_rays):
        angle = i * (360 / n_rays)
        groups.append(
            group(
                f"ray-{i}",
                [
                    rect(
                        "r",
                        [0, -ray_dist],
                        [ray_width, ray_length],
                        roundness=ray_width / 2,
                    ),
                    fill("f", color),
                ],
                group_transform(rotation=angle),
            )
        )
    return groups


def sun_layers(cx, cy, op, disc_r=40, glow_r=70, ray_length=42, n_rays=8):
    """Sun disc + glow + ray layer with breathing animation."""
    half = op // 2

    # Ray layer (individual rotated groups, whole layer rotates slowly)
    ray_groups = sun_ray_groups(n_rays, ray_length, 6, disc_r + 12, GOLDEN)
    ray_layer = shape_layer(
        "sun-rays",
        ray_groups,
        layer_transform(
            static([cx, cy, 0]),
            rotation=animated(
                [
                    kf(0, [0], EASE_IN_OUT),
                    kf(op, [360 / n_rays]),  # rotate by one ray spacing
                ]
            ),
            scale=animated(
                [
                    kf(0, [100, 100, 100], EASE_IN_OUT_3),
                    kf(half, [112, 112, 100], EASE_IN_OUT_3),
                    kf(op, [100, 100, 100]),
                ]
            ),
            opacity=animated(
                [
                    kf(0, [70], EASE_IN_OUT),
                    kf(half, [95], EASE_IN_OUT),
                    kf(op, [70]),
                ]
            ),
        ),
        op=op,
    )

    # Glow layer (offset breathing from rays)
    glow_layer = shape_layer(
        "sun-glow",
        [
            ellipse("glow", [0, 0], [glow_r * 2, glow_r * 2]),
            fill("glow-fill", GOLDEN),
        ],
        layer_transform(
            static([cx, cy, 0]),
            opacity=animated(
                [
                    kf(0, [18], EASE_IN_OUT),
                    kf(int(op * 0.3), [35], EASE_IN_OUT),
                    kf(int(op * 0.65), [15], EASE_IN_OUT),
                    kf(op, [18]),
                ]
            ),
            scale=animated(
                [
                    kf(0, [100, 100, 100], EASE_IN_OUT_3),
                    kf(int(op * 0.35), [118, 118, 100], EASE_IN_OUT_3),
                    kf(int(op * 0.7), [96, 96, 100], EASE_IN_OUT_3),
                    kf(op, [100, 100, 100]),
                ]
            ),
        ),
        op=op,
    )

    # Disc layer (subtle breathing)
    disc_layer = shape_layer(
        "sun-disc",
        [
            ellipse("disc", [0, 0], [disc_r * 2, disc_r * 2]),
            fill("disc-fill", GOLDEN),
        ],
        layer_transform(
            static([cx, cy, 0]),
            scale=animated(
                [
                    kf(0, [100, 100, 100], EASE_IN_OUT_3),
                    kf(half, [104, 104, 100], EASE_IN_OUT_3),
                    kf(op, [100, 100, 100]),
                ]
            ),
        ),
        op=op,
    )

    # Order: rays behind, glow mid, disc front
    return [ray_layer, glow_layer, disc_layer]


# =============================================================================
# MOON BUILDERS
# =============================================================================


def moon_layers(cx, cy, op, disc_r=30, shadow_offset=15):
    """Crescent moon via bright disc + dark shadow disc."""
    half = op // 2
    glow_layer = shape_layer(
        "moon-glow",
        [
            ellipse("glow", [0, 0], [disc_r * 3.5, disc_r * 3.5]),
            fill("glow-fill", LAVENDER),
        ],
        layer_transform(
            static([cx, cy, 0]),
            opacity=animated(
                [
                    kf(0, [12], EASE_IN_OUT),
                    kf(half, [22], EASE_IN_OUT),
                    kf(op, [12]),
                ]
            ),
            scale=animated(
                [
                    kf(0, [100, 100, 100], EASE_IN_OUT_3),
                    kf(half, [108, 108, 100], EASE_IN_OUT_3),
                    kf(op, [100, 100, 100]),
                ]
            ),
        ),
        op=op,
    )

    bright = shape_layer(
        "moon-bright",
        [
            ellipse("disc", [0, 0], [disc_r * 2, disc_r * 2]),
            fill("moon-fill", MOON_SILVER),
        ],
        layer_transform(static([cx, cy, 0])),
        op=op,
    )

    shadow = shape_layer(
        "moon-shadow",
        [
            ellipse("shadow", [0, 0], [disc_r * 1.7, disc_r * 1.85]),
            fill("shadow-fill", NIGHT_SKY),
        ],
        layer_transform(static([cx + shadow_offset, cy - 5, 0])),
        op=op,
    )

    return [glow_layer, bright, shadow]


def star_layers(positions, op):
    """Twinkling stars at given positions. Each star twinkles independently."""
    layers = []
    for i, (x, y, size) in enumerate(positions):
        # Offset twinkle phase per star
        phase = int(i * op / len(positions) * 0.7) % op
        mid1 = (phase + op // 4) % op
        mid2 = (phase + op // 2) % op
        mid3 = (phase + 3 * op // 4) % op

        # Sort keyframe times and assign alternating brightness
        times = sorted([0, mid1, mid2, mid3, op])
        vals = [40, 85, 35, 75, 40]

        kfs = []
        for j, (t, v) in enumerate(zip(times, vals)):
            if j < len(times) - 1:
                kfs.append(kf(t, [v], EASE_IN_OUT))
            else:
                kfs.append(kf(t, [v]))

        layers.append(
            shape_layer(
                f"star-{i+1}",
                [
                    ellipse("star", [0, 0], [size, size]),
                    fill("star-fill", SNOW_WHITE),
                ],
                layer_transform(
                    static([x, y, 0]),
                    opacity=animated(kfs),
                    scale=animated(
                        [
                            kf(0, [100, 100, 100], EASE_IN_OUT_3),
                            kf(
                                (phase + op // 3) % op or 1,
                                [130, 130, 100],
                                EASE_IN_OUT_3,
                            ),
                            kf(op, [100, 100, 100]),
                        ]
                    ),
                ),
                op=op,
            )
        )
    return layers


# =============================================================================
# LIGHTNING BUILDER
# =============================================================================


def lightning_bolt_layer(name, vertices, bolt_time, op, color=None):
    """Lightning bolt that double-flashes at bolt_time."""
    if color is None:
        color = GOLDEN
    t0 = bolt_time
    # Double flash: on-off-on-off
    o_kf = [
        kf(0, [0], EASE_IN_OUT),
        kf(max(t0 - 2, 0), [0], EASE_IN_OUT),
        kf(t0, [100], EASE_IN_OUT),
        kf(t0 + 3, [0], EASE_IN_OUT),
        kf(t0 + 5, [90], EASE_IN_OUT),
        kf(t0 + 8, [0], EASE_IN_OUT),
        kf(op, [0]),
    ]

    return shape_layer(
        name,
        [
            path_shape("bolt", vertices),
            stroke("bolt-stroke", color, 3),
        ],
        layer_transform(
            static([150, 150, 0]),
            opacity=animated(o_kf),
        ),
        op=op,
    )


def cloud_illumination_kf(bolt_times, op, base_color, flash_color):
    """Animate cloud color to flash brighter during lightning."""
    kfs = []
    kfs.append({"t": 0, "s": base_color, **EASE_IN_OUT})
    for bt in sorted(bolt_times):
        kfs.append({"t": max(bt - 1, 1), "s": base_color, **EASE_IN_OUT})
        kfs.append({"t": bt, "s": flash_color, **EASE_IN_OUT})
        kfs.append({"t": bt + 6, "s": flash_color, **EASE_IN_OUT})
        kfs.append({"t": bt + 15, "s": base_color, **EASE_IN_OUT})
    kfs.append({"t": op, "s": base_color})
    return animated(kfs)


# =============================================================================
# ANIMATIONS
# =============================================================================


def gen_day_clear():
    op = 240
    layers = sun_layers(150, 150, op, disc_r=40, glow_r=72, ray_length=44, n_rays=8)
    return lottie("weather-day-clear", 300, 300, op, layers)


def gen_day_partly_cloudy():
    op = 300
    sun = sun_layers(120, 115, op, disc_r=30, glow_r=55, ray_length=32, n_rays=8)
    cloud = cloud_layer(
        "cloud",
        [165, 158, 0],
        [185, 155, 0],
        [(-28, 4, 68, 44), (5, -8, 80, 54), (32, 2, 62, 42)],
        CLOUD_LIGHT,
        op,
        opacity=88,
        bob_y=4,
        lobe_breathe=True,
    )
    shadow = shape_layer(
        "cloud-shadow",
        [
            ellipse("s1", [-20, 0], [70, 25]),
            ellipse("s2", [15, 0], [60, 22]),
            fill("shadow-fill", CLOUD_DARK, opacity=18),
        ],
        layer_transform(
            animated(
                [
                    kf_pos(0, [168, 173, 0], EASE_IN_OUT_2, to=[3, 0, 0], ti=[0, 0, 0]),
                    kf_pos(
                        op // 2,
                        [188, 171, 0],
                        EASE_IN_OUT_2,
                        to=[0, 0, 0],
                        ti=[3, 0, 0],
                    ),
                    kf_pos(op, [168, 173, 0]),
                ]
            ),
            scale=static([105, 40, 100]),
        ),
        op=op,
    )

    # Front to back: cloud, shadow, sun
    return lottie("weather-day-partly-cloudy", 300, 300, op, [cloud, shadow] + sun)


def gen_day_mostly_cloudy():
    op = 300
    front = cloud_layer(
        "cloud-front",
        [155, 160, 0],
        [172, 157, 0],
        [(-32, 5, 78, 52), (5, -6, 88, 62), (38, 3, 72, 48)],
        CLOUD_MID,
        op,
        opacity=92,
        bob_y=3,
        lobe_breathe=True,
    )
    mid = cloud_layer(
        "cloud-mid",
        [140, 138, 0],
        [126, 140, 0],
        [(-22, 0, 82, 55), (18, -5, 76, 50)],
        CLOUD_LIGHT,
        op,
        opacity=75,
        bob_y=2,
    )
    back = cloud_layer(
        "cloud-back",
        [162, 120, 0],
        [172, 122, 0],
        [(-15, 0, 70, 42), (22, -3, 62, 38)],
        [0.78, 0.82, 0.87, 1],
        op,
        opacity=50,
    )
    return lottie("weather-day-mostly-cloudy", 300, 300, op, [front, mid, back])


def gen_day_rain():
    op = 120  # 2-second loop for continuous rain
    # Cloud at top
    cloud = cloud_layer(
        "cloud",
        [150, 80, 0],
        [156, 80, 0],
        [(-35, 5, 85, 52), (0, -8, 95, 62), (38, 3, 78, 50)],
        CLOUD_DARK,
        op,
        opacity=92,
    )

    # 14 rain drops — organic spacing, staggered timing, varied speeds
    drops = []
    drop_specs = [
        # (x, width, height, opacity, st_offset, y_start, y_end, fall_dur)
        # Group A: early starters, widely spaced x
        (205, 2.5, 24, 70, 0, 62, 318, 78),
        (92, 2, 18, 55, -15, 80, 298, 115),
        # Group B: mid-early, different zone
        (148, 3, 20, 62, -32, 58, 322, 95),
        (185, 2, 16, 48, -50, 85, 295, 130),
        # Group C: staggered far apart
        (108, 2.5, 22, 65, -68, 65, 312, 82),
        (220, 3, 19, 52, -88, 72, 305, 110),
        # Group D: fill gaps
        (78, 2, 21, 58, -105, 70, 310, 92),
        (168, 2.5, 17, 50, -125, 82, 292, 125),
        # Group E: late starters
        (130, 3, 23, 68, -145, 60, 320, 85),
        (198, 2, 15, 45, -162, 88, 288, 118),
        # Group F: fill remaining gaps
        (85, 2.5, 20, 60, -180, 68, 308, 98),
        (155, 2, 22, 55, -200, 64, 315, 88),
        (115, 3, 18, 50, -218, 78, 300, 105),
        (210, 2.5, 21, 62, -235, 66, 312, 75),
    ]
    for i, (x, w, h, opac, st, ys, ye, fdur) in enumerate(drop_specs):
        drops.append(
            rain_drop_layer(
                f"drop-{i+1}",
                x,
                ys,
                ye,
                w,
                h,
                STEEL_BLUE,
                opac,
                op,
                st_offset=st,
                fall_dur=fdur,
            )
        )

    return lottie("weather-day-rain", 300, 300, op, [cloud] + drops)


def gen_day_thunderstorm():
    op = 180  # 3-second loop — shorter = more intense

    # Dark cloud with illumination
    bolt_times = [30, 72, 120, 155]
    cloud_shapes = make_cloud_shapes(
        CLOUD_VERY_DARK,
        [
            (-42, 5, 90, 58),
            (0, -12, 105, 68),
            (42, 3, 85, 55),
        ],
    )
    # Replace static fill with animated fill for illumination
    cloud_shapes[-1] = {
        "ty": "fl",
        "nm": "cloud-fill",
        "r": 1,
        "c": cloud_illumination_kf(
            bolt_times, op, CLOUD_VERY_DARK, [0.52, 0.60, 0.72, 1]
        ),
        "o": static(95),
    }
    cloud = shape_layer(
        "cloud",
        cloud_shapes,
        layer_transform(
            animated(
                [
                    kf_pos(
                        0, [150, 82, 0], EASE_IN_OUT_2, to=[1.5, 0, 0], ti=[0, 0, 0]
                    ),
                    kf_pos(
                        op // 2,
                        [157, 82, 0],
                        EASE_IN_OUT_2,
                        to=[0, 0, 0],
                        ti=[1.5, 0, 0],
                    ),
                    kf_pos(op, [150, 82, 0]),
                ]
            ),
        ),
        op=op,
    )

    # 4 lightning bolts at different positions and times
    bolt1 = lightning_bolt_layer(
        "bolt-1", [[8, -50], [-10, -12], [8, -8], [-6, 38]], bolt_times[0], op
    )
    bolt2 = lightning_bolt_layer(
        "bolt-2", [[-15, -45], [5, -15], [-8, -10], [10, 35]], bolt_times[1], op
    )
    bolt3 = lightning_bolt_layer(
        "bolt-3", [[20, -48], [2, -18], [15, -12], [-2, 32]], bolt_times[2], op
    )
    bolt4 = lightning_bolt_layer(
        "bolt-4", [[-5, -52], [12, -20], [-8, -15], [5, 30]], bolt_times[3], op
    )

    # Rain drops — organic spacing, staggered "drip drop" rhythm
    drops = []
    drop_specs = [
        # (x, width, height, opacity, st_offset, y_start, y_end, fall_dur)
        (210, 2.5, 22, 60, 0, 60, 322, 110),
        (88, 3, 18, 52, -22, 82, 295, 155),
        (155, 2, 24, 65, -48, 56, 325, 120),
        (192, 2.5, 16, 45, -72, 88, 290, 170),
        (105, 3, 20, 58, -98, 64, 318, 105),
        (225, 2, 21, 50, -125, 75, 305, 145),
        (78, 2.5, 19, 55, -150, 68, 312, 130),
        (170, 3, 17, 48, -178, 84, 298, 160),
        (130, 2, 23, 62, -205, 58, 320, 115),
        (200, 2.5, 15, 42, -228, 90, 288, 175),
        (95, 3, 20, 56, -255, 66, 310, 125),
        (148, 2, 22, 52, -280, 72, 308, 140),
        (215, 2.5, 18, 48, -305, 80, 300, 150),
        (112, 3, 21, 60, -330, 62, 316, 108),
    ]
    for i, (x, w, h, opac, st, ys, ye, fdur) in enumerate(drop_specs):
        drops.append(
            rain_drop_layer(
                f"drop-{i+1}",
                x,
                ys,
                ye,
                w,
                h,
                STEEL_BLUE,
                opac,
                op=op,
                st_offset=st,
                fall_dur=fdur,
            )
        )

    return lottie(
        "weather-day-thunderstorm",
        300,
        300,
        op,
        [cloud, bolt1, bolt2, bolt3, bolt4] + drops,
    )


def gen_day_snow():
    op = 300  # 5-second loop

    cloud = cloud_layer(
        "cloud",
        [150, 78, 0],
        [157, 78, 0],
        [(-30, 3, 78, 50), (5, -6, 88, 58), (35, 2, 72, 46)],
        CLOUD_MID,
        op,
        opacity=82,
        lobe_breathe=True,
    )

    # 14 snowflakes — organic spacing, widely staggered timing + speed
    flakes = []
    flake_specs = [
        # (x, drift, size, opacity, st_offset, fall_dur)
        (212, 16, 10, 75, 0, 265),
        (88, -14, 8, 62, -35, 210),
        (155, 18, 11, 78, -72, 300),
        (195, -10, 7, 55, -110, 185),
        (78, 15, 9, 68, -150, 250),
        (230, -17, 10, 72, -188, 230),
        (120, 13, 8, 60, -228, 195),
        (175, -12, 9, 65, -268, 275),
        (98, 16, 10, 70, -308, 240),
        (210, -14, 7, 55, -345, 190),
        (145, 11, 9, 68, -385, 260),
        (80, -15, 8, 58, -425, 285),
        (185, 14, 10, 72, -462, 205),
        (130, -12, 7, 55, -500, 248),
    ]
    for i, (x, drift, sz, opac, st, fdur) in enumerate(flake_specs):
        flakes.append(
            snowflake_layer(
                f"flake-{i+1}",
                x,
                60,
                310,
                drift,
                sz,
                SNOW_WHITE,
                opac,
                op,
                st_offset=st,
                fall_dur=fdur,
            )
        )

    return lottie("weather-day-snow", 300, 300, op, [cloud] + flakes)


def gen_night_clear():
    op = 300
    stars = star_layers(
        [
            (70, 70, 5),
            (220, 55, 4),
            (100, 45, 3.5),
            (245, 105, 3),
            (60, 130, 3.5),
        ],
        op,
    )
    moon = moon_layers(150, 125, op, disc_r=30)
    return lottie("weather-night-clear", 300, 300, op, stars + moon)


def gen_night_partly_cloudy():
    op = 300
    stars = star_layers(
        [
            (70, 65, 4.5),
            (230, 50, 3.5),
            (95, 40, 3),
        ],
        op,
    )
    moon = moon_layers(135, 115, op, disc_r=28)
    cloud = cloud_layer(
        "cloud",
        [170, 140, 0],
        [190, 137, 0],
        [(-25, 3, 65, 42), (8, -6, 75, 50), (30, 2, 58, 38)],
        NIGHT_CLOUD,
        op,
        opacity=80,
        bob_y=3,
        lobe_breathe=True,
    )
    return lottie("weather-night-partly-cloudy", 300, 300, op, [cloud] + stars + moon)


def gen_night_rain():
    op = 120
    cloud = cloud_layer(
        "cloud",
        [150, 80, 0],
        [155, 80, 0],
        [(-35, 5, 82, 50), (0, -8, 90, 58), (35, 3, 75, 48)],
        NIGHT_CLOUD,
        op,
        opacity=90,
    )

    drops = []
    night_blue = [0.35, 0.48, 0.65, 1]
    drop_specs = [
        # (x, width, height, opacity, st_offset, y_start, y_end, fall_dur)
        (208, 2.5, 22, 55, 0, 62, 315, 80),
        (90, 3, 18, 50, -18, 78, 298, 112),
        (152, 2, 20, 58, -40, 58, 320, 90),
        (188, 2.5, 16, 45, -62, 85, 292, 125),
        (108, 3, 21, 52, -85, 65, 310, 82),
        (222, 2, 19, 48, -108, 72, 305, 105),
        (78, 2.5, 23, 55, -132, 60, 318, 88),
        (168, 3, 17, 50, -155, 82, 295, 118),
        (128, 2, 20, 52, -178, 68, 308, 95),
        (200, 2.5, 22, 48, -200, 74, 302, 78),
        (95, 3, 18, 52, -222, 66, 312, 110),
        (175, 2, 21, 50, -245, 70, 306, 85),
    ]
    for i, (x, w, h, opac, st, ys, ye, fdur) in enumerate(drop_specs):
        drops.append(
            rain_drop_layer(
                f"drop-{i+1}",
                x,
                ys,
                ye,
                w,
                h,
                night_blue,
                opac,
                op,
                st_offset=st,
                fall_dur=fdur,
            )
        )

    return lottie("weather-night-rain", 300, 300, op, [cloud] + drops)


# =============================================================================
# UI
# =============================================================================


def gen_ui_loading():
    """3 orbiting dots — already validated, keep as-is."""
    op = 120
    layers = []
    dot_specs = [(0, 100, 8, 0), (120, 70, 7, 0), (240, 40, 6, 0)]
    for i, (start_rot, opac, size, _) in enumerate(dot_specs):
        layers.append(
            shape_layer(
                f"dot-{i+1}",
                [
                    ellipse("dot", [0, -14], [size, size]),
                    fill("fill", STEEL_BLUE),
                ],
                layer_transform(
                    static([30, 30, 0]),
                    opacity=static(opac),
                    rotation=animated(
                        [
                            kf(0, [start_rot], EASE_IN_OUT),
                            kf(op, [start_rot + 360]),
                        ]
                    ),
                ),
                op=op,
            )
        )
    return lottie("loading-spinner", 60, 60, op, layers)


# =============================================================================
# WEATHER DAY — REMAINING
# =============================================================================


def gen_day_overcast():
    op = 360  # 6-second loop — very slow, heavy
    front = cloud_layer(
        "cloud-front",
        [145, 155, 0],
        [160, 153, 0],
        [(-38, 5, 90, 58), (0, -8, 100, 65), (40, 3, 85, 55)],
        CLOUD_DARK,
        op,
        opacity=95,
        bob_y=2,
    )
    mid = cloud_layer(
        "cloud-mid",
        [160, 135, 0],
        [148, 137, 0],
        [(-25, 0, 85, 55), (20, -5, 80, 52)],
        CLOUD_MID,
        op,
        opacity=82,
    )
    back = cloud_layer(
        "cloud-back",
        [140, 118, 0],
        [155, 120, 0],
        [(-20, 0, 75, 45), (18, -3, 68, 42)],
        CLOUD_LIGHT,
        op,
        opacity=60,
    )
    return lottie("weather-day-overcast", 300, 300, op, [front, mid, back])


def gen_day_drizzle():
    op = 150  # 2.5-second loop
    cloud = cloud_layer(
        "cloud",
        [150, 85, 0],
        [155, 85, 0],
        [(-30, 4, 75, 48), (3, -6, 82, 55), (32, 2, 68, 44)],
        CLOUD_MID,
        op,
        opacity=78,
    )
    drops = []
    drop_specs = [
        # (x, width, height, opacity, st_offset, y_start, y_end, fall_dur)
        # Drizzle: sparse, gentle, well-separated
        (195, 1.5, 12, 40, 0, 90, 295, 120),
        (105, 1.5, 10, 35, -38, 100, 280, 145),
        (160, 1.5, 14, 42, -78, 88, 298, 105),
        (82, 1.5, 11, 38, -118, 96, 285, 138),
        (220, 1.5, 13, 40, -155, 92, 292, 128),
        (135, 1.5, 10, 35, -195, 102, 278, 150),
    ]
    for i, (x, w, h, opac, st, ys, ye, fdur) in enumerate(drop_specs):
        drops.append(
            rain_drop_layer(
                f"drop-{i+1}",
                x,
                ys,
                ye,
                w,
                h,
                STEEL_BLUE,
                opac,
                op,
                st_offset=st,
                fall_dur=fdur,
            )
        )
    return lottie("weather-day-drizzle", 300, 300, op, [cloud] + drops)


def gen_day_ice():
    op = 90  # 1.5-second loop — fast pellets
    cloud = cloud_layer(
        "cloud",
        [150, 78, 0],
        [154, 78, 0],
        [(-32, 4, 80, 50), (2, -7, 90, 58), (34, 2, 74, 48)],
        CLOUD_DARK,
        op,
        opacity=90,
    )
    drops = []
    # Ice pellets: wider, shorter than rain — fast but staggered
    pellet_specs = [
        # (x, width, height, opacity, st_offset, y_start, y_end, fall_dur)
        (210, 4, 6, 70, 0, 68, 325, 58),
        (88, 3, 5, 60, -12, 78, 312, 78),
        (165, 4, 7, 72, -28, 65, 328, 52),
        (115, 3, 5, 58, -42, 82, 308, 72),
        (225, 4, 6, 65, -55, 70, 320, 62),
        (78, 3, 5, 55, -68, 85, 305, 82),
        (190, 4, 7, 68, -82, 66, 322, 55),
        (135, 3, 6, 60, -95, 80, 310, 75),
        (95, 4, 5, 62, -108, 72, 318, 65),
        (205, 3, 6, 58, -120, 76, 315, 70),
        (148, 4, 7, 66, -135, 68, 325, 60),
        (82, 3, 5, 55, -148, 84, 302, 85),
    ]
    for i, (x, w, h, opac, st, ys, ye, fdur) in enumerate(pellet_specs):
        drops.append(
            rain_drop_layer(
                f"pellet-{i+1}",
                x,
                ys,
                ye,
                w,
                h,
                [0.75, 0.82, 0.90, 1],
                opac,
                op,
                st_offset=st,
                fall_dur=fdur,
            )
        )
    return lottie("weather-day-ice", 300, 300, op, [cloud] + drops)


def gen_day_fog():
    op = 480  # 8-second loop — very slow drift
    layers = []
    # 4 translucent horizontal bands at different heights/speeds
    band_specs = [
        ("fog-1", 150, 100, 20, 0.18, 92, 260),
        ("fog-2", 140, 150, -15, 0.14, 78, 210),
        ("fog-3", 160, 190, 25, 0.10, 110, 280),
        ("fog-4", 145, 240, -18, 0.16, 65, 240),
    ]
    for nm, cx, cy, drift, ob, rh, rw in band_specs:
        pos = animated(
            [
                kf_pos(
                    0, [cx, cy, 0], EASE_IN_OUT_2, to=[drift / 3, 0, 0], ti=[0, 0, 0]
                ),
                kf_pos(
                    op // 2,
                    [cx + drift, cy, 0],
                    EASE_IN_OUT_2,
                    to=[0, 0, 0],
                    ti=[drift / 3, 0, 0],
                ),
                kf_pos(op, [cx, cy, 0]),
            ]
        )
        o_kf = animated(
            [
                kf(0, [ob * 100], EASE_IN_OUT),
                kf(op // 3, [(ob + 0.06) * 100], EASE_IN_OUT),
                kf(2 * op // 3, [(ob - 0.04) * 100], EASE_IN_OUT),
                kf(op, [ob * 100]),
            ]
        )
        shapes = [
            rect("band", [0, 0], [rw, rh], roundness=rh / 2),
            fill("fog-fill", [0.82, 0.85, 0.90, 1]),
        ]
        layers.append(
            shape_layer(nm, shapes, layer_transform(pos, opacity=o_kf), op=op)
        )
    return lottie("weather-day-fog", 300, 300, op, layers)


def gen_day_windy():
    op = 180  # 3-second loop
    layers = []
    # 4 wind streaks moving right across canvas
    streak_specs = [
        (0, 110, 70, 0.55, 0),
        (-40, 160, 50, 0.40, -25),
        (-20, 200, 60, 0.48, -50),
        (-60, 140, 55, 0.35, -75),
    ]
    for i, (x_start, y, length, opac, st_off) in enumerate(streak_specs):
        pos = animated(
            [
                kf_pos(0, [x_start, y, 0], LINEAR_2, to=[0, 0, 0], ti=[0, 0, 0]),
                kf_pos(op, [x_start + 380, y, 0]),
            ]
        )
        shapes = [
            rect("streak", [0, 0], [length, 2], roundness=1),
            fill("streak-fill", [0.55, 0.65, 0.78, 1]),
        ]
        layers.append(
            shape_layer(
                f"wind-{i+1}",
                shapes,
                layer_transform(pos, opacity=static(opac * 100)),
                op=op,
                st=st_off,
            )
        )
    return lottie("weather-day-windy", 300, 300, op, layers)


def gen_day_haze():
    op = 360  # 6-second loop — subtle shimmer
    layers = []
    # 3 large translucent ellipses with slow scale/opacity breathing
    haze_specs = [
        (130, 140, 200, 160, 0.12, 0),
        (170, 170, 180, 140, 0.10, -40),
        (150, 150, 220, 170, 0.08, -80),
    ]
    for i, (cx, cy, w, h, opac, st_off) in enumerate(haze_specs):
        s_kf = animated(
            [
                kf(0, [100, 100, 100], EASE_IN_OUT_3),
                kf(op // 3, [108, 106, 100], EASE_IN_OUT_3),
                kf(2 * op // 3, [96, 98, 100], EASE_IN_OUT_3),
                kf(op, [100, 100, 100]),
            ]
        )
        o_kf = animated(
            [
                kf(0, [opac * 100], EASE_IN_OUT),
                kf(op // 2, [(opac + 0.05) * 100], EASE_IN_OUT),
                kf(op, [opac * 100]),
            ]
        )
        shapes = [
            ellipse("haze", [0, 0], [w, h]),
            fill("haze-fill", [0.85, 0.82, 0.75, 1]),
        ]
        layers.append(
            shape_layer(
                f"haze-{i+1}",
                shapes,
                layer_transform(static([cx, cy, 0]), scale=s_kf, opacity=o_kf),
                op=op,
                st=st_off,
            )
        )
    return lottie("weather-day-haze", 300, 300, op, layers)


# =============================================================================
# WEATHER NIGHT — REMAINING
# =============================================================================


def gen_night_mostly_cloudy():
    op = 300
    stars = star_layers([(80, 50, 3.5), (220, 40, 3)], op)
    moon = moon_layers(140, 110, op, disc_r=26)
    front = cloud_layer(
        "cloud-front",
        [155, 150, 0],
        [170, 148, 0],
        [(-30, 4, 75, 48), (5, -6, 82, 55), (32, 2, 68, 44)],
        NIGHT_CLOUD,
        op,
        opacity=88,
        bob_y=2,
    )
    mid = cloud_layer(
        "cloud-mid",
        [135, 130, 0],
        [125, 132, 0],
        [(-22, 0, 70, 45), (15, -4, 65, 42)],
        [0.20, 0.26, 0.38, 1],
        op,
        opacity=70,
    )
    return lottie(
        "weather-night-mostly-cloudy", 300, 300, op, [front, mid] + stars + moon
    )


def gen_night_overcast():
    op = 360
    front = cloud_layer(
        "cloud-front",
        [145, 155, 0],
        [158, 153, 0],
        [(-35, 5, 85, 55), (0, -8, 95, 62), (38, 3, 80, 52)],
        [0.15, 0.18, 0.28, 1],
        op,
        opacity=95,
        bob_y=2,
    )
    mid = cloud_layer(
        "cloud-mid",
        [160, 135, 0],
        [148, 137, 0],
        [(-25, 0, 80, 50), (18, -4, 72, 48)],
        [0.18, 0.22, 0.32, 1],
        op,
        opacity=80,
    )
    back = cloud_layer(
        "cloud-back",
        [140, 118, 0],
        [152, 120, 0],
        [(-20, 0, 70, 42), (15, -3, 65, 40)],
        [0.22, 0.28, 0.38, 1],
        op,
        opacity=55,
    )
    return lottie("weather-night-overcast", 300, 300, op, [front, mid, back])


def gen_night_drizzle():
    op = 150
    stars = star_layers([(75, 45, 3), (230, 55, 2.5)], op)
    moon = moon_layers(180, 90, op, disc_r=22)
    cloud = cloud_layer(
        "cloud",
        [140, 120, 0],
        [148, 120, 0],
        [(-25, 3, 68, 44), (5, -5, 75, 50), (28, 2, 62, 40)],
        NIGHT_CLOUD,
        op,
        opacity=75,
    )
    drops = []
    night_blue = [0.35, 0.48, 0.65, 1]
    drop_specs = [
        # (x, width, height, opacity, st_offset, y_start, y_end, fall_dur)
        (190, 1.5, 12, 35, 0, 125, 295, 115),
        (100, 1.5, 10, 30, -42, 135, 280, 145),
        (155, 1.5, 14, 38, -88, 120, 298, 108),
        (215, 1.5, 11, 32, -130, 138, 282, 140),
        (125, 1.5, 13, 36, -175, 128, 290, 125),
    ]
    for i, (x, w, h, opac, st, ys, ye, fdur) in enumerate(drop_specs):
        drops.append(
            rain_drop_layer(
                f"drop-{i+1}",
                x,
                ys,
                ye,
                w,
                h,
                night_blue,
                opac,
                op,
                st_offset=st,
                fall_dur=fdur,
            )
        )
    return lottie("weather-night-drizzle", 300, 300, op, [cloud] + drops + stars + moon)


def gen_night_thunderstorm():
    op = 180
    bolt_times = [28, 70, 118, 152]
    cloud_shapes_list = make_cloud_shapes(
        [0.12, 0.15, 0.25, 1],
        [
            (-42, 5, 90, 58),
            (0, -12, 105, 68),
            (42, 3, 85, 55),
        ],
    )
    cloud_shapes_list[-1] = {
        "ty": "fl",
        "nm": "cloud-fill",
        "r": 1,
        "c": cloud_illumination_kf(
            bolt_times, op, [0.12, 0.15, 0.25, 1], [0.40, 0.45, 0.58, 1]
        ),
        "o": static(95),
    }
    cloud = shape_layer(
        "cloud",
        cloud_shapes_list,
        layer_transform(
            animated(
                [
                    kf_pos(0, [150, 82, 0], EASE_IN_OUT_2, to=[1, 0, 0], ti=[0, 0, 0]),
                    kf_pos(
                        op // 2, [155, 82, 0], EASE_IN_OUT_2, to=[0, 0, 0], ti=[1, 0, 0]
                    ),
                    kf_pos(op, [150, 82, 0]),
                ]
            ),
        ),
        op=op,
    )

    bolt1 = lightning_bolt_layer(
        "bolt-1", [[8, -50], [-10, -12], [8, -8], [-6, 38]], bolt_times[0], op
    )
    bolt2 = lightning_bolt_layer(
        "bolt-2", [[-15, -45], [5, -15], [-8, -10], [10, 35]], bolt_times[1], op
    )
    bolt3 = lightning_bolt_layer(
        "bolt-3", [[20, -48], [2, -18], [15, -12], [-2, 32]], bolt_times[2], op
    )
    bolt4 = lightning_bolt_layer(
        "bolt-4", [[-5, -52], [12, -20], [-8, -15], [5, 30]], bolt_times[3], op
    )

    night_blue = [0.35, 0.48, 0.65, 1]
    drops = []
    drop_specs = [
        # (x, width, height, opacity, st_offset, y_start, y_end, fall_dur)
        (215, 2.5, 22, 52, 0, 58, 322, 108),
        (85, 3, 18, 48, -25, 80, 295, 152),
        (155, 2, 24, 58, -52, 55, 328, 118),
        (195, 2.5, 16, 42, -78, 86, 290, 168),
        (108, 3, 20, 55, -105, 62, 316, 100),
        (228, 2, 21, 45, -132, 74, 308, 142),
        (75, 2.5, 19, 50, -158, 66, 318, 88),
        (172, 3, 17, 44, -185, 82, 298, 158),
        (132, 2, 23, 55, -210, 56, 325, 112),
        (205, 2.5, 15, 40, -235, 88, 285, 172),
        (92, 3, 20, 48, -260, 64, 312, 125),
        (148, 2, 22, 52, -288, 70, 310, 135),
    ]
    for i, (x, w, h, opac, st, ys, ye, fdur) in enumerate(drop_specs):
        drops.append(
            rain_drop_layer(
                f"drop-{i+1}",
                x,
                ys,
                ye,
                w,
                h,
                night_blue,
                opac,
                op=op,
                st_offset=st,
                fall_dur=fdur,
            )
        )

    return lottie(
        "weather-night-thunderstorm",
        300,
        300,
        op,
        [cloud, bolt1, bolt2, bolt3, bolt4] + drops,
    )


def gen_night_snow():
    op = 300
    stars = star_layers([(65, 40, 3), (240, 50, 2.5), (95, 35, 3)], op)
    cloud = cloud_layer(
        "cloud",
        [150, 78, 0],
        [156, 78, 0],
        [(-28, 3, 72, 46), (5, -5, 82, 54), (32, 2, 68, 44)],
        [0.25, 0.30, 0.42, 1],
        op,
        opacity=80,
        lobe_breathe=True,
    )
    flakes = []
    flake_specs = [
        # (x, drift, size, opacity, st_offset, fall_dur)
        (215, 15, 9, 65, 0, 255),
        (85, -12, 7, 52, -38, 200),
        (160, 17, 10, 70, -78, 285),
        (195, -10, 6, 48, -118, 180),
        (75, 14, 8, 58, -160, 245),
        (225, -16, 9, 62, -200, 228),
        (118, 12, 7, 50, -240, 195),
        (180, -13, 8, 55, -280, 270),
        (95, 11, 9, 60, -318, 235),
        (210, -14, 6, 48, -358, 190),
        (140, 16, 8, 55, -398, 260),
        (78, -11, 7, 52, -438, 275),
    ]
    for i, (x, drift, sz, opac, st, fdur) in enumerate(flake_specs):
        flakes.append(
            snowflake_layer(
                f"flake-{i+1}",
                x,
                60,
                310,
                drift,
                sz,
                SNOW_WHITE,
                opac,
                op,
                st_offset=st,
                fall_dur=fdur,
            )
        )
    return lottie("weather-night-snow", 300, 300, op, [cloud] + flakes + stars)


def gen_night_ice():
    op = 90
    cloud = cloud_layer(
        "cloud",
        [150, 78, 0],
        [153, 78, 0],
        [(-30, 4, 78, 48), (2, -6, 86, 56), (32, 2, 72, 46)],
        [0.15, 0.20, 0.30, 1],
        op,
        opacity=88,
    )
    drops = []
    ice_color = [0.70, 0.78, 0.88, 1]
    pellet_specs = [
        # (x, width, height, opacity, st_offset, y_start, y_end, fall_dur)
        (208, 4, 6, 60, 0, 68, 325, 55),
        (85, 3, 5, 52, -14, 80, 310, 76),
        (162, 4, 7, 64, -30, 65, 328, 48),
        (115, 3, 5, 50, -45, 82, 308, 70),
        (225, 4, 6, 58, -58, 70, 320, 60),
        (78, 3, 5, 48, -72, 85, 305, 80),
        (190, 4, 7, 62, -85, 66, 322, 52),
        (132, 3, 6, 52, -100, 78, 315, 72),
        (95, 4, 5, 55, -115, 72, 318, 62),
        (205, 3, 6, 50, -128, 76, 312, 68),
    ]
    for i, (x, w, h, opac, st, ys, ye, fdur) in enumerate(pellet_specs):
        drops.append(
            rain_drop_layer(
                f"pellet-{i+1}",
                x,
                ys,
                ye,
                w,
                h,
                ice_color,
                opac,
                op,
                st_offset=st,
                fall_dur=fdur,
            )
        )
    return lottie("weather-night-ice", 300, 300, op, [cloud] + drops)


def gen_night_fog():
    op = 480
    stars = star_layers([(90, 60, 2.5), (210, 45, 2)], op)
    layers = []
    band_specs = [
        ("fog-1", 150, 110, 18, 14, 85, 250),
        ("fog-2", 140, 160, -14, 11, 72, 220),
        ("fog-3", 160, 200, 22, 8, 100, 270),
        ("fog-4", 145, 250, -16, 12, 60, 235),
    ]
    for nm, cx, cy, drift, ob, rh, rw in band_specs:
        pos = animated(
            [
                kf_pos(
                    0, [cx, cy, 0], EASE_IN_OUT_2, to=[drift / 3, 0, 0], ti=[0, 0, 0]
                ),
                kf_pos(
                    op // 2,
                    [cx + drift, cy, 0],
                    EASE_IN_OUT_2,
                    to=[0, 0, 0],
                    ti=[drift / 3, 0, 0],
                ),
                kf_pos(op, [cx, cy, 0]),
            ]
        )
        o_kf = animated(
            [
                kf(0, [ob], EASE_IN_OUT),
                kf(op // 3, [ob + 5], EASE_IN_OUT),
                kf(2 * op // 3, [ob - 3], EASE_IN_OUT),
                kf(op, [ob]),
            ]
        )
        shapes = [
            rect("band", [0, 0], [rw, rh], roundness=rh / 2),
            fill("fog-fill", [0.25, 0.28, 0.38, 1]),
        ]
        layers.append(
            shape_layer(nm, shapes, layer_transform(pos, opacity=o_kf), op=op)
        )
    return lottie("weather-night-fog", 300, 300, op, layers + stars)


# =============================================================================
# ASTRONOMY
# =============================================================================


def gen_astro_moon_cycle():
    """Seekable: frame 0=new moon, frame 120=full, frame 240=end.

    A scaled circle IS the terminator ellipse — mathematically exact.
    Scale.x of a circle from 100% to 0% creates the correct curvature.
    Anchor point at the left/right edge makes it shrink from one side.

    Two shadow layers (waxing + waning) with alpha mattes, cross at full moon.
    Layer stack (index 0=back, last=front):
      0: bright moon disc
      1: crater details (subtle dark ellipses)
      2: matte for waxing (td=1, invisible)
      3: waxing shadow (tt=1, frames 0-120, dark on left)
      4: matte for waning (td=1, invisible)
      5: waning shadow (tt=1, frames 120-240, dark on right)
    """
    import math

    op = 240
    cx, cy, r = 100, 100, 45
    kf_interval = 10

    # Bright moon disc
    bright = shape_layer(
        "moon-bright",
        [
            ellipse("disc", [0, 0], [r * 2, r * 2]),
            fill("moon-fill", MOON_SILVER),
        ],
        layer_transform(static([cx, cy, 0])),
        op=op,
    )

    # Crater details — each crater in its own group (ellipse+fill) for renderer compat
    crater_groups = []
    crater_defs = [
        # (name, ox, oy, w, h, gray)
        # Maria (large dark patches)
        ("mare-imbrium", -10, -14, 28, 24, 0.58),
        ("mare-seren", 14, -6, 20, 16, 0.62),
        ("mare-tranq", 10, 14, 22, 16, 0.60),
        ("mare-fecund", -16, 10, 18, 14, 0.63),
        # Medium craters
        ("copernicus", -20, -2, 10, 9, 0.55),
        ("tycho", -4, 24, 9, 8, 0.52),
        ("kepler", 22, 6, 7, 7, 0.58),
        # Small craters
        ("aristarchus", -26, -8, 5, 5, 0.50),
        ("small-1", 0, -26, 4, 4, 0.60),
        ("small-2", 18, -22, 5, 4, 0.58),
        ("small-3", -12, 26, 4, 3, 0.55),
    ]
    for cname, cox, coy, cw, ch, gray in crater_defs:
        crater_groups.append(
            group(
                cname,
                [
                    ellipse(cname, [cox, coy], [cw, ch]),
                    fill(f"{cname}-f", [gray, gray, gray + 0.02, 1]),
                ],
            )
        )
    craters = shape_layer(
        "craters", crater_groups, layer_transform(static([cx, cy, 0])), op=op
    )

    # Helper: build scale keyframes from cos curve over a frame range
    def scale_kfs(f_start, f_end):
        kfs = []
        f = f_start
        while f <= f_end:
            phase = f / op
            # (1 + cos(2*pi*phase)) / 2 gives: 1 at new moon, 0 at full
            sx = 100 * (1 + math.cos(2 * math.pi * phase)) / 2
            entry = {"t": f, "s": [round(sx, 2), 100, 100]}
            if f < f_end:
                entry["i"] = {"x": [0.42, 0.42, 0.42], "y": [0, 0, 0]}
                entry["o"] = {"x": [0.58, 0.58, 0.58], "y": [1, 1, 1]}
            kfs.append(entry)
            f += kf_interval
        # Ensure exact end frame
        if kfs[-1]["t"] != f_end:
            phase = f_end / op
            sx = 100 * (1 + math.cos(2 * math.pi * phase)) / 2
            kfs.append({"t": f_end, "s": [round(sx, 2), 100, 100]})
        return {"a": 1, "k": kfs}

    # Shadow shape: circle slightly larger than moon (matte clips excess)
    shadow_shapes = [
        ellipse("shadow", [0, 0], [(r + 2) * 2, (r + 2) * 2]),
        fill("shadow-fill", NIGHT_SKY),
    ]

    # --- Waxing shadow (frames 0-120, dark on left) ---
    # Anchor at left edge of circle, position at left edge of moon
    # As scale.x shrinks 100→0, shadow retracts from right toward left
    matte_wax = shape_layer(
        "matte-wax",
        [
            ellipse("disc", [0, 0], [r * 2, r * 2]),
            fill("matte-fill", [1, 1, 1, 1]),
        ],
        layer_transform(static([cx, cy, 0])),
        op=op,
    )
    matte_wax["td"] = 1

    waxing = shape_layer(
        "shadow-wax",
        list(shadow_shapes),
        layer_transform(
            static([cx - r, cy, 0]),
            anchor=static([-(r + 2), 0, 0]),
            scale=scale_kfs(0, 120),
        ),
        ip=0,
        op=121,
    )
    waxing["tt"] = 1

    # --- Waning shadow (frames 120-240, dark on right) ---
    # Anchor at right edge of circle, position at right edge of moon
    # As scale.x grows 0→100, shadow extends from right toward left
    matte_wan = shape_layer(
        "matte-wan",
        [
            ellipse("disc", [0, 0], [r * 2, r * 2]),
            fill("matte-fill", [1, 1, 1, 1]),
        ],
        layer_transform(static([cx, cy, 0])),
        op=op,
    )
    matte_wan["td"] = 1

    waning = shape_layer(
        "shadow-wan",
        list(shadow_shapes),
        layer_transform(
            static([cx + r, cy, 0]),
            anchor=static([(r + 2), 0, 0]),
            scale=scale_kfs(120, 240),
        ),
        ip=120,
        op=241,
    )
    waning["tt"] = 1

    return lottie(
        "astro-moon-cycle",
        200,
        200,
        op,
        [bright, craters, matte_wax, waxing, matte_wan, waning],
    )


def gen_astro_sunrise():
    op = 240
    cx = 150

    # Sky gradient simulation: warm band at horizon
    sky_warm = shape_layer(
        "sky-warm",
        [
            rect("band", [0, 0], [300, 60], roundness=0),
            fill("warm-fill", [0.95, 0.70, 0.40, 1]),
        ],
        layer_transform(
            static([cx, 120, 0]),
            opacity=animated(
                [
                    kf(0, [5], EASE_IN_OUT),
                    kf(op // 2, [30], EASE_IN_OUT),
                    kf(op, [15]),
                ]
            ),
        ),
        op=op,
    )

    # Horizon line
    horizon = shape_layer(
        "horizon",
        [
            rect("line", [0, 0], [300, 3], roundness=1),
            fill("horizon-fill", [0.60, 0.50, 0.40, 1]),
        ],
        layer_transform(static([cx, 120, 0])),
        op=op,
    )

    # Sun rising from below horizon
    sun_pos = animated(
        [
            kf_pos(0, [cx, 152, 0], EASE_IN_OUT_2, to=[0, -3, 0], ti=[0, 0, 0]),
            kf_pos(op, [cx, 78, 0]),
        ]
    )
    sun = shape_layer(
        "sun",
        [
            ellipse("disc", [0, 0], [44, 44]),
            fill("sun-fill", GOLDEN),
        ],
        layer_transform(sun_pos),
        op=op,
    )

    # Glow around sun
    glow = shape_layer(
        "sun-glow",
        [
            ellipse("glow", [0, 0], [90, 90]),
            fill("glow-fill", GOLDEN),
        ],
        layer_transform(
            sun_pos,
            opacity=animated(
                [
                    kf(0, [10], EASE_IN_OUT),
                    kf(op, [28]),
                ]
            ),
        ),
        op=op,
    )

    # 6 rays that fade in as sun rises
    ray_layers = []
    for i in range(6):
        angle = -75 + i * 30
        rad = math.radians(angle)
        dist = 50
        rx = cx + dist * math.sin(rad)
        ry = 90 - dist * math.cos(rad)
        o_kf = animated(
            [
                kf(0, [0], EASE_IN_OUT),
                kf(op // 3, [35 + i * 5], EASE_IN_OUT),
                kf(op, [55 + i * 5]),
            ]
        )
        ray_layers.append(
            shape_layer(
                f"ray-{i}",
                [
                    rect("ray", [0, 0], [3.5, 22], roundness=1.5),
                    fill("ray-fill", GOLDEN),
                ],
                layer_transform(
                    static([rx, ry, 0]), rotation=static(angle), opacity=o_kf
                ),
                op=op,
                st=-i * 10,
            )
        )

    return lottie(
        "astro-sunrise", 300, 150, op, ray_layers + [sun, glow, sky_warm, horizon]
    )


def gen_astro_sunset():
    op = 240
    cx = 150

    # Warm sky band
    sky_warm = shape_layer(
        "sky-warm",
        [
            rect("band", [0, 0], [300, 60], roundness=0),
            fill("warm-fill", [0.90, 0.45, 0.25, 1]),
        ],
        layer_transform(
            static([cx, 120, 0]),
            opacity=animated(
                [
                    kf(0, [25], EASE_IN_OUT),
                    kf(op // 2, [35], EASE_IN_OUT),
                    kf(op, [8]),
                ]
            ),
        ),
        op=op,
    )

    horizon = shape_layer(
        "horizon",
        [
            rect("line", [0, 0], [300, 3], roundness=1),
            fill("horizon-fill", [0.50, 0.35, 0.28, 1]),
        ],
        layer_transform(static([cx, 120, 0])),
        op=op,
    )

    # Sun descending
    sun_pos = animated(
        [
            kf_pos(0, [cx, 78, 0], EASE_IN_OUT_2, to=[0, 3, 0], ti=[0, 0, 0]),
            kf_pos(op, [cx, 152, 0]),
        ]
    )
    # Color shift: golden -> deep orange
    sun_color = animated(
        [
            {"t": 0, "s": GOLDEN, **EASE_IN_OUT},
            {"t": op // 2, "s": [0.95, 0.60, 0.25, 1], **EASE_IN_OUT},
            {"t": op, "s": [0.85, 0.40, 0.20, 1]},
        ]
    )
    sun = shape_layer(
        "sun",
        [
            ellipse("disc", [0, 0], [44, 44]),
            {"ty": "fl", "nm": "sun-fill", "c": sun_color, "r": 1, "o": static(100)},
        ],
        layer_transform(sun_pos),
        op=op,
    )

    glow = shape_layer(
        "sun-glow",
        [
            ellipse("glow", [0, 0], [100, 80]),
            fill("glow-fill", [0.92, 0.55, 0.30, 1]),
        ],
        layer_transform(
            sun_pos,
            opacity=animated(
                [
                    kf(0, [22], EASE_IN_OUT),
                    kf(op, [6]),
                ]
            ),
        ),
        op=op,
    )

    # Rays fading out
    ray_layers = []
    for i in range(6):
        angle = -75 + i * 30
        rad = math.radians(angle)
        dist = 45
        rx = cx + dist * math.sin(rad)
        ry = 85 - dist * math.cos(rad)
        o_kf = animated(
            [
                kf(0, [50], EASE_IN_OUT),
                kf(op // 2, [25], EASE_IN_OUT),
                kf(op, [0]),
            ]
        )
        ray_layers.append(
            shape_layer(
                f"ray-{i}",
                [
                    rect("ray", [0, 0], [3.5, 20], roundness=1.5),
                    fill("ray-fill", GOLDEN),
                ],
                layer_transform(
                    static([rx, ry, 0]), rotation=static(angle), opacity=o_kf
                ),
                op=op,
            )
        )

    return lottie(
        "astro-sunset", 300, 150, op, ray_layers + [sun, glow, sky_warm, horizon]
    )


def gen_astro_aurora():
    """Vertical curtain bands swaying gently — aurora borealis effect."""
    op = 360
    layers = []
    # Narrow vertical bands with soft edges via rounded rects
    band_specs = [
        # (cx, w, color, opacity, sway_x, phase_offset)
        (55, 45, [0.12, 0.72, 0.32, 1], 18, 20, 0),
        (115, 35, [0.18, 0.62, 0.70, 1], 14, -16, -45),
        (170, 50, [0.40, 0.28, 0.68, 1], 16, 22, -90),
        (235, 40, [0.15, 0.78, 0.42, 1], 15, -18, -140),
        (310, 55, [0.30, 0.42, 0.78, 1], 17, 18, -195),
        (365, 38, [0.22, 0.68, 0.48, 1], 12, -14, -250),
    ]
    for i, (cx, w, color, opac, sx, st_off) in enumerate(band_specs):
        cy = 100
        h = 180
        pos = animated(
            [
                kf_pos(0, [cx, cy, 0], EASE_IN_OUT_2, to=[sx / 3, 0, 0], ti=[0, 0, 0]),
                kf_pos(
                    op // 2,
                    [cx + sx, cy - 5, 0],
                    EASE_IN_OUT_2,
                    to=[0, 0, 0],
                    ti=[sx / 3, 0, 0],
                ),
                kf_pos(op, [cx, cy, 0]),
            ]
        )
        o_kf = animated(
            [
                kf(0, [opac], EASE_IN_OUT),
                kf(op // 3, [opac + 8], EASE_IN_OUT),
                kf(2 * op // 3, [opac - 4], EASE_IN_OUT),
                kf(op, [opac]),
            ]
        )
        s_kf = animated(
            [
                kf(0, [100, 100, 100], EASE_IN_OUT_3),
                kf(op // 3, [92, 105, 100], EASE_IN_OUT_3),
                kf(2 * op // 3, [108, 96, 100], EASE_IN_OUT_3),
                kf(op, [100, 100, 100]),
            ]
        )
        shapes = [
            rect("band", [0, 0], [w, h], roundness=w // 2),
            fill("band-fill", color),
        ]
        layers.append(
            shape_layer(
                f"aurora-{i+1}",
                shapes,
                layer_transform(pos, scale=s_kf, opacity=o_kf),
                op=op,
                st=st_off,
            )
        )
    return lottie("astro-aurora", 400, 200, op, layers)


def gen_astro_star_twinkle():
    """Single star with 4-point cross shape + pulse. 30x30."""
    op = 180
    # Two thin perpendicular rects = 4-point star
    s_kf = animated(
        [
            kf(0, [100, 100, 100], EASE_IN_OUT_3),
            kf(op // 3, [140, 140, 100], EASE_IN_OUT_3),
            kf(2 * op // 3, [85, 85, 100], EASE_IN_OUT_3),
            kf(op, [100, 100, 100]),
        ]
    )
    o_kf = animated(
        [
            kf(0, [35], EASE_IN_OUT),
            kf(op // 4, [95], EASE_IN_OUT),
            kf(op // 2, [25], EASE_IN_OUT),
            kf(3 * op // 4, [85], EASE_IN_OUT),
            kf(op, [35]),
        ]
    )
    # Horizontal bar
    h_bar = shape_layer(
        "h-bar",
        [
            rect("bar", [0, 0], [12, 2.5], roundness=1),
            fill("bar-fill", SNOW_WHITE),
        ],
        layer_transform(static([15, 15, 0]), scale=s_kf, opacity=o_kf),
        op=op,
    )
    # Vertical bar
    v_bar = shape_layer(
        "v-bar",
        [
            rect("bar", [0, 0], [2.5, 12], roundness=1),
            fill("bar-fill", SNOW_WHITE),
        ],
        layer_transform(static([15, 15, 0]), scale=s_kf, opacity=o_kf),
        op=op,
    )
    # Center dot
    dot = shape_layer(
        "center",
        [
            ellipse("dot", [0, 0], [4, 4]),
            fill("dot-fill", SNOW_WHITE),
        ],
        layer_transform(static([15, 15, 0]), opacity=o_kf),
        op=op,
    )

    return lottie("astro-star-twinkle", 30, 30, op, [h_bar, v_bar, dot])


# =============================================================================
# SURPRISE / CELEBRATION
# =============================================================================


def _heart_shape(size=20):
    """Proper heart bezier: 4 vertices with smooth cubic handles."""
    s = size
    # Top-center dip, left bump, bottom point, right bump
    return path_shape(
        "heart",
        [
            [0, -s * 0.3],  # top center (dip)
            [-s * 0.5, -s * 0.8],  # left bump peak
            [0, s * 0.6],  # bottom point
            [s * 0.5, -s * 0.8],  # right bump peak
        ],
        [  # in tangents
            [s * 0.25, 0],  # top: curve in from right
            [0, -s * 0.35],  # left: curve down
            [-s * 0.4, -s * 0.2],  # bottom: curve from left
            [0, s * 0.35],  # right: curve up
        ],
        [  # out tangents
            [-s * 0.25, 0],  # top: curve out to left
            [0, s * 0.35],  # left: curve up
            [s * 0.4, -s * 0.2],  # bottom: curve to right
            [0, -s * 0.35],  # right: curve down
        ],
        closed=True,
    )


def gen_surprise_hearts():
    op = 240
    layers = []
    heart_specs = [
        # (x, y_start, y_end, scale, opacity, color, st_offset, drift)
        (250, 480, 120, 100, 80, CORAL, 0, 25),
        (150, 460, 80, 85, 65, [0.95, 0.50, 0.45, 1], -30, -20),
        (350, 470, 100, 110, 72, [0.85, 0.38, 0.42, 1], -60, 18),
        (100, 450, 140, 75, 60, CORAL, -90, -28),
        (300, 465, 90, 95, 70, [0.92, 0.45, 0.40, 1], -120, 22),
        (200, 475, 60, 105, 75, [0.88, 0.40, 0.38, 1], -150, -16),
        (400, 455, 110, 80, 62, CORAL, -180, 30),
    ]
    for i, (x, ys, ye, sc, opac, color, st_off, drift) in enumerate(heart_specs):
        fdur = 200 + i * 8
        pos = animated(
            [
                kf_pos(
                    0, [x, ys, 0], EASE_IN_OUT_2, to=[drift / 3, 0, 0], ti=[0, 0, 0]
                ),
                kf_pos(
                    fdur // 2,
                    [x + drift, (ys + ye) / 2, 0],
                    EASE_IN_OUT_2,
                    to=[-drift / 4, 0, 0],
                    ti=[drift / 4, 0, 0],
                ),
                kf_pos(fdur, [x - drift * 0.4, ye, 0]),
            ]
        )
        o_kf = animated(
            [
                kf(0, [0], EASE_IN_OUT),
                kf(int(fdur * 0.12), [opac], EASE_IN_OUT),
                kf(int(fdur * 0.65), [opac], EASE_IN_OUT),
                kf(fdur, [0]),
            ]
        )
        r_kf = animated(
            [
                kf(0, [-8 if i % 2 == 0 else 8], EASE_IN_OUT),
                kf(fdur // 2, [6 if i % 2 == 0 else -6], EASE_IN_OUT),
                kf(fdur, [-4 if i % 2 == 0 else 4]),
            ]
        )
        shapes = [_heart_shape(18), fill("heart-fill", color)]
        layers.append(
            shape_layer(
                f"heart-{i+1}",
                shapes,
                layer_transform(
                    pos,
                    opacity=o_kf,
                    rotation=animated(r_kf.get("k", r_kf["k"])) if False else r_kf,
                    scale=static([sc, sc, 100]),
                ),
                op=op,
                st=st_off,
            )
        )
    return lottie("surprise-hearts", 500, 500, op, layers)


def gen_surprise_confetti():
    op = 180
    layers = []
    colors = [
        CORAL,
        GOLDEN,
        STEEL_BLUE,
        TEAL,
        LAVENDER,
        [0.95, 0.55, 0.30, 1],
        [0.45, 0.80, 0.55, 1],
    ]

    for i in range(14):
        color = colors[i % len(colors)]
        # Spread from center, arc up then fall down
        angle = (i * 26 + 5) % 360
        rad = math.radians(angle)
        spread = 60 + (i % 5) * 25
        cx, cy = 300, 200

        peak_x = cx + spread * math.sin(rad)
        peak_y = cy - 60 - (i % 4) * 25  # burst upward
        end_x = peak_x + (i - 7) * 8
        end_y = 380 + (i % 3) * 15

        mid_t = 30 + (i % 3) * 10
        pos = animated(
            [
                kf_pos(0, [cx, cy, 0], EASE_IN_OUT_2, to=[0, -15, 0], ti=[0, 0, 0]),
                kf_pos(
                    mid_t,
                    [peak_x, peak_y, 0],
                    EASE_IN_OUT_2,
                    to=[0, 0, 0],
                    ti=[0, -10, 0],
                ),
                kf_pos(op, [end_x, end_y, 0]),
            ]
        )
        r_kf = animated(
            [
                kf(0, [0], LINEAR),
                kf(op, [180 + i * 60]),
            ]
        )
        o_kf = animated(
            [
                kf(0, [0], EASE_IN_OUT),
                kf(8, [92], LINEAR),
                kf(int(op * 0.75), [80], EASE_IN_OUT),
                kf(op, [0]),
            ]
        )
        w = 8 + (i % 3) * 3
        h = 5 + (i % 4) * 2
        shapes = [
            rect("piece", [0, 0], [w, h], roundness=1),
            fill("piece-fill", color),
        ]
        layers.append(
            shape_layer(
                f"confetti-{i+1}",
                shapes,
                layer_transform(pos, rotation=r_kf, opacity=o_kf),
                op=op,
                st=-(i % 5) * 3,
            )
        )
    return lottie("surprise-confetti", 600, 400, op, layers)


def gen_surprise_fireworks():
    op = 240
    layers = []
    colors = [
        GOLDEN,
        CORAL,
        [0.45, 0.80, 0.95, 1],
        LAVENDER,
        [0.50, 0.90, 0.55, 1],
        [0.95, 0.70, 0.40, 1],
    ]
    cx, cy = 250, 250

    # Two bursts at different times for visual richness
    for burst_idx, (burst_t, bx, by) in enumerate([(0, 250, 200), (70, 180, 250)]):
        n_sparks = 10
        for i in range(n_sparks):
            angle = i * (360 / n_sparks) + burst_idx * 15
            rad = math.radians(angle)
            color = colors[(i + burst_idx * 3) % len(colors)]
            dist = 100 + (i % 3) * 30
            end_x = bx + dist * math.sin(rad)
            end_y = by - dist * math.cos(rad)

            # Fast burst then decelerate
            burst_end = 50 + (i % 4) * 8
            pos = animated(
                [
                    kf_pos(0, [bx, by, 0], EASE_IN_OUT_2, to=[0, 0, 0], ti=[0, 0, 0]),
                    kf_pos(
                        burst_end,
                        [end_x, end_y, 0],
                        EASE_IN_OUT_2,
                        to=[0, 0, 0],
                        ti=[0, 0, 0],
                    ),
                    kf_pos(
                        burst_end + 60, [end_x, end_y + 30, 0]
                    ),  # slight gravity fall
                ]
            )
            o_kf = animated(
                [
                    kf(0, [0], EASE_IN_OUT),
                    kf(4, [100], LINEAR),
                    kf(burst_end, [85], EASE_IN_OUT),
                    kf(burst_end + 60, [0]),
                ]
            )
            # Shrink as they fade
            s_kf = animated(
                [
                    kf(0, [80, 80, 100], EASE_IN_OUT_3),
                    kf(burst_end // 2, [120, 120, 100], EASE_IN_OUT_3),
                    kf(burst_end + 60, [30, 30, 100]),
                ]
            )
            shapes = [
                ellipse("spark", [0, 0], [7, 7]),
                fill("spark-fill", color),
            ]
            layers.append(
                shape_layer(
                    f"spark-{burst_idx}-{i}",
                    shapes,
                    layer_transform(pos, scale=s_kf, opacity=o_kf),
                    op=op,
                    st=-burst_t - i * 2,
                )
            )

    # Central flash for each burst
    for burst_idx, (burst_t, bx, by) in enumerate([(0, 250, 200), (70, 180, 250)]):
        flash_op = animated(
            [
                kf(0, [0], EASE_IN_OUT),
                kf(3, [85], EASE_IN_OUT),
                kf(25, [0]),
            ]
        )
        layers.append(
            shape_layer(
                f"flash-{burst_idx}",
                [
                    ellipse("flash", [0, 0], [25, 25]),
                    fill("flash-fill", [1, 1, 0.92, 1]),
                ],
                layer_transform(static([bx, by, 0]), opacity=flash_op),
                op=op,
                st=-burst_t,
            )
        )

    return lottie("surprise-fireworks", 500, 500, op, layers)


def gen_surprise_sparkles():
    """4-point star sparkles that pulse in and out at scattered positions."""
    op = 240
    layers = []
    sparkle_specs = [
        (80, 100, 10, 0),
        (320, 80, 8, -25),
        (200, 300, 12, -50),
        (100, 280, 9, -75),
        (350, 250, 10, -100),
        (250, 120, 8, -130),
        (50, 200, 11, -155),
        (300, 350, 9, -40),
        (180, 180, 10, -65),
    ]
    for i, (x, y, sz, st_off) in enumerate(sparkle_specs):
        s_kf = animated(
            [
                kf(0, [20, 20, 100], EASE_IN_OUT_3),
                kf(op // 4, [130, 130, 100], EASE_IN_OUT_3),
                kf(op // 2, [15, 15, 100], EASE_IN_OUT_3),
                kf(3 * op // 4, [125, 125, 100], EASE_IN_OUT_3),
                kf(op, [20, 20, 100]),
            ]
        )
        o_kf = animated(
            [
                kf(0, [10], EASE_IN_OUT),
                kf(op // 4, [92], EASE_IN_OUT),
                kf(op // 2, [8], EASE_IN_OUT),
                kf(3 * op // 4, [88], EASE_IN_OUT),
                kf(op, [10]),
            ]
        )
        r_kf = animated(
            [
                kf(0, [0], EASE_IN_OUT),
                kf(op, [45]),
            ]
        )
        # Two crossing thin rects = 4-point sparkle
        layers.append(
            shape_layer(
                f"sparkle-h-{i}",
                [
                    rect("h", [0, 0], [sz, sz * 0.25], roundness=1),
                    fill("h-fill", SNOW_WHITE),
                ],
                layer_transform(
                    static([x, y, 0]), scale=s_kf, opacity=o_kf, rotation=r_kf
                ),
                op=op,
                st=st_off,
            )
        )
        layers.append(
            shape_layer(
                f"sparkle-v-{i}",
                [
                    rect("v", [0, 0], [sz * 0.25, sz], roundness=1),
                    fill("v-fill", SNOW_WHITE),
                ],
                layer_transform(
                    static([x, y, 0]), scale=s_kf, opacity=o_kf, rotation=r_kf
                ),
                op=op,
                st=st_off,
            )
        )
    return lottie("surprise-sparkles", 400, 400, op, layers)


def gen_surprise_birthday_cake():
    op = 180
    cake = shape_layer(
        "cake",
        [
            rect("body", [0, 15], [120, 70], roundness=8),
            fill("cake-fill", [0.72, 0.55, 0.40, 1]),
            rect("frosting", [0, -12], [124, 20], roundness=10),
            fill("frost-fill", [0.95, 0.92, 0.88, 1]),
        ],
        layer_transform(static([150, 200, 0])),
        op=op,
    )

    candle = shape_layer(
        "candle",
        [
            rect("stick", [0, 0], [6, 35], roundness=2),
            fill("candle-fill", [0.90, 0.85, 0.30, 1]),
        ],
        layer_transform(static([150, 162, 0])),
        op=op,
    )

    flame_s = animated(
        [
            kf(0, [100, 100, 100], EASE_IN_OUT_3),
            kf(15, [90, 115, 100], EASE_IN_OUT_3),
            kf(30, [110, 95, 100], EASE_IN_OUT_3),
            kf(45, [95, 110, 100], EASE_IN_OUT_3),
            kf(60, [105, 98, 100], EASE_IN_OUT_3),
            kf(75, [92, 112, 100], EASE_IN_OUT_3),
            kf(90, [108, 96, 100], EASE_IN_OUT_3),
            kf(105, [94, 108, 100], EASE_IN_OUT_3),
            kf(120, [106, 94, 100], EASE_IN_OUT_3),
            kf(135, [96, 110, 100], EASE_IN_OUT_3),
            kf(150, [104, 97, 100], EASE_IN_OUT_3),
            kf(165, [93, 106, 100], EASE_IN_OUT_3),
            kf(op, [100, 100, 100]),
        ]
    )
    flame_o = animated(
        [
            kf(0, [85], EASE_IN_OUT),
            kf(20, [95], EASE_IN_OUT),
            kf(40, [80], EASE_IN_OUT),
            kf(60, [92], EASE_IN_OUT),
            kf(80, [82], EASE_IN_OUT),
            kf(100, [90], EASE_IN_OUT),
            kf(120, [84], EASE_IN_OUT),
            kf(140, [93], EASE_IN_OUT),
            kf(160, [83], EASE_IN_OUT),
            kf(op, [85]),
        ]
    )
    flame = shape_layer(
        "flame",
        [
            ellipse("flame", [0, 0], [10, 16]),
            fill("flame-fill", [1, 0.75, 0.20, 1]),
        ],
        layer_transform(static([150, 138, 0]), scale=flame_s, opacity=flame_o),
        op=op,
    )

    glow = shape_layer(
        "flame-glow",
        [
            ellipse("glow", [0, 0], [30, 30]),
            fill("glow-fill", [1, 0.80, 0.30, 1]),
        ],
        layer_transform(static([150, 140, 0]), opacity=static(12)),
        op=op,
    )

    return lottie("surprise-birthday-cake", 300, 300, op, [flame, glow, candle, cake])


def gen_surprise_flowers():
    """2 flowers blooming — fewer layers, cleaner look."""
    op = 300
    layers = []
    flower_specs = [
        (110, 190, [0.90, 0.45, 0.55, 1], 0),
        (190, 200, [0.75, 0.50, 0.85, 1], -50),
    ]
    for fi, (fx, fy, color, st_off) in enumerate(flower_specs):
        for pi in range(5):
            angle = pi * 72 + fi * 36
            rad = math.radians(angle)
            dist = 20
            px = fx + dist * math.sin(rad)
            py = fy - dist * math.cos(rad)
            bloom_s = animated(
                [
                    kf(0, [5, 5, 100], EASE_IN_OUT_3),
                    kf(op // 3, [105, 105, 100], EASE_IN_OUT_3),
                    kf(op, [100, 100, 100]),
                ]
            )
            o_kf = animated(
                [
                    kf(0, [0], EASE_IN_OUT),
                    kf(op // 4, [85], EASE_IN_OUT),
                    kf(op, [80]),
                ]
            )
            shapes = [
                ellipse("petal", [0, 0], [18, 26]),
                fill("petal-fill", color),
            ]
            layers.append(
                shape_layer(
                    f"f{fi}-p{pi}",
                    shapes,
                    layer_transform(
                        static([px, py, 0]),
                        rotation=static(angle),
                        scale=bloom_s,
                        opacity=o_kf,
                    ),
                    op=op,
                    st=st_off - pi * 12,
                )
            )
        # Center
        center_s = animated(
            [
                kf(0, [0, 0, 100], EASE_IN_OUT_3),
                kf(op // 3, [110, 110, 100], EASE_IN_OUT_3),
                kf(op, [100, 100, 100]),
            ]
        )
        layers.append(
            shape_layer(
                f"f{fi}-center",
                [
                    ellipse("center", [0, 0], [14, 14]),
                    fill("center-fill", GOLDEN),
                ],
                layer_transform(static([fx, fy, 0]), scale=center_s),
                op=op,
                st=st_off,
            )
        )
    return lottie("surprise-flowers", 300, 300, op, layers)


def gen_surprise_celebration():
    """Stars burst outward + confetti rains down."""
    op = 240
    layers = []
    star_colors = [GOLDEN, CORAL, STEEL_BLUE, LAVENDER, TEAL]
    cx, cy = 250, 250

    # Burst of stars
    for i in range(10):
        angle = i * 36 + 10
        rad = math.radians(angle)
        dist = 120 + (i % 3) * 30
        end_x = cx + dist * math.sin(rad)
        end_y = cy - dist * math.cos(rad)
        color = star_colors[i % len(star_colors)]

        burst_t = 60 + (i % 3) * 10
        pos = animated(
            [
                kf_pos(0, [cx, cy, 0], EASE_IN_OUT_2, to=[0, 0, 0], ti=[0, 0, 0]),
                kf_pos(
                    burst_t,
                    [end_x, end_y, 0],
                    EASE_IN_OUT_2,
                    to=[0, 0, 0],
                    ti=[0, 0, 0],
                ),
                kf_pos(burst_t + 50, [end_x, end_y + 25, 0]),
            ]
        )
        o_kf = animated(
            [
                kf(0, [0], EASE_IN_OUT),
                kf(6, [95], LINEAR),
                kf(burst_t, [80], EASE_IN_OUT),
                kf(burst_t + 50, [0]),
            ]
        )
        r_kf = animated(
            [
                kf(0, [0], LINEAR),
                kf(burst_t + 50, [120 + i * 25]),
            ]
        )
        # 4-point star shape via two rects
        layers.append(
            shape_layer(
                f"star-h-{i}",
                [
                    rect("h", [0, 0], [10, 3], roundness=1),
                    fill("h-fill", color),
                ],
                layer_transform(pos, rotation=r_kf, opacity=o_kf),
                op=op,
                st=-i * 4,
            )
        )
        layers.append(
            shape_layer(
                f"star-v-{i}",
                [
                    rect("v", [0, 0], [3, 10], roundness=1),
                    fill("v-fill", color),
                ],
                layer_transform(pos, rotation=r_kf, opacity=o_kf),
                op=op,
                st=-i * 4,
            )
        )

    return lottie("surprise-celebration", 500, 500, op, layers)


# =============================================================================
# UI — REMAINING
# =============================================================================


def gen_ui_wifi_connecting():
    """WiFi signal: dot + 3 concentric arcs using crescents (not full circles)."""
    op = 120
    layers = []

    # 3 arcs — each is a stroked circle with a masking fill circle on bottom half
    # Simpler approach: just use small arc-like curved rects
    for i in range(3):
        r = 12 + i * 8
        thickness = 2.5
        # Arc approximation: a short wide rounded rect, curved at the layer level
        # Actually, use two ellipses: outer stroke and inner fill to cut bottom
        # Simplest: stroked ellipse, layer positioned so bottom half is off-screen
        # ... or just use crescents

        # Crescent approach: outer filled circle - inner filled circle (bg color)
        # Won't work with transparent bg. Use stroke approach with clip.

        # Pragmatic: small rounded rect at each radius, rotated to fan shape
        arc_o = animated(
            [
                kf(0, [20], EASE_IN_OUT),
                kf(i * op // 3, [20], EASE_IN_OUT),
                kf(i * op // 3 + op // 6, [95], EASE_IN_OUT),
                kf((i + 1) * op // 3, [95], EASE_IN_OUT),
                kf(min((i + 1) * op // 3 + op // 6, op), [20]),
            ]
        )

        # Three small dots in an arc pattern
        for j in range(3):
            arc_angle = -35 + j * 35  # -35, 0, 35 degrees
            rad = math.radians(arc_angle)
            dx = r * math.sin(rad)
            dy = -r * math.cos(rad)

            layers.append(
                shape_layer(
                    f"arc{i}-dot{j}",
                    [
                        ellipse("dot", [0, 0], [thickness, thickness]),
                        fill("dot-fill", STEEL_BLUE),
                    ],
                    layer_transform(static([30 + dx, 38 + dy, 0]), opacity=arc_o),
                    op=op,
                )
            )

    # Base dot
    layers.append(
        shape_layer(
            "base-dot",
            [
                ellipse("dot", [0, 0], [5, 5]),
                fill("dot-fill", STEEL_BLUE),
            ],
            layer_transform(static([30, 38, 0])),
            op=op,
        )
    )

    return lottie("ui-wifi-connecting", 60, 60, op, layers)


def gen_ui_location_pin():
    """Location pin using simple shapes — circle head + triangular bottom."""
    op = 180
    # Pin head (circle)
    head = shape_layer(
        "pin-head",
        [
            ellipse("head", [0, 0], [18, 18]),
            fill("head-fill", CORAL),
        ],
        layer_transform(static([20, 18, 0])),
        op=op,
    )

    # Inner dot
    dot = shape_layer(
        "pin-dot",
        [
            ellipse("dot", [0, 0], [7, 7]),
            fill("dot-fill", [1, 1, 1, 1]),
        ],
        layer_transform(static([20, 18, 0])),
        op=op,
    )

    # Pin tail (small triangle pointing down)
    tail_v = [[0, 8], [-6, 0], [6, 0]]
    tail_i = [[0, 0]] * 3
    tail_o = [[0, 0]] * 3
    tail = shape_layer(
        "pin-tail",
        [
            path_shape("tail", tail_v, tail_i, tail_o, closed=True),
            fill("tail-fill", CORAL),
        ],
        layer_transform(static([20, 26, 0])),
        op=op,
    )

    # Expanding pulse ring
    pulse_s = animated(
        [
            kf(0, [30, 30, 100], EASE_IN_OUT_3),
            kf(op, [220, 220, 100]),
        ]
    )
    pulse_o = animated(
        [
            kf(0, [50], EASE_IN_OUT),
            kf(op, [0]),
        ]
    )
    pulse = shape_layer(
        "pulse",
        [
            ellipse("ring", [0, 0], [16, 16]),
            stroke("ring-stroke", CORAL, 1.5),
        ],
        layer_transform(static([20, 40, 0]), scale=pulse_s, opacity=pulse_o),
        op=op,
    )

    return lottie("ui-location-pin", 40, 60, op, [head, dot, tail, pulse])


def gen_ui_alert_pulse():
    """Warning icon: triangle + exclamation, each as separate layers."""
    op = 120

    # Triangle outline
    tri_v = [[0, -16], [16, 12], [-16, 12]]
    tri_i = [[0, 0]] * 3
    tri_o = [[0, 0]] * 3
    tri = shape_layer(
        "triangle",
        [
            path_shape("tri", tri_v, tri_i, tri_o, closed=True),
            fill("tri-fill", CORAL),
        ],
        layer_transform(static([30, 28, 0])),
        op=op,
    )

    # Exclamation bar
    bang = shape_layer(
        "bang",
        [
            rect("bar", [0, 0], [3, 10], roundness=1),
            fill("bar-fill", [1, 1, 1, 1]),
        ],
        layer_transform(static([30, 25, 0])),
        op=op,
    )

    # Exclamation dot
    bang_dot = shape_layer(
        "bang-dot",
        [
            ellipse("dot", [0, 0], [3, 3]),
            fill("dot-fill", [1, 1, 1, 1]),
        ],
        layer_transform(static([30, 33, 0])),
        op=op,
    )

    # Pulse ring
    pulse_s = animated(
        [
            kf(0, [60, 60, 100], EASE_IN_OUT_3),
            kf(op, [200, 200, 100]),
        ]
    )
    pulse_o = animated(
        [
            kf(0, [45], EASE_IN_OUT),
            kf(op, [0]),
        ]
    )
    pulse = shape_layer(
        "pulse",
        [
            ellipse("ring", [0, 0], [28, 28]),
            stroke("ring-stroke", CORAL, 1.5),
        ],
        layer_transform(static([30, 30, 0]), scale=pulse_s, opacity=pulse_o),
        op=op,
    )

    return lottie("ui-alert-pulse", 60, 60, op, [bang, bang_dot, tri, pulse])


# =============================================================================
# AMBIENT
# =============================================================================


def gen_ambient_sun_arc():
    """Seekable: frame 0=sunrise (left), 120=noon (top), 240=sunset (right)."""
    op = 240
    # Sun follows semicircular arc via multi-waypoint path
    n_points = 13
    pos_kfs = []
    for i in range(n_points):
        t = int(i * op / (n_points - 1))
        angle = math.pi * i / (n_points - 1)
        x = 200 - 160 * math.cos(angle)
        y = 170 - 140 * math.sin(angle)
        if i < n_points - 1:
            pos_kfs.append(
                kf_pos(t, [x, y, 0], EASE_IN_OUT_2, to=[0, 0, 0], ti=[0, 0, 0])
            )
        else:
            pos_kfs.append(kf_pos(t, [x, y, 0]))

    sun = shape_layer(
        "sun",
        [
            ellipse("disc", [0, 0], [28, 28]),
            fill("sun-fill", GOLDEN),
        ],
        layer_transform(animated(pos_kfs)),
        op=op,
    )

    glow = shape_layer(
        "sun-glow",
        [
            ellipse("glow", [0, 0], [48, 48]),
            fill("glow-fill", GOLDEN),
        ],
        layer_transform(
            animated(pos_kfs),
            opacity=animated(
                [
                    kf(0, [10], EASE_IN_OUT),
                    kf(op // 2, [25], EASE_IN_OUT),
                    kf(op, [10]),
                ]
            ),
        ),
        op=op,
    )

    # Arc path indicator (thin line)
    arc_dots = []
    for i in range(7):
        angle = math.pi * i / 6
        x = 200 - 160 * math.cos(angle)
        y = 170 - 140 * math.sin(angle)
        arc_dots.append(
            shape_layer(
                f"arc-dot-{i}",
                [
                    ellipse("d", [0, 0], [3, 3]),
                    fill("d-fill", [0.55, 0.50, 0.45, 1]),
                ],
                layer_transform(static([x, y, 0]), opacity=static(25)),
                op=op,
            )
        )

    horizon = shape_layer(
        "horizon",
        [
            rect("line", [0, 0], [380, 2], roundness=1),
            fill("horizon-fill", [0.55, 0.50, 0.45, 1]),
        ],
        layer_transform(static([200, 170, 0])),
        op=op,
    )

    return lottie("ambient-sun-arc", 400, 200, op, [sun, glow] + arc_dots + [horizon])


def gen_ambient_uv_gauge():
    """Seekable UV gauge: rising bar inside rounded container, color shifts."""
    op = 132

    # Container outline
    container = shape_layer(
        "container",
        [
            rect("outline", [0, 0], [30, 70], roundness=15),
            stroke("outline-stroke", [0.70, 0.70, 0.70, 1], 2),
        ],
        layer_transform(static([50, 50, 0])),
        op=op,
    )

    # Fill bar — grows from bottom to top via position + scale
    # Scale Y from 0 to 100, anchored at bottom
    fill_s = animated(
        [
            kf(0, [100, 3, 100], EASE_IN_OUT_3),
            kf(op, [100, 100, 100]),
        ]
    )
    fill_pos = animated(
        [
            kf_pos(0, [50, 82, 0], EASE_IN_OUT_2, to=[0, 0, 0], ti=[0, 0, 0]),
            kf_pos(op, [50, 50, 0]),
        ]
    )
    fill_color = animated(
        [
            {"t": 0, "s": [0.30, 0.80, 0.40, 1], **EASE_IN_OUT},
            {"t": op // 3, "s": [0.90, 0.85, 0.25, 1], **EASE_IN_OUT},
            {"t": 2 * op // 3, "s": [0.95, 0.55, 0.20, 1], **EASE_IN_OUT},
            {"t": op, "s": [0.90, 0.25, 0.20, 1]},
        ]
    )
    fill_bar = shape_layer(
        "fill-bar",
        [
            rect("bar", [0, 0], [24, 62], roundness=12),
            {"ty": "fl", "nm": "bar-fill", "c": fill_color, "r": 1, "o": static(85)},
        ],
        layer_transform(fill_pos, scale=fill_s),
        op=op,
    )

    return lottie("ambient-uv-gauge", 100, 100, op, [fill_bar, container])


# =============================================================================
# MAIN
# =============================================================================

GENERATORS = {
    # Weather Day
    "weather/day/clear.json": gen_day_clear,
    "weather/day/partly_cloudy.json": gen_day_partly_cloudy,
    "weather/day/mostly_cloudy.json": gen_day_mostly_cloudy,
    "weather/day/overcast.json": gen_day_overcast,
    "weather/day/rain.json": gen_day_rain,
    "weather/day/drizzle.json": gen_day_drizzle,
    "weather/day/thunderstorm.json": gen_day_thunderstorm,
    "weather/day/snow.json": gen_day_snow,
    "weather/day/ice.json": gen_day_ice,
    "weather/day/fog.json": gen_day_fog,
    "weather/day/windy.json": gen_day_windy,
    "weather/day/haze.json": gen_day_haze,
    # Weather Night
    "weather/night/clear.json": gen_night_clear,
    "weather/night/partly_cloudy.json": gen_night_partly_cloudy,
    "weather/night/mostly_cloudy.json": gen_night_mostly_cloudy,
    "weather/night/overcast.json": gen_night_overcast,
    "weather/night/rain.json": gen_night_rain,
    "weather/night/drizzle.json": gen_night_drizzle,
    "weather/night/thunderstorm.json": gen_night_thunderstorm,
    "weather/night/snow.json": gen_night_snow,
    "weather/night/ice.json": gen_night_ice,
    "weather/night/fog.json": gen_night_fog,
    # Astronomy
    "astro/moon_cycle.json": gen_astro_moon_cycle,
    "astro/sunrise.json": gen_astro_sunrise,
    "astro/sunset.json": gen_astro_sunset,
    "astro/aurora.json": gen_astro_aurora,
    "astro/star_twinkle.json": gen_astro_star_twinkle,
    # Surprise
    "surprise/hearts.json": gen_surprise_hearts,
    "surprise/confetti.json": gen_surprise_confetti,
    "surprise/fireworks.json": gen_surprise_fireworks,
    "surprise/sparkles.json": gen_surprise_sparkles,
    "surprise/birthday_cake.json": gen_surprise_birthday_cake,
    "surprise/flowers.json": gen_surprise_flowers,
    "surprise/celebration.json": gen_surprise_celebration,
    # UI
    "ui/loading.json": gen_ui_loading,
    "ui/wifi_connecting.json": gen_ui_wifi_connecting,
    "ui/location_pin.json": gen_ui_location_pin,
    "ui/alert_pulse.json": gen_ui_alert_pulse,
    # Ambient
    "ambient/sun_arc.json": gen_ambient_sun_arc,
    "ambient/uv_gauge.json": gen_ambient_uv_gauge,
}


def main():
    import sys

    targets = sys.argv[1:] if len(sys.argv) > 1 else GENERATORS.keys()
    print(f"Generating {len(list(targets))} animations:")
    for path in targets:
        if path in GENERATORS:
            data = GENERATORS[path]()
            write_lottie(path, data)
        else:
            print(f"  UNKNOWN: {path}")


if __name__ == "__main__":
    main()
