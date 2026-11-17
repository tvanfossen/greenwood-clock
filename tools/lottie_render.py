"""Offline Lottie rendering with ThorVG — the same engine the firmware uses (LVGL lv_lottie).

Note: the device embeds ThorVG 0.15.3 (components/lvgl/src/libs/thorvg/config.h);
thorvg-python bundles a 1.x engine. Results are representative, not bit-exact.
"""

from pathlib import Path

import thorvg_python as tvg
from PIL import Image

_ENGINE = None


## @brief Shared ThorVG engine (initialised once per process).
## @utility
## @version 1
## @return The tvg.Engine instance.
def engine():
    global _ENGINE
    if _ENGINE is None:
        _ENGINE = tvg.Engine(threads=4)
    return _ENGINE


class LottieRenderer:
    """One Lottie file bound to a fixed-size software canvas."""

    ## @brief Load @p path and prepare a w x h straight-alpha canvas.
    ## @utility
    ## @version 1
    def __init__(self, path, w, h):
        self.path = Path(path)
        self.w, self.h = w, h
        eng = engine()
        self.canvas = tvg.SwCanvas(eng)
        self.canvas.set_target(w, h, cs=tvg.Colorspace.ABGR8888S)
        self.anim = tvg.LottieAnimation(eng)
        pic = self.anim.get_picture()
        res = pic.load(str(self.path))
        if res != tvg.Result.SUCCESS:
            raise RuntimeError(f"ThorVG load failed for {self.path}: {res.name}")
        pic.set_size(w, h)
        self.canvas.add(pic)
        _, self.total_frames = self.anim.get_total_frame()

    ## @brief Render frame @p no.
    ## @utility
    ## @version 1
    ## @return PIL RGBA image.
    def render(self, no):
        self.anim.set_frame(float(no))
        self.canvas.update()
        self.canvas.draw(True)
        self.canvas.sync()
        return Image.frombuffer(
            "RGBA", (self.w, self.h), bytes(self.canvas.buffer_arr), "raw", "RGBA", 0, 1
        )


## @brief Alpha coverage of an image in fully-opaque-pixel units.
## @utility
## @version 1
## @return sum(alpha) / 255.
def coverage(img):
    return sum(img.getchannel("A").getdata()) / 255.0
