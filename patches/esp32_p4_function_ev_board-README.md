# BSP patch: espressif/esp32_p4_function_ev_board 5.0.0~1 (APPLIED, vendored)

The BSP is vendored in `components/espressif__esp32_p4_function_ev_board/`: the
pristine registry copy with `esp32_p4_function_ev_board-5.0.0-backlight-i2c.patch` applied.
A `components/` directory with the managed name overrides the registry package.

The patch fixes two boot warnings:

1. `bsp_display_brightness_init()` is made idempotent. Upstream calls it from both
   `bsp_display_start_with_config()` and `bsp_display_new_with_handles()`. The second
   LEDC claim logs `ledc: GPIO 26 is not usable, maybe conflict with others`.
2. The touch I2C bus sets `flags.enable_internal_pullup = true`. This silences
   `i2c.master: Please check pull-up resistances`. The board already has external pull-ups.

## History

Vendoring was blocked until 2026-11-17. ESP-IDF `v5.5-beta1` never passed `sdkconfig.json`
to the component-manager solver. Registry `lvgl/lvgl >= 9.6.0` has
`$CONFIG{LV_USE_FREETYPE}` rules, so every re-solve died with `MissingKconfigError`. The
project now builds with released ESP-IDF v5.5.6 (`tasks.py` `IDF_PATH`), which fixes this.

## Updating

Download the new upstream version from the registry, re-apply this patch, regenerate the
patch if it changed, and update this file. Both changes are worth reporting upstream
(github.com/espressif/esp-bsp).
