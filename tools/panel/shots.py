## @file shots.py
## @brief Screenshot the phone panel (sdcard/www/panel) in a real browser: serve it locally,
## open it in headless Firefox (Playwright) at a desktop size and a phone held upright and
## sideways, tap through the given steps and save one PNG per step; browser console errors are
## printed. Exit status: the number of taps whose control was not found.
## v3: a step may reload the page first ("reload": true) to start from the menu.
## v4: taps find their control by the text it shows (the panel's panel_find), so one list of
## steps runs at every size; the phone upright too.
## @version 4
"""Usage: shots.py <panel dir> <out dir> <steps.json>

steps.json: [{"name": "menu", "reload": false, "taps": ["Theme", {"find": "slider",
              "fx": 0.9}, {"find": "Add", "nth": 0}, ...], "drags": [[fx, fy, dx, dy]]}, ...]
A tap names text its control shows (any part of it), or "slider"; fx / fy place it in
the control (default its centre), nth picks among several (default -1, the last drawn). A
drag starts at a fraction of the canvas and moves by pixels. Each step (after an optional
reload) drags then taps, after the previous step's; then a screenshot is taken.
"""

import functools
import http.server
import json
import sys
import threading
from pathlib import Path

from playwright.sync_api import sync_playwright

VIEWS = {
    "desktop": (1024, 600, 1),
    "phone": (390, 844, 3),
    "phone_wide": (844, 390, 3),
}


## @brief Serve @p root on a free local port in a daemon thread.
## v2: the panel opens with sample data (?mock): no clock to pair with.
## @utility
## @version 2
## @return The server's base URL.
def serve(root):
    handler = functools.partial(
        http.server.SimpleHTTPRequestHandler, directory=str(root)
    )
    handler.log_message = lambda *a: None
    srv = http.server.ThreadingHTTPServer(("127.0.0.1", 0), handler)
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    return f"http://127.0.0.1:{srv.server_address[1]}/index.html?mock"


## @brief Tap the control @p item names (see the usage); one canvas pixel is one CSS pixel.
## @utility
## v2: by the text the control shows (panel_find), not by clock pixel.
## @version 2
## @return Whether the control was found.
def tap(page, item):
    item = {"find": item} if isinstance(item, str) else item
    found = page.evaluate(
        "([t, n]) => Module.ccall('panel_find', 'number', ['string', 'number'], [t, n])",
        [item["find"], item.get("nth", -1)],
    )
    if found:
        x1, y1, x2, y2 = page.evaluate("window.panelFound")
        box = page.locator("canvas").bounding_box()
        page.mouse.click(
            box["x"] + x1 + (x2 - x1) * item.get("fx", 0.5),
            box["y"] + y1 + (y2 - y1) * item.get("fy", 0.5),
        )
        page.wait_for_timeout(400)
    return bool(found)


## @brief Drag from a fraction (@p fx, @p fy) of the canvas by (@p dx, @p dy) pixels.
## @utility
## v2: starts at a fraction of the canvas (any size); held before release (no fling); kept
## on the canvas.
## @version 2
def drag(page, fx, fy, dx, dy):
    box = page.locator("canvas").bounding_box()
    x, y = box["x"] + fx * box["width"], box["y"] + fy * box["height"]
    # stay on the canvas: a release outside it never reaches the panel (it stays pressed)
    dy = max(dy, box["y"] + 4 - y)
    dy = min(dy, box["y"] + box["height"] - 4 - y)
    page.mouse.move(x, y)
    page.mouse.down()
    for i in range(1, 11):
        page.mouse.move(x + dx * i / 10, y + dy * i / 10)
        page.wait_for_timeout(16)
    page.wait_for_timeout(150)  # held still: no fling, so the next tap is a tap
    page.mouse.up()
    page.wait_for_timeout(700)


## @brief Open @p url and wait for the panel to run.
## @utility
## @version 1
def load(page, url):
    page.goto(url)
    page.wait_for_function("window.panelReady === true", timeout=20000)
    page.wait_for_timeout(600)


## @brief Run every step at one view size, saving <out>/<view>_<step>.png.
## v2: drags before taps.
## v3: optional reload per step.
## v4: counts the taps whose control was not found.
## @utility
## @version 4
## @return How many taps missed.
def run_view(browser, url, out, view, steps):
    w, h, scale = VIEWS[view]
    page = browser.new_page(
        viewport={"width": w, "height": h}, device_scale_factor=scale
    )
    page.on(
        "console", lambda m: m.type == "error" and print(f"[{view}] console: {m.text}")
    )
    page.on("pageerror", lambda e: print(f"[{view}] page error: {e}"))
    load(page, url)
    missed = 0
    for step in steps:
        if step.get("reload"):
            load(page, url)
        for fx, fy, dx, dy in step.get("drags", []):
            drag(page, fx, fy, dx, dy)
        for item in step.get("taps", []):
            if not tap(page, item):
                missed += 1
                print(f"[{view}] {step['name']}: no control shows {item!r}")
        path = out / f"{view}_{step['name']}.png"
        page.screenshot(path=str(path))
        print(path)
    page.close()
    return missed


## @brief Entry point.
## @utility
## v2: exits with the number of missed taps; three view sizes.
## @version 2
def main():
    root, out, steps = (
        Path(sys.argv[1]),
        Path(sys.argv[2]),
        json.loads(Path(sys.argv[3]).read_text()),
    )
    out.mkdir(parents=True, exist_ok=True)
    url = serve(root)
    with sync_playwright() as p:
        browser = p.firefox.launch()
        missed = sum(run_view(browser, url, out, view, steps) for view in VIEWS)
        browser.close()
    sys.exit(missed)


if __name__ == "__main__":
    main()
