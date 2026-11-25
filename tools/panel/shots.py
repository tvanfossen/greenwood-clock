## @file shots.py
## @brief Screenshot the phone panel (sdcard/www/panel) in a real browser: serve it locally,
## open it in headless Firefox (Playwright) at a desktop and a phone-landscape size, tap
## through the given steps and save one PNG per step; browser console errors are printed.
## @version 2
"""Usage: shots.py <panel dir> <out dir> <steps.json>

steps.json: [{"name": "menu", "taps": [[x, y], ...], "drags": [[x, y, dx, dy], ...]}, ...]
in clock pixels (1024 x 600); each step's drags then taps run after the previous step's,
then a screenshot is taken.
"""

import functools
import http.server
import json
import sys
import threading
from pathlib import Path

from playwright.sync_api import sync_playwright

VIEWS = {"desktop": (1024, 600, 1), "phone": (844, 390, 3)}
CLOCK_W, CLOCK_H = 1024, 600


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


## @brief Tap clock pixel (@p x, @p y) on the page's canvas, wherever it is laid out.
## @utility
## @version 1
def tap(page, x, y):
    box = page.locator("canvas").bounding_box()
    page.mouse.click(
        box["x"] + x * box["width"] / CLOCK_W, box["y"] + y * box["height"] / CLOCK_H
    )
    page.wait_for_timeout(400)


## @brief Drag from clock pixel (@p x, @p y) by (@p dx, @p dy) (scrolls a list).
## @utility
## @version 1
def drag(page, x, y, dx, dy):
    box = page.locator("canvas").bounding_box()
    sx, sy = box["width"] / CLOCK_W, box["height"] / CLOCK_H
    page.mouse.move(box["x"] + x * sx, box["y"] + y * sy)
    page.mouse.down()
    for i in range(1, 11):
        page.mouse.move(
            box["x"] + (x + dx * i / 10) * sx, box["y"] + (y + dy * i / 10) * sy
        )
        page.wait_for_timeout(16)
    page.mouse.up()
    page.wait_for_timeout(700)


## @brief Run every step at one view size, saving <out>/<view>_<step>.png.
## v2: drags before taps.
## @utility
## @version 2
def run_view(browser, url, out, view, steps):
    w, h, scale = VIEWS[view]
    page = browser.new_page(
        viewport={"width": w, "height": h}, device_scale_factor=scale
    )
    page.on(
        "console", lambda m: m.type == "error" and print(f"[{view}] console: {m.text}")
    )
    page.on("pageerror", lambda e: print(f"[{view}] page error: {e}"))
    page.goto(url)
    page.wait_for_function("window.panelReady === true", timeout=20000)
    page.wait_for_timeout(600)
    for step in steps:
        for x, y, dx, dy in step.get("drags", []):
            drag(page, x, y, dx, dy)
        for x, y in step.get("taps", []):
            tap(page, x, y)
        path = out / f"{view}_{step['name']}.png"
        page.screenshot(path=str(path))
        print(path)
    page.close()


## @brief Entry point.
## @utility
## @version 1
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
        for view in VIEWS:
            run_view(browser, url, out, view, steps)
        browser.close()


if __name__ == "__main__":
    main()
