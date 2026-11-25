## @file mock_clock.py
## @brief End-to-end test of the phone panel against a stand-in clock: serves sdcard/www and
## the panel's API contract (POST /api/pair, GET /api/panel, POST /api/settings,
## POST /api/schedule, all but pairing behind a bearer token), opens the panel in headless
## Firefox the way a scanned QR code does, taps through it, and checks what reached the
## "clock". Exit status: the number of failed checks.
## @version 1
"""Usage: mock_clock.py <sdcard/www dir> <out dir for screenshots>"""

import functools
import http.server
import json
import sys
import threading
from pathlib import Path

from playwright.sync_api import sync_playwright

CODE = "0f3a9c1d2e4b5a6978c0d1e2f3a4b5c6"
TOKEN = "5e55105e55105e55105e55105e55105e"
CLOCK_W, CLOCK_H = 1024, 600


## @brief The stand-in clock's state and what it received.
## @utility
## @version 1
class Clock:
    ## @brief The stand-in clock's starting schedule; no posts, the code unused.
    ## @utility
    ## @version 1
    def __init__(self):
        self.code_used = False
        self.posts = []
        self.schedule = {
            "version": 1,
            "profiles": [
                {
                    "name": "Morning",
                    "repeat": "daily",
                    "start": "07:00",
                    "end": "12:00",
                },
                {
                    "name": "Afternoon",
                    "repeat": "daily",
                    "start": "12:00",
                    "end": "sunset",
                },
                {
                    "name": "Evening",
                    "repeat": "daily",
                    "start": "sunset",
                    "end": "22:00",
                },
                {
                    "name": "Night",
                    "repeat": "daily",
                    "start": "22:00",
                    "end": "07:00",
                    "brightness": 12,
                },
                {
                    "name": "Christmas",
                    "repeat": "yearly",
                    "from": "12-24",
                    "to": "12-25",
                },
            ],
        }

    ## @brief GET /api/panel's body.
    ## @utility
    ## @version 1
    ## @return The state JSON text.
    def state(self):
        return json.dumps(
            {
                "version": "1.0.0",
                "values": {
                    "brightness": 60,
                    "background": "A:/backgrounds/campfire.png",
                    "theme": "default",
                    "anim": "Snowfall",
                },
                "lists": {
                    "backgrounds": [["A:/backgrounds/campfire.png", "campfire"]],
                    "themes": [["default", "Default"], ["sassafras", "Sassafras"]],
                    "anims": [["Snowfall", "Snowfall"], ["None", "None"]],
                },
                "schedule": self.schedule,
                "now": {"date": "2026-11-24", "min": 13 * 60},
                "sun": [[7 * 60, 17 * 60]] * 366,
            }
        )


## @brief Request handler: static files, then the API contract.
## @utility
## @version 1
class Handler(http.server.SimpleHTTPRequestHandler):
    clock = None

    ## @brief Quiet: no per-request log lines.
    ## @utility
    ## @version 1
    def log_message(self, *args):
        pass

    ## @brief Send @p body as JSON with @p status.
    ## @utility
    ## @version 1
    def _send(self, status, body):
        data = body.encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    ## @brief Whether the request carries the session token.
    ## @utility
    ## @version 1
    ## @return Whether it does.
    def _authed(self):
        return self.headers.get("Authorization") == f"Bearer {TOKEN}"

    ## @brief GET: the panel state (token needed) or a static file under /www.
    ## @utility
    ## @version 1
    def do_GET(self):
        if self.path.startswith("/api/panel"):
            ok = self._authed()
            return self._send(
                200 if ok else 401, self.clock.state() if ok else '{"error":"scan"}'
            )
        if self.path.startswith("/www/"):
            self.path = self.path[len("/www") :]
        return super().do_GET()

    ## @brief POST: pairing, or a change (token needed) that the clock records.
    ## @utility
    ## @version 1
    def do_POST(self):
        body = self.rfile.read(int(self.headers.get("Content-Length", 0))).decode()
        if self.path == "/api/pair":
            ok = json.loads(body).get("code") == CODE and not self.clock.code_used
            self.clock.code_used = self.clock.code_used or ok
            return self._send(
                200 if ok else 403, json.dumps({"token": TOKEN}) if ok else "{}"
            )
        if not self._authed():
            return self._send(401, '{"error":"scan"}')
        self.clock.posts.append((self.path, body))
        if self.path == "/api/schedule":
            self.clock.schedule = json.loads(body)
        return self._send(200, '{"status":"ok"}')


## @brief Serve @p root with the stand-in clock.
## @utility
## @version 1
## @return The base URL.
def serve(root, clock):
    Handler.clock = clock
    handler = functools.partial(Handler, directory=str(root))
    srv = http.server.ThreadingHTTPServer(("127.0.0.1", 0), handler)
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    return f"http://127.0.0.1:{srv.server_address[1]}"


## @brief Tap clock pixel (@p x, @p y).
## @utility
## @version 1
def tap(page, x, y):
    box = page.locator("canvas").bounding_box()
    page.mouse.click(
        box["x"] + x * box["width"] / CLOCK_W, box["y"] + y * box["height"] / CLOCK_H
    )
    page.wait_for_timeout(500)


## @brief Pair, load, change the brightness and save a schedule profile; then a second phone
## with the used code is refused.
## @utility
## @version 1
## @return The failed checks' descriptions.
def run(base, out):
    clock, fails = serve.clock, []
    with sync_playwright() as p:
        browser = p.firefox.launch()
        page = browser.new_page(viewport={"width": 1024, "height": 600})
        page.goto(f"{base}/www/panel/index.html#{CODE}")
        page.wait_for_function("window.panelReady === true", timeout=20000)
        page.wait_for_timeout(600)
        if "#" in page.url:
            fails.append("the code stayed in the address bar")
        tap(page, 320, 248)  # Schedule
        page.screenshot(path=str(out / "e2e_schedule.png"))
        tap(page, 400, 250)  # Morning
        tap(page, 832, 106)  # Save
        page.wait_for_timeout(800)
        page.screenshot(path=str(out / "e2e_saved.png"))
        if not any(path == "/api/schedule" for path, _ in clock.posts):
            fails.append("the schedule save did not reach the clock")
        other = browser.new_page()
        other.goto(f"{base}/www/panel/index.html#{CODE}")
        other.wait_for_timeout(1500)
        if "Scan the QR code" not in other.inner_text("#note"):
            fails.append("a second phone with the used code was not refused")
        browser.close()
    return fails


## @brief Entry point.
## @utility
## @version 1
def main():
    root, out = Path(sys.argv[1]), Path(sys.argv[2])
    out.mkdir(parents=True, exist_ok=True)
    clock = Clock()
    serve.clock = clock
    fails = run(serve(root, clock), out)
    for path, body in clock.posts:
        print(f"clock received {path}: {body[:120]}")
    for f in fails:
        print(f"FAIL: {f}")
    print(f"panel e2e: {'ok' if not fails else f'{len(fails)} failed'}")
    sys.exit(len(fails))


if __name__ == "__main__":
    main()
