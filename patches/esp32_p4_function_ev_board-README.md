# BSP patch: espressif/esp32_p4_function_ev_board 5.0.0~1 (NOT APPLIED)

`esp32_p4_function_ev_board-5.0.0-backlight-i2c.patch` fixes two boot warnings:

1. `bsp_display_brightness_init()` is made idempotent. Upstream calls it from both
   `bsp_display_start_with_config()` and `bsp_display_new_with_handles()`. The second
   LEDC claim logs `ledc: GPIO 26 is not usable, maybe conflict with others`.
2. The touch I2C bus sets `flags.enable_internal_pullup = true`. This silences
   `i2c.master: Please check pull-up resistances`. The board already has external pull-ups.

## Why it is not applied

Applying it means vendoring the BSP, as `components/` or via `override_path`, and any
such dependency change forces a component-manager re-solve. On this ESP-IDF
(`v5.5-beta1`), every re-solve fails with `MissingKconfigError: LV_USE_FREETYPE`:

- The solver evaluates the optional deps of **every** registry version matching a spec.
  It does this for `esp_lvgl_port`'s `lvgl/lvgl >=8,<10`, even though our fork in
  `components/lvgl` is what gets built.
- `lvgl/lvgl >= 9.6.0` declares `if: idf_version >=5.5 && $CONFIG{LV_USE_FREETYPE} == True`.
- The beta's `tools/cmake/build.cmake` does not pass `--sdkconfig_json_file` to the
  solver, so no Kconfig key resolves. Released v5.5.1+ passes it.

Fix path: move to a released ESP-IDF 5.5.x, then vendor the BSP and apply this patch.
Both changes are also worth reporting upstream (github.com/espressif/esp-bsp).
