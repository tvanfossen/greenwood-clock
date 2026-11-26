## @file mock_clock.py
## @brief End-to-end test of the phone panel against a stand-in clock: serves sdcard/www and
## the panel's API contract (POST /api/pair, GET /api/panel, POST /api/settings,
## POST /api/schedule, all but pairing behind a bearer token), opens the panel in headless
## Firefox the way a scanned QR code does, taps through it, and checks what reached the
## "clock". Exit status: the number of failed checks.
## v2: an upload (a picture through the file chooser arrives as a 1024x600 PNG background).
## v3: serves the gzipped twins (*.gz) like the clock, and checks the panel loaded through them.
## v4: no global settings: the showing profile is edited from the menu (a customized theme
## kept by name, a brightness), and the clock gets both.
## v5: Settings > Location: an address looked up by the clock and the place it matched kept.
## v6: typing through the native input (the phone's keyboard); the upload through the crop.
## v7: the Library: thumbnails from the clock, a used background's delete refused, a rename and
## a delete reaching the clock, a theme duplicated under a new name.
## @version 7
"""Usage: mock_clock.py <sdcard/www dir> <out dir for screenshots>"""

import functools
import http.server
import io
import json
import struct
import sys
import threading
import urllib.parse
from pathlib import Path

from PIL import Image

from playwright.sync_api import sync_playwright

CODE = "0f3a9c1d2e4b5a6978c0d1e2f3a4b5c6"
TOKEN = "5e55105e55105e55105e55105e55105e"
CLOCK_W, CLOCK_H = 1024, 600
LOOKUP_MATCH = {
    "label": "Chicago, IL",
    "lat": 41.883,
    "lon": -87.632,
}  # Chicago City Hall
SDCARD = Path(__file__).resolve().parents[2] / "sdcard"
PICTURE = SDCARD / "backgrounds" / "spring-flower.png"
THUMB = (170, 100)  # LIB_THUMB_W x LIB_THUMB_H


## @brief The stand-in clock's state and what it received.
## v2: uploads recorded.
## @utility
## @version 2
class Clock:
    ## @brief The stand-in clock's starting schedule; no posts, the code unused.
    ## @utility
    ## v2: uploads recorded.
    ## v3: gzipped files served recorded.
    ## v4: the Library (Christmas uses winter_morning).
    ## @version 4
    def __init__(self):
        self.code_used = False
        self.gzipped = []
        self.posts = []
        self.uploads = []
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
                    "image": "A:/backgrounds/winter_morning.png",
                },
            ],
        }
        self.thumbs = []
        self.renamed = {}  # new name -> the sample file
        self.library = {
            "backgrounds": [
                "campfire",
                "night_winter",
                "winter_afternoon",
                "winter_morning",
            ],
            "album": ["spring-flower"],
            "themes": ["default", "sassafras"],
            "anims": ["Snowfall", "None"],
        }

    ## @brief GET /api/library's body for @p kind.
    ## @utility
    ## @version 1
    ## @return The JSON text.
    def library_list(self, kind):
        items = [
            {
                "name": n,
                "label": n.capitalize() if kind == "themes" else n,
                "size": 900000,
                "mtime": 1795000000,
                "thumb": kind in ("backgrounds", "album"),
            }
            for n in self.library.get(kind, [])
        ]
        return json.dumps({"items": items, "total_mb": 30436, "free_mb": 27911})

    ## @brief A picture's thumbnail, made from the sample file as the clock makes it.
    ## @utility
    ## @version 1
    ## @return PNG bytes, or None.
    def thumb(self, name):
        src = SDCARD / "backgrounds" / f"{self.renamed.get(name, name)}.png"
        if not src.is_file():
            return None
        self.thumbs.append(name)
        out = io.BytesIO()
        Image.open(src).convert("RGB").resize(THUMB, Image.BOX).save(out, "PNG")
        return out.getvalue()

    ## @brief Who uses background @p name ("" when none).
    ## @utility
    ## @version 1
    ## @return The first profile's name.
    def used_by(self, kind, name):
        ref = f"A:/backgrounds/{name}.png" if kind == "backgrounds" else name
        key = {"backgrounds": "image", "themes": "theme", "anims": "anim"}.get(kind)
        users = [
            p["name"] for p in self.schedule["profiles"] if key and p.get(key) == ref
        ]
        return users[0] if users else ""

    ## @brief POST /api/library/delete or /rename: change the lists as the clock does.
    ## @utility
    ## @version 1
    ## @return (status, body).
    def library_change(self, path, req):
        kind, items = req.get("kind"), self.library.get(req.get("kind"), [])
        name = req.get("from") if path.endswith("rename") else req.get("name")
        who = self.used_by(kind, name) if path.endswith("delete") else ""
        if name not in items or who:
            return 409, json.dumps(
                {"error": f"Used by {who}" if who else "No such item"}
            )
        if path.endswith("rename"):
            items[items.index(name)] = req.get("to")
            self.renamed[req.get("to")] = self.renamed.get(name, name)
        else:
            items.remove(name)
        return 200, '{"status":"ok"}'

    ## @brief GET /api/panel's body.
    ## @utility
    ## v2: no global "values".
    ## v3: the location.
    ## @version 3
    ## @return The state JSON text.
    def state(self):
        return json.dumps(
            {
                "version": "1.0.0",
                "lists": {
                    "backgrounds": [["A:/backgrounds/campfire.png", "campfire"]],
                    "themes": [["default", "Default"], ["sassafras", "Sassafras"]],
                    "anims": [["Snowfall", "Snowfall"], ["None", "None"]],
                },
                "schedule": self.schedule,
                "now": {"date": "2026-11-24", "min": 13 * 60},
                "location": {
                    "label": "Chicago, IL",
                    "lat": 41.88,
                    "lon": -87.63,
                    "tz": "America/Chicago",
                },
                "sun": [[7 * 60, 17 * 60]] * 366,
            }
        )


