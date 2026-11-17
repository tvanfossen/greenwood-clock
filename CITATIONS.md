# Citations

Sources behind physically-modelled behaviour in this firmware. Each entry says
what was taken from it, where in the source it appears, and which code uses it.
Values that are artistic choices rather than measurements are listed separately
under **Design parameters** so the two are never confused.

## Snowfall clock animation

Code: `components/display_fsm/src/snow_physics.c` (model),
`components/display_fsm/include/snow_physics.h` (parameters),
`tests/host/test_snow_physics.c` (checks the values below).

### [1] Locatelli & Hobbs (1974) — fall speed

Locatelli, J. D., and P. V. Hobbs (1974). *Fall speeds and masses of solid
precipitation particles.* Journal of Geophysical Research, 79(15), 2185–2197.
doi:[10.1029/JC079i015p02185](https://doi.org/10.1029/JC079i015p02185)

Used:

- **Terminal fall speed of unrimed aggregates: V = 0.81 · D^0.16**, V in m/s, D the
  maximum dimension in mm. Read from Fig. 20, p. 2192 (best-fit curve labelled
  "unrimed aggregates"). Constants `SNOW_FALL_SPEED_A`, `SNOW_FALL_SPEED_B`.
- **Range check:** aggregates of unrimed dendrites 1–14 mm in size fall at
  0.5–1.4 m/s (Conclusions, p. 2197). The test asserts the fit stays inside this
  band for every D in 1–14 mm, and the flake size range (1–10 mm) is chosen inside it.

### [2] Kajikawa (1982) — fall pattern

Kajikawa, M. (1982). *Observation of the falling motion of early snow flakes.
Part I. Relationship between the free-fall pattern and the number and shape of
component snow crystals.* Journal of the Meteorological Society of Japan, Ser. II,
60(2), 797–803. doi:[10.2151/jmsj1965.60.2_797](https://doi.org/10.2151/jmsj1965.60.2_797)

Used (abstract):

- About **80% of snowflakes fall in a spiral or rotation pattern**; steady fall is
  rare. Hence `steady_fraction = 0.20`: one flake in five falls without sway.

### [3] Kajikawa (1989) — velocity fluctuations

Kajikawa, M. (1989). *Observation of the falling motion of early snowflakes.
Part II: On the variation of falling velocity.* Journal of the Meteorological
Society of Japan, Ser. II, 67(5), 731–738.
doi:[10.2151/jmsj1965.67.5_731](https://doi.org/10.2151/jmsj1965.67.5_731)

Used (abstract):

- **Horizontal velocity standard deviation 1–8 cm/s.** Each swaying flake draws
  σ uniformly from that range; the sway is sinusoidal, so its peak velocity is
  √2·σ (`sway_sigma_min_mps`, `sway_sigma_max_mps`).
- **Vertical velocity standard deviation 3–5% of the mean fall speed.**
  `vjitter_rel = 0.04` (peak √2 · 4% on a sinusoid).

### [4] Gunn & Marshall (1958) — size distribution (form only)

Gunn, K. L. S., and J. S. Marshall (1958). *The distribution with size of
aggregate snowflakes.* Journal of Meteorology, 15(5), 452–461.
doi:[10.1175/1520-0469(1958)015<0452:TDWSOA>2.0.CO;2](https://doi.org/10.1175/1520-0469(1958)015%3C0452:TDWSOA%3E2.0.CO;2)

Used:

- **Exponential size distribution N(D) ∝ exp(−Λ·D)**, sampled by inverse CDF
  and truncated to the flake size range. Only the *functional form* is taken
  from this paper. Its fitted Λ(R) (as a function of precipitation rate) could not
  be read from an accessible copy and was **not** used: Λ is a design parameter
  (see below).

### [5] Langer et al. (2004) — perspective in the image plane

Langer, M. S., L. Zhang, A. W. Klein, A. Bhatia, J. Pereira, and D. Rekhi (2004).
*A spectral-particle hybrid method for rendering falling snow.* Eurographics
Symposium on Rendering, 217–226.
doi:[10.2312/EGWR/EGSR04/217-226](https://doi.org/10.2312/EGWR/EGSR04/217-226)

Used:

- Under linear perspective, a flake's **image speed and image size both scale
  with 1/depth** (the image-plane relation the paper derives for falling snow).
  `snow_px_per_m()` applies focal/z to both motion and radius, which gives the
  depth parallax. The paper's spectral-texture renderer itself is not used.

### Design parameters (not from the literature)

| Parameter | Value | Reason |
|---|---|---|
| `focal_px` | 643 | ~50° vertical field of view over 600 px |
| `z_near_m` / `z_far_m` | 4 / 14 m | Gives a gentle 4–13 s screen crossing |
| `lambda_per_mm` | 0.35 /mm | Slope of [4]'s exponential; favours small flakes |
| `wind_mps` | 0.15 m/s | Barely-there breeze |
| `sway_period_*_s` | 2.5–5 s | Spiral period. [2] relates it to amplitude without giving accessible absolute values |
| `size_gain` | 10× | Real flakes are sub-pixel at these depths; exaggerated so they read on screen |
| opacity fade | 255 → 110 with depth | Aerial-perspective cue |

## Falling-leaves clock animation

Code: `components/display_fsm/src/leaf_physics.c` (model),
`components/display_fsm/src/leaf_field.c` (screen mapping),
`tests/host/test_leaf_physics.c` and `tests/host/test_leaf_field.c` (checks).

### [6] Andersen, Pesavento & Wang (2005b) — falling-card model (used)

Andersen, A., U. Pesavento, and Z. J. Wang (2005). *Analysis of transitions between
fluttering, tumbling and steady descent of falling cards.* Journal of Fluid
Mechanics, 541, 91–104. doi:[10.1017/S0022112005005847](https://doi.org/10.1017/S0022112005005847)

Used:

- **Equations of motion:** the thin-card limit, eqs. 5.1–5.3, with circulation
  (4.7), drag (4.8) and dissipative torque (4.9), in the non-dimensional units of
  eq. 4.1 (L = a, U = √((ρs/ρf − 1)·g·b)).
- **Coefficients:** C_T = 1.2, C_R = π, A = 1.4, B = 1.0, µ₁ = µ₂ = 0.2 (§5.2).
- **Regimes:** §5.2 reports periodic fluttering at I\* = 1.1 and period-one tumbling
  at I\* = 1.4. The host tests assert both. Per-leaf I\* is drawn from 0.9–1.6, so
  most leaves flutter and some tumble.

### [7] Andersen, Pesavento & Wang (2005a) — measured forces (consulted)

Andersen, A., U. Pesavento, and Z. J. Wang (2005). *Unsteady aerodynamics of
fluttering and tumbling plates.* Journal of Fluid Mechanics, 541, 65–90.
doi:[10.1017/S002211200500594X](https://doi.org/10.1017/S002211200500594X)

Consulted, not used directly. Its dimensional water-tank model (eqs. 6.1–6.11,
Table 4) was implemented first, using the ideal-fluid torque arm l_τ = 0. It
reproduced the measured descent speed for β = 1/14 (−10.3 vs −9.0 cm/s, Table 1),
but it settled into steady broadside fall instead of fluttering. That paper leaves
l_τ undetermined (§6.4), so the companion model [6], which closes the torque with
µ₁ and µ₂, was adopted rather than tuning l_τ to fit.

### Leaf design parameters (not from the literature)

| Parameter | Value | Reason |
|---|---|---|
| Leaf density / air density | 500 / 1.2 kg·m⁻³ | Typical dry leaf. Gives U ≈ 0.89 m/s via eq. 4.1, with b = 0.2 mm |
| `semi_chord_m` | 3 cm | A 6 cm leaf |
| I\* range | 0.9–1.6 | Mostly flutter. A real dry leaf is I\* ≈ 2.7, which [6] places in the *chaotic* regime, and that would not look gentle |
| `z_near_m` / `z_far_m` | 3 / 8 m | Mid-depth leaf crosses the screen in ~10 s |
| `size_gain` | 2× | Drawn size only; motion uses the true projection [5] |
| Count, palette, kite shape | 18; red / orange / gold / brown / rust | Visual |
