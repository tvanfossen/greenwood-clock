# Lottie Animation Manifest

Complete list of all animations needed for the Greenwood Clock.
Generate in the order listed — dependencies flow top to bottom.

## Category 1: Weather Conditions — Day (`A:/lottie/weather/day/`)

| # | File | Description | Canvas | Max Shapes | Priority |
|---|------|-------------|--------|------------|----------|
| 1 | `clear.json` | Sun with gentle ray pulsing, warm golden glow | 300x300 | 10 | HIGH |
| 2 | `partly_cloudy.json` | Sun partially occluded by one drifting cloud | 300x300 | 12 | HIGH |
| 3 | `mostly_cloudy.json` | 2-3 clouds with subtle horizontal drift | 300x300 | 10 | HIGH |
| 4 | `overcast.json` | Dense layered clouds, very slow movement | 300x300 | 8 | MED |
| 5 | `rain.json` | Dark cloud with rain streaks falling downward | 300x300 | 15 | HIGH |
| 6 | `drizzle.json` | Light cloud with sparse, thin drops | 300x300 | 12 | MED |
| 7 | `thunderstorm.json` | Dark cloud + lightning flash + rain streaks | 300x300 | 15 | HIGH |
| 8 | `snow.json` | Cloud with snowflakes drifting down (use repeater) | 300x300 | 12 | HIGH |
| 9 | `ice.json` | Cloud with angular ice pellets falling fast | 300x300 | 12 | MED |
| 10 | `fog.json` | Layered translucent horizontal bands drifting | 300x300 | 8 | MED |
| 11 | `windy.json` | Stylized wind lines/swirls moving right | 300x300 | 8 | LOW |
| 12 | `haze.json` | Subtle shimmer/heat distortion effect | 300x300 | 6 | LOW |

## Category 2: Weather Conditions — Night (`A:/lottie/weather/night/`)

Same conditions but with moon/stars replacing sun, darker tones.

| # | File | Description | Canvas | Max Shapes | Priority |
|---|------|-------------|--------|------------|----------|
| 13 | `clear.json` | Crescent moon + 3-4 twinkling stars | 300x300 | 10 | HIGH |
| 14 | `partly_cloudy.json` | Moon partially behind one drifting cloud | 300x300 | 12 | HIGH |
| 15 | `mostly_cloudy.json` | Moon mostly hidden, clouds drift | 300x300 | 10 | MED |
| 16 | `overcast.json` | Dense clouds, no moon visible, very dark | 300x300 | 8 | LOW |
| 17 | `rain.json` | Dark cloud + rain against dark background | 300x300 | 15 | HIGH |
| 18 | `drizzle.json` | Cloud + sparse drops, moonlight tint | 300x300 | 12 | LOW |
| 19 | `thunderstorm.json` | Dark cloud + lightning illuminating scene | 300x300 | 15 | MED |
| 20 | `snow.json` | Snowflakes against dark sky, subtle star twinkle | 300x300 | 12 | MED |
| 21 | `ice.json` | Ice pellets against dark sky | 300x300 | 12 | LOW |
| 22 | `fog.json` | Dark foggy layers, barely visible stars | 300x300 | 8 | LOW |

## Category 3: Astronomy (`A:/lottie/astro/`)

| # | File | Description | Canvas | Max Shapes | Priority |
|---|------|-------------|--------|------------|----------|
| 23 | `moon_cycle.json` | Full lunation cycle (new→full→new), **seekable by frame**. Frame 0=new moon, frame 120=full moon, frame 240=end. Use opacity/mask to reveal illuminated portion. | 200x200 | 8 | HIGH |
| 24 | `sunrise.json` | Sun rising over a horizon line, light rays expanding upward | 300x150 | 10 | MED |
| 25 | `sunset.json` | Sun descending below horizon, warm→cool color shift | 300x150 | 10 | MED |
| 26 | `aurora.json` | Shimmering curtain of green/purple vertical light bands | 400x200 | 12 | MED |
| 27 | `star_twinkle.json` | Single star with opacity + scale pulse (instanced N times by code) | 30x30 | 3 | LOW |

## Category 4: Surprise / Celebration (`A:/lottie/surprise/`)

| # | File | Description | Canvas | Max Shapes | Priority |
|---|------|-------------|--------|------------|----------|
| 28 | `hearts.json` | Hearts floating upward with gentle drift (use repeater) | 500x500 | 10 | HIGH |
| 29 | `confetti.json` | Multicolor confetti burst and fall (use repeater) | 600x400 | 12 | HIGH |
| 30 | `fireworks.json` | Firework burst with sparkle trails expanding from center | 500x500 | 15 | MED |
| 31 | `sparkles.json` | Scattered sparkle/star effects pulsing at random phases | 400x400 | 10 | MED |
| 32 | `birthday_cake.json` | Simple cake silhouette with flickering candle flame | 300x300 | 10 | MED |
| 33 | `flowers.json` | 2-3 flowers blooming open (petal shapes scaling out) | 300x300 | 12 | LOW |
| 34 | `celebration.json` | Generic party — streamers + stars bursting outward | 500x500 | 12 | MED |

## Category 5: UI Elements (`A:/lottie/ui/`)

| # | File | Description | Canvas | Max Shapes | Priority |
|---|------|-------------|--------|------------|----------|
| 35 | `loading.json` | Circular loading spinner (3 dots orbiting) | 60x60 | 4 | HIGH |
| 36 | `wifi_connecting.json` | WiFi icon with signal wave arcs pulsing outward | 60x60 | 5 | MED |
| 37 | `location_pin.json` | Map pin with expanding pulse ring around base | 40x60 | 4 | MED |
| 38 | `alert_pulse.json` | Warning triangle icon with red pulse ring | 60x60 | 5 | LOW |

## Category 6: Ambient Dashboard (`A:/lottie/ambient/`)

| # | File | Description | Canvas | Max Shapes | Priority |
|---|------|-------------|--------|------------|----------|
| 39 | `sun_arc.json` | Sun traversing a semicircular arc path. **Seekable**: frame 0=sunrise, frame 120=noon, frame 240=sunset. | 400x200 | 6 | MED |
| 40 | `uv_gauge.json` | Circular gauge with fill level. **Seekable**: frame maps to UV index 0-11. | 100x100 | 5 | LOW |

**Total: 40 animation files** across 6 categories.