## @brief Request handler: static files, then the API contract.
## @utility
## v2: gzipped twins.
## @version 2
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
    ## v2: a file's gzipped twin when the browser takes gzip (as the clock does).
    ## v3: the Library.
    ## @version 3
    def do_GET(self):
        if self.path.startswith("/api/library"):
            return self._library_get()
        if self.path.startswith("/api/panel"):
            ok = self._authed()
            return self._send(
                200 if ok else 401, self.clock.state() if ok else '{"error":"scan"}'
            )
        if self.path.startswith("/www/"):
            self.path = self.path[len("/www") :]
        twin = Path(self.directory) / f"{self.path.lstrip('/')}.gz"
        if "gzip" in self.headers.get("Accept-Encoding", "") and twin.is_file():
            return self._send_gzipped(twin)
        return super().do_GET()

    ## @brief GET /api/library, /thumb, /theme (token needed).
    ## @utility
    ## @version 1
    def _library_get(self):
        if not self._authed():
            return self._send(401, '{"error":"scan"}')
        url = urllib.parse.urlparse(self.path)
        q = dict(urllib.parse.parse_qsl(url.query))
        if url.path == "/api/library":
            return self._send(200, self.clock.library_list(q.get("kind")))
        if url.path == "/api/library/theme":
            f = SDCARD / "themes" / f"{q.get('stem')}.json"
            return self._send(
                200 if f.is_file() else 404, f.read_text() if f.is_file() else "{}"
            )
        png = self.clock.thumb(q.get("name", ""))
        if png is None:
            return self._send(404, '{"error":"No thumbnail yet"}')
        self.send_response(200)
        self.send_header("Content-Type", "image/png")
        self.send_header("Content-Length", str(len(png)))
        self.end_headers()
        self.wfile.write(png)

    ## @brief Send @p twin (a .gz file) as its original file, gzip-encoded.
    ## @utility
    ## @version 1
    def _send_gzipped(self, twin):
        data = twin.read_bytes()
        self.clock.gzipped.append(twin.stem)
        self.send_response(200)
        self.send_header("Content-Type", self.guess_type(twin.stem))
        self.send_header("Content-Encoding", "gzip")
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    ## @brief POST: pairing, or a change (token needed) that the clock records.
    ## @utility
    ## v2: uploads go to _upload.
    ## v3: an address lookup answers the sample match (a public building).
    ## v4: the Library's delete and rename.
    ## @version 4
    def do_POST(self):
        raw = self.rfile.read(int(self.headers.get("Content-Length", 0)))
        if self.path.startswith("/api/upload"):
            return self._upload(raw)
        body = raw.decode()
        if self.path == "/api/pair":
            ok = json.loads(body).get("code") == CODE and not self.clock.code_used
            self.clock.code_used = self.clock.code_used or ok
            return self._send(
                200 if ok else 403, json.dumps({"token": TOKEN}) if ok else "{}"
            )
        if not self._authed():
            return self._send(401, '{"error":"scan"}')
        self.clock.posts.append((self.path, body))
        if self.path == "/api/location/lookup":
            return self._send(200, json.dumps(LOOKUP_MATCH))
        if self.path == "/api/schedule":
            self.clock.schedule = json.loads(body)
        if self.path.startswith("/api/library/"):
            return self._send(*self.clock.library_change(self.path, json.loads(body)))
        return self._send(200, '{"status":"ok"}')

    ## @brief POST /api/upload: record the query and the PNG's size (token needed).
    ## @utility
    ## @version 1
    def _upload(self, raw):
        if not self._authed():
            return self._send(401, '{"error":"scan"}')
        is_png = raw[:8] == b"\x89PNG\r\n\x1a\n"
        size = struct.unpack(">II", raw[16:24]) if is_png else None
        self.clock.uploads.append((self.path, size))
        return self._send(200 if is_png else 400, '{"status":"success"}')


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


