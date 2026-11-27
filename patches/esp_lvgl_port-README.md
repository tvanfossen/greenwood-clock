# esp_lvgl_port patch: espressif/esp_lvgl_port 2.7.2 (APPLIED, vendored)

Vendored in `components/espressif__esp_lvgl_port/`: the pristine registry copy with
`esp_lvgl_port-2.7.2-self-wake.patch` applied. `main/idf_component.yml` points the dependency
at it with `override_path` (a same-named `components/` directory alone did not replace the
locked registry copy).

The patch makes `lvgl_port_task_wake()` return at once when the LVGL task calls it itself. An
invalidation made inside `lv_timer_handler` (an animation timer) already resumes the refresh
timer, and the task only sleeps until that is due, so setting the event bits as well only made
it loop once more. Snowfall invalidates ~140 areas a frame from its timer; measured on the
device, invalidation went from 3.5 to 2.85 ms a frame.
