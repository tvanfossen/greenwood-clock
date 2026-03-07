# Lottie Generation Walkthrough

## Setup

1. Open a new Claude conversation (separate from the main clock project)
2. Paste the contents of `SYSTEM_PROMPT.md` as the system/initial context
3. Follow the procedural steps below, one animation at a time

## Procedure for Each Animation

### Step 1: Request

Copy-paste this template, filling in values from `MANIFEST.md`:

```
Generate the following Lottie animation:

File: [filename from manifest]
Category: [category path, e.g., "weather/day"]
Description: [description from manifest]
Canvas: [WxH from manifest]
Max shapes: [max shapes from manifest]

Requirements:
- Transparent background
- Seamless loop
- [any additional notes from manifest, e.g., "seekable by frame"]
```

### Step 2: Save

Save Claude's JSON output to: `output/[category]/[filename]`

Example: `output/weather/day/clear.json`

### Step 3: Validate (optional)

Open the JSON in https://lottiefiles.com/preview or any Lottie player to verify:
- Animation plays correctly
- Loop is seamless
- Shape count is within limits
- Colors match the Arctic Observatory theme

### Step 4: Deploy to Device

```bash
# From project root
python tools/file_push.py output/weather/day/clear.json /lottie/weather/day/clear.json
```

### Step 5: Test on Device

Force the weather state via web control or API:
```bash
curl -X POST http://greenwood-clock.local/api/display/state \
  -H "Content-Type: application/json" \
  -d '{"state": "weather"}'
```

Check serial monitor for render timing and any ThorVG errors.

### Step 6: Iterate if Needed

If the animation is too slow (frame drops) or doesn't render correctly:
- Ask Claude to reduce shape count
- Simplify gradients to solid fills
- Reduce canvas size
- Remove masks/mattes

Then repeat from Step 2.

## Generation Order (Recommended)

Follow the priority column in the manifest. Generate HIGH priority first:

### Round 1 — HIGH Priority (Core Weather + Surprise)
1. `weather/day/clear.json` (#1)
2. `weather/day/partly_cloudy.json` (#2)
3. `weather/day/mostly_cloudy.json` (#3)
4. `weather/day/rain.json` (#5)
5. `weather/day/thunderstorm.json` (#7)
6. `weather/day/snow.json` (#8)
7. `weather/night/clear.json` (#13)
8. `weather/night/partly_cloudy.json` (#14)
9. `weather/night/rain.json` (#17)
10. `astro/moon_cycle.json` (#23)
11. `surprise/hearts.json` (#28)
12. `surprise/confetti.json` (#29)
13. `ui/loading.json` (#35)

### Round 2 — MEDIUM Priority
14-27. Remaining MED items from manifest

### Round 3 — LOW Priority
28-40. Remaining LOW items from manifest

## Directory Structure

```
tools/lottie_gen/
├── SYSTEM_PROMPT.md      ← System prompt for the generation Claude instance
├── MANIFEST.md           ← Full animation manifest with specs
├── WALKTHROUGH.md        ← This file — procedural generation guide
└── output/               ← Generated JSON files (create as you go)
    ├── weather/
    │   ├── day/
    │   │   ├── clear.json
    │   │   ├── partly_cloudy.json
    │   │   └── ...
    │   └── night/
    │       ├── clear.json
    │       └── ...
    ├── astro/
    │   ├── moon_cycle.json
    │   └── ...
    ├── surprise/
    │   ├── hearts.json
    │   └── ...
    ├── ui/
    │   ├── loading.json
    │   └── ...
    └── ambient/
        ├── sun_arc.json
        └── ...
```

## Notes

- Each animation is independent — you can skip around if one is problematic
- The device currently has the hummingbird Lottie working at 20fps (250x250, ~60 shapes)
  so 300x300 with 10-15 shapes should perform well
- Seekable animations (moon_cycle, sun_arc, uv_gauge) use time remapping — the code
  will call `lv_lottie_set_progress()` to jump to the correct frame
- Weather condition Lottie files are loaded when the WeatherOverlay state enters —
  only one plays at a time, never concurrent weather animations