## @brief The failed checks of editing the showing profile (Afternoon at the stand-in's
## 13:00): the customized theme was kept as "ro", and the saved schedule has Afternoon on
## that theme at a brightness above the starting 60 %.
## @utility
## @version 1
## @return The failures.
def profile_fails(posts):
    fails = []
    if not any(path == "/api/theme/save?stem=ro" for path, _ in posts):
        fails.append(
            f"the customized theme was not kept as 'ro': {[p for p, _ in posts]}"
        )
    saved = [json.loads(body) for path, body in posts if path == "/api/schedule"]
    after = [
        p for s in saved[:1] for p in s.get("profiles", []) if p["name"] == "Afternoon"
    ]
    if (
        not after
        or after[0].get("theme") != "ro"
        or after[0].get("brightness", 0) <= 60
    ):
        fails.append(
            f"Afternoon not saved with theme 'ro' and a brighter screen: {after}"
        )
    return fails


## @brief Edit the showing profile from the menu: customize a theme, name it "ro", raise the
## brightness, save.
## @utility
## v2: text through the native input; the upload through the crop.
## @version 2
def edit_now(page):
    tap(page, 320, 176)  # Now: Afternoon
    tap(page, 395, 330)  # Theme
    tap(page, 400, 170)  # Customize...
    tap(page, 927, 561)  # the editor's Save
    page.fill("#text-input", "ro")  # the phone's own keyboard (a native input)
    page.click("#text-done")  # kept as "ro"
    page.wait_for_timeout(600)
    tap(page, 378, 438)  # Brightness
    tap(page, 700, 168)  # the slider, near the top
    tap(page, 832, 106)  # Done
    page.screenshot(path=str(Path(serve.out) / "e2e_now_profile.png"))
    tap(page, 832, 106)  # Save the profile
    page.wait_for_timeout(800)


## @brief Settings > Location: type an address, the clock looks it up, the place is kept.
## @utility
## v2: text through the native input; the upload through the crop.
## v3: taps by label (the menu grows).
## @version 3
## @return The failures.
def set_location(page, clock):
    tap_label(page, "Location")
    tap_label(page, "Chicago, IL")  # the place
    page.fill("#text-input", "c")  # the phone's own keyboard
    page.click("#text-done")  # looked up
    page.wait_for_timeout(800)
    page.screenshot(path=str(Path(serve.out) / "e2e_location_found.png"))
    tap(page, 832, 106)  # Use
    page.wait_for_timeout(800)
    kept = [json.loads(b) for p, b in clock.posts if p == "/api/location"]
    looked = [json.loads(b) for p, b in clock.posts if p == "/api/location/lookup"]
    fails = (
        [] if looked and looked[0].get("address") == "c" else [f"no lookup: {looked}"]
    )
    ok = (
        kept
        and abs(kept[0]["lat"] - 41.883) < 0.001
        and kept[0]["tz"] == "America/Chicago"
    )
    return fails + ([] if ok else [f"the place was not kept: {kept}"])


## @brief Pair, load, save a schedule profile, upload a background picture; then a second
## phone with the used code is refused.
## v2: the upload.
## v3: the panel loads through its gzipped twins.
## v4: a brightness change posts a valid body.
## v5: the showing profile edited from the menu (theme kept by name, brightness); the new menu.
## v6: the location.
## @utility
## v7: text through the native input; the upload through the crop.
## v8: the Library.
## v9: the upload from the Library's Add (no Upload menu).
## @version 9
## @return The failed checks' descriptions.
def run(base, out):
    clock, fails = serve.clock, []
    with sync_playwright() as p:
        browser = p.firefox.launch()
        page = browser.new_page(viewport={"width": 1024, "height": 600})
        page.on("console", lambda m: print(f"[panel] {m.type}: {m.text}"))
        page.on("pageerror", lambda e: print(f"[panel] page error: {e}"))
        page.goto(f"{base}/www/panel/index.html#{CODE}")
        page.wait_for_function("window.panelReady === true", timeout=20000)
        page.wait_for_timeout(600)
        if "#" in page.url:
            fails.append("the code stayed in the address bar")
        if not {"panel.js", "panel.wasm"} <= set(clock.gzipped):
            fails.append(f"the panel did not load gzipped: {clock.gzipped}")
        edit_now(page)
        fails += profile_fails(clock.posts)
        tap(page, 704, 176)  # Schedule
        page.screenshot(path=str(out / "e2e_schedule.png"))
        tap(page, 400, 250)  # Morning
        tap(page, 832, 106)  # Save
        page.wait_for_timeout(800)
        page.screenshot(path=str(out / "e2e_saved.png"))
        if not any(path == "/api/schedule" for path, _ in clock.posts):
            fails.append("the schedule save did not reach the clock")
        tap(page, 190, 106)  # Back to the menu
        fails += set_location(page, clock)
        tap(page, 190, 106)  # Back to the menu
        tap_label(page, "Library")
        tap_label(page, "Backgrounds")
        page.screenshot(path=str(out / "e2e_upload.png"))
        with page.expect_file_chooser() as chooser:
            tap_label(page, "Add")  # a background: choose a picture
        chooser.value.set_files(str(PICTURE))
        page.wait_for_selector(
            "#crop-send", timeout=5000
        )  # cropped in the clock's frame
        page.wait_for_timeout(900)  # the real clock drawn over it
        page.screenshot(path=str(out / "e2e_crop.png"))
        page.click("#crop-send")
        page.wait_for_timeout(2500)
        page.screenshot(path=str(out / "e2e_uploaded.png"))
        if not any(
            "kind=background" in q and dims == (1024, 600) for q, dims in clock.uploads
        ):
            fails.append(f"no 1024x600 background arrived: {clock.uploads}")
        fails += library_check(page, clock, out)
        other = browser.new_page()
        other.goto(f"{base}/www/panel/index.html#{CODE}")
        other.wait_for_timeout(1500)
        if "Scan the QR code" not in other.inner_text("#note"):
            fails.append("a second phone with the used code was not refused")
        browser.close()
    return fails


