# Arctic Observatory — Design Philosophy

A visual language for the Greenwood Clock's Lottie animations. Every animation in this
system shares a unified aesthetic: clean, warm, contemplative — like watching weather
instruments through the window of a Nordic research station.

---

## Color Theory

### Day Palette

The day palette builds tension between warmth and coolness. The sun is golden amber
(#E9C46A), a color that reads as warm daylight without veering into saturated cartoon
yellow. It sits against steel blue (#4a6fa5), a desaturated blue that evokes overcast
Nordic skies and brushed metal instrument housings. These two form the primary
complementary axis: warm focal element against cool environmental context.

Cloud shapes use a blue-gray gradient from #8ba5c4 (thin cloud) to #5a7799 (dense
overcast). This keeps clouds in the same cool family as the steel blue accent without
competing with it. Rain and precipitation inherit the darker end of this range.

The warm accent coral (#e76f51) appears sparingly — lightning flashes, alert states,
celebration elements. It has high contrast against both the cool and neutral tones,
making it an effective signal color. Use it for moments of energy, never for ambient
background elements.

The cool accent teal (#2d8b8b) bridges the warm and cool palettes. It reads as neither
warm nor cold, making it ideal for UI elements that should feel neutral-functional:
loading spinners, wifi indicators, location pins. Teal says "system status" rather
than "weather" or "celebration."

### Night Palette

Night shifts the value range downward while maintaining hue relationships. The deep
background tone moves to navy-purple (#1a1a3e), but since our backgrounds are
transparent, this manifests in the shapes themselves. The moon is silver-white (#fafafa)
with just enough warmth to avoid feeling clinical. Stars share this silver but at reduced
scale and varied opacity.

Night clouds darken to #3a4f6e — still recognizably blue, but with enough purple
undertone to feel nocturnal. The key night technique is value contrast over hue
contrast: shapes define themselves by being lighter or darker than their neighbors,
not by color difference. This creates the low-light atmosphere without needing a
background layer.

Lavender (#a490c2) replaces steel blue as the night accent. It maintains the cool
temperature but adds the purple cast that signals nighttime. Stars twinkle between
lavender and silver-white.

### Shared Accents

Coral (#e76f51) and teal (#2d8b8b) are time-of-day agnostic. They carry the same
meaning regardless of palette context:

| Color | Hex | Role | Examples |
|-------|-----|------|----------|
| Coral | #e76f51 | Energy, alert, celebration | Lightning, hearts, alerts, fireworks |
| Teal | #2d8b8b | System, functional, calm | Loading, wifi, location, gauges |

### Opacity as a First-Class Tool

Translucency is not a fallback — it is a primary compositional device. Fog is layered
bands at 20-40% opacity. Cloud depth comes from overlapping shapes at 70-90% opacity
rather than from different fill colors. Rain streaks at 50-70% opacity feel like water;
at 100% they feel like wires. Snow at 60-80% opacity catches imaginary light.

Every shape should have its opacity considered as deliberately as its color. The default
is NOT 100%.

---

## Motion Language

### Easing Vocabulary

All motion uses cubic bezier easing. The two primary curves:

- **Organic ease**: `[0.42, 0, 0.58, 1]` — symmetric ease-in-out. Used for breathing,
  pulsing, drifting. Anything that should feel alive.
- **Soft decelerate**: `[0.25, 0.1, 0.25, 1]` — ease-out with gentle start. Used for
  elements entering the scene, expanding, settling.

Linear easing (`[0, 0, 1, 1]`) is reserved exclusively for gravity-driven motion:
rain falling, ice pellets, confetti descent after burst. Constant velocity = physics,
not aesthetics.

Never leave easing handles empty or undefined — ThorVG defaults to linear, which
makes organic animations feel robotic.

### Tempo

Animations fall into two tempo categories:

| Category | Loop duration | Frame count (60fps) | Examples |
|----------|-------------|---------------------|----------|
| Ambient | 4-6 seconds | 240-360 frames | Weather, astronomy, ambient dashboard |
| Functional | 1-2 seconds | 60-120 frames | UI spinners, alerts, loading states |

Ambient animations should feel like they could loop forever without the viewer noticing
the seam. Functional animations should feel responsive and purposeful.

### Rhythm and Phase Offset

When multiple instances of a shape repeat (rain drops, snow flakes, confetti), they
MUST NOT start at the same phase. Use the repeater's offset property or stagger
keyframe start times so particles feel random rather than synchronized.

Good rain: 8 drops, each starting at a different vertical position, reaching the bottom
at different times. Bad rain: 8 drops in lockstep like a curtain.

### Motion Hierarchy

Every animation has a primary, secondary, and (optionally) tertiary motion layer:

1. **Primary**: The defining movement. Sun ray pulsing, rain falling, heart floating up.
   Largest amplitude, most visible.
2. **Secondary**: Supporting movement. Cloud drifting, star twinkling, subtle scale
   breathing. Smaller amplitude, slower tempo.
3. **Tertiary**: Barely perceptible. Slight opacity fluctuation, micro-drift. The viewer
   feels it more than sees it.

If an animation has too many elements at the primary motion level, it becomes chaotic
and distracting on a clock face that runs 24/7.

---

## Shape Vocabulary

### Weather Shapes (Organic)

Weather elements use soft, rounded forms. Clouds are built from 2-4 overlapping
ellipses — never a single blob, never a detailed outline. The overlap creates natural
density variation. Sun rays are thin rounded rectangles scaled out from center, rotated
via repeater.

Rain is thin vertical lines with rounded caps (stroke, not fill). Snow is small circles
or simple 6-pointed polystars. Ice pellets are small diamonds (rotated squares). These
are simple enough to repeat 8-12 times via repeater without performance cost.

Lightning is a 3-4 segment polyline path — jagged but not noisy. Two or three straight
segments with sharp angle changes. Thin stroke, high opacity flash.

### Astronomy Shapes (Precise)

Moon and stars use geometric precision. The crescent moon is two overlapping circles —
one bright, one dark (the "bite"). Stars are polystars with specific inner/outer radius
ratios. The sun arc is a clean semicircular path.

These shapes have deliberate mathematical proportions. A 6-pointed star has inner radius
at 40% of outer. The crescent offset is exactly calculated for the target lunar phase.

### UI Shapes (Functional)

UI elements are the most geometric: perfect circles for dots, clean arcs for wifi
signals, precise pins for location. These shapes prioritize instant readability at
small canvas sizes (40-60px). No organic softness — crisp, aligned, purposeful.

### Celebration Shapes (Expressive)

Celebration animations get the most variety: hearts (bezier paths), confetti (small
rotated rectangles), fireworks (radial burst of small circles), streamers (wavy paths).
These are the only category where slight irregularity is intentional — confetti pieces
vary in size, hearts drift at slightly different speeds.

---

## Composition Principles

### Anchor to Center

Every animation has a visual anchor. For weather: the sun, moon, or primary cloud. For
UI: the icon itself. For celebration: the burst origin point. Secondary elements orbit,
drift from, or relate spatially to this anchor.

Do not scatter elements randomly across the canvas. Even "scattered" effects like
confetti burst FROM a center point.

### Transparent Background Contract

All animations composite over the clock UI. There is no background layer. This means:

- Shapes must be self-contained — they define their own visual boundaries
- No shape should depend on a background color for contrast
- Edge-touching shapes should fade to transparent via opacity, not end abruptly
- The canvas boundary is a soft edge, not a hard crop

### Scale for Target Display

The 1024x600 display shows these animations at specific sizes within the UI layout.
A 300x300 weather animation occupies roughly 30% of the display width. At that scale:

- Minimum visible shape: ~8px (a rain drop or star)
- Comfortable text-replacement shape: ~20px (UI icon detail)
- Primary focal shape: ~80-150px (sun disc, moon, large cloud)

Design shapes to read clearly at the rendered size, not the authored size. A 300x300
canvas displayed at 300x300 physical pixels on a 1024-wide display has good fidelity —
do not over-detail shapes that will render at 1:1.

### Negative Space

The most important design element is what ISN'T there. A clear sky animation with a
sun and 6 rays has 80%+ transparent space — and that's correct. The animation lives
within a larger UI; it should breathe, not fill.

Resist the urge to add shapes to fill empty canvas. If the primary motion reads
clearly and the composition is balanced, stop.