## @brief Tap the control showing @p label (shots.tap: by what it shows, not by pixel).
## @utility
## @version 1
## @return Whether one was found.
def tap_label(page, label):
    from shots import tap as shots_tap

    return shots_tap(page, label)


## @brief The Library: thumbnails from the clock; a used background cannot be deleted; a rename
## and a delete reach the clock; a theme is duplicated under a new name.
## @utility
## v2: starts at the Backgrounds the upload left.
## @version 2
## @return The failed checks' descriptions.
def library_check(page, clock, out):
    fails = []
    for label in ("\uf053", "Backgrounds"):  # from the upload: the list afresh
        tap_label(page, label)
    page.wait_for_timeout(1500)  # the thumbnails, one by one
    page.screenshot(path=str(out / "e2e_library.png"))
    if len(set(clock.thumbs)) < 4:
        fails.append(f"the grid did not fetch every thumbnail: {clock.thumbs}")
    for label in ("winter_morning", "Delete", "Tap again"):
        tap_label(page, label)
    page.screenshot(path=str(out / "e2e_used.png"))
    if any(p == "/api/library/delete" for p, _ in clock.posts):
        fails.append("a background a profile uses was sent for deletion")
    for label in ("\uf053", "campfire", "Rename"):
        tap_label(page, label)
    page.fill("#text-input", "campfire_2")
    page.click("#text-done")
    page.wait_for_timeout(800)
    for label in ("campfire_2", "Delete", "Tap again"):
        tap_label(page, label)
    page.wait_for_timeout(800)
    page.screenshot(path=str(out / "e2e_deleted.png"))
    sent = [(p, json.loads(b)) for p, b in clock.posts if p.startswith("/api/library/")]
    want = [
        (
            "/api/library/rename",
            {"kind": "backgrounds", "from": "campfire", "to": "campfire_2"},
        ),
        ("/api/library/delete", {"kind": "backgrounds", "name": "campfire_2"}),
    ]
    if sent != want:
        fails.append(f"rename then delete did not reach the clock: {sent}")
    for label in ("\uf053", "Themes", "Sassafras", "Duplicate"):
        tap_label(page, label)
    page.wait_for_timeout(500)
    page.screenshot(path=str(out / "e2e_duplicate.png"))
    page.wait_for_selector("#text-input", timeout=3000)
    page.fill("#text-input", "Rose")
    page.click("#text-done")
    page.wait_for_timeout(800)
    if not any(p == "/api/theme/save?stem=rose" for p, _ in clock.posts):
        fails.append("the duplicated theme was not kept as rose")
    return fails


## @brief Entry point.
## @utility
## v2: prints the uploads.
## v3: the screenshot folder for edit_now.
## @version 3
def main():
    root, out = Path(sys.argv[1]), Path(sys.argv[2])
    out.mkdir(parents=True, exist_ok=True)
    clock = Clock()
    serve.clock = clock
    serve.out = out
    fails = run(serve(root, clock), out)
    for path, body in clock.posts:
        print(f"clock received {path}: {body[:120]}")
    for query, dims in clock.uploads:
        print(f"clock received {query}: PNG {dims}")
    for f in fails:
        print(f"FAIL: {f}")
    print(f"panel e2e: {'ok' if not fails else f'{len(fails)} failed'}")
    sys.exit(len(fails))


if __name__ == "__main__":
    main()
