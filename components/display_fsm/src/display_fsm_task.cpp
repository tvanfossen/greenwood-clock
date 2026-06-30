// components/display_fsm/src/display_fsm_task.cpp
//
// FreeRTOS task that owns the event queue and dispatches to TinyFSM.
// All LVGL operations happen under lvgl_port_lock in this task.

#include "display_fsm.h"
#include "display_fsm_base.h"
#include "display_states.h"
#include "display_widgets.h"
#include "display_scheduler.h"
#include "image_decode.h"
#include "ui_contrast.h"

#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_heap_caps.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "bsp/display.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "freertos/idf_additions.h"
#include "settings.h"
#include "time_sync.h"
#include "sdcard.h"
#include "nws.h"

#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <sys/stat.h>
#include <errno.h>

static const char *TAG = "fsm_task";

#define FSM_QUEUE_DEPTH   16
#define FSM_TASK_STACK     (16 * 1024)  // stb_image PNG inflate needs ~8KB stack
#define FSM_TASK_PRIORITY  5

static QueueHandle_t s_event_queue = NULL;

// ---------------------------------------------------------------------------
// Shared Lottie loader task — persistent, SPIRAM stack, queue-driven.
// All Lottie JSON parsing (ThorVG/RapidJSON recursive descent) funnels here
// so no other task needs a deep stack.
// ---------------------------------------------------------------------------

#if LV_USE_LOTTIE

#define LOTTIE_LOADER_STACK  (64 * 1024)
#define LOTTIE_LOADER_QUEUE  4

static QueueHandle_t s_loader_queue = NULL;

// Read a Lottie JSON file into a fresh SPIRAM buffer (off the LVGL lock).
static uint8_t *read_lottie_file(const char *path, size_t *size_out)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        ESP_LOGE(TAG, "lottie_load: open failed '%s' errno=%d", path, errno);
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *buf = (n > 0) ? (uint8_t *)heap_caps_malloc((size_t)n, MALLOC_CAP_SPIRAM) : NULL;
    if (buf && fread(buf, 1, (size_t)n, f) == (size_t)n) {
        *size_out = (size_t)n;
    } else {
        heap_caps_free(buf);   // free(NULL) safe
        buf = NULL;
        ESP_LOGE(TAG, "lottie_load: read failed '%s' (size=%ld)", path, n);
    }
    fclose(f);
    return buf;
}

// Attach already-read Lottie data and retime to the target FPS. Holds the LVGL
// lock across BOTH parse and finalize: the widget may be destroyed (e.g. user
// swipes away from the weather screen) while we read the file off-lock. Widget
// destruction runs under the LVGL lock on the FSM task, so taking the lock here
// and validating the widget BEFORE touching it makes the parse+finalize atomic
// w.r.t. deletion — closing the use-after-free where the loader called
// tvg_animation_get_total_frame on a freed widget (this=0x1f8 crash).
// ThorVG copies the buffer (copy=true), so the caller frees it afterwards.
static void apply_lottie_src(const lottie_load_job_t *job, const uint8_t *data, size_t size)
{
    lvgl_port_lock(0);

    // lv_obj_is_valid walks the live object tree, so a freed/dangling widget
    // pointer is rejected safely (no deref of freed memory).
    if (!lv_obj_is_valid(job->widget)) {
        lvgl_port_unlock();
        ESP_LOGW(TAG, "lottie_load: widget gone before apply — skipping '%s'", job->path);
        return;
    }

    // ThorVG parse (a dedicated ThorVG mutex serializes it vs the render task)
    // then the fast attach + inline first render. Both under the LVGL lock.
    lv_lottie_parse_src_data(job->widget, data, size);
    lv_lottie_finalize_src(job->widget);
    if (job->target_fps > 0) {
        lv_anim_t *anim = lv_lottie_get_anim(job->widget);
        if (anim) {
            uint32_t dur = lv_anim_get_time(anim);
            lv_anim_set_duration(anim, dur * 60 / job->target_fps);
            anim->act_time = 0;
            lv_anim_start(anim);
        }
    }
    lvgl_port_unlock();
}

static void lottie_loader_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "Lottie loader task started (SP=%p)", __builtin_frame_address(0));

    lottie_load_job_t job;
    while (true) {
        if (xQueueReceive(s_loader_queue, &job, portMAX_DELAY) != pdTRUE)
            continue;

        ESP_LOGI(TAG, "lottie_load: path='%s'", job.path);

        // Read off-lock so the SD read (large for the 742KB hummingbird) is out
        // of the LVGL critical section; only parse+render hold the lock.
        size_t size = 0;
        uint8_t *data = read_lottie_file(job.path, &size);
        if (!data) continue;

        apply_lottie_src(&job, data, size);
        heap_caps_free(data);

        ESP_LOGI(TAG, "lottie_load: done '%s' (stack_hw=%lu B)",
                 job.path,
                 (unsigned long)uxTaskGetStackHighWaterMark(NULL) * sizeof(StackType_t));
    }
}

static void lottie_loader_init(void)
{
    if (s_loader_queue) return;
    s_loader_queue = xQueueCreate(LOTTIE_LOADER_QUEUE, sizeof(lottie_load_job_t));
    configASSERT(s_loader_queue);

    BaseType_t ret = xTaskCreateWithCaps(lottie_loader_task, "lottie_loader",
                                          LOTTIE_LOADER_STACK, NULL, 4, NULL,
                                          MALLOC_CAP_SPIRAM);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create Lottie loader task");
    }
}

#endif /* LV_USE_LOTTIE */

// NOTE: FSM_INITIAL_STATE(DisplayFsm, ClockFull) is in state_clock.cpp
// because it needs the full ClockFull definition.

// ---------------------------------------------------------------------------
// Event dispatch — maps C event struct to TinyFSM event dispatch
// ---------------------------------------------------------------------------

static void dispatch_event(const display_event_t *evt)
{
    switch (evt->type) {
        case DISPLAY_EVT_DISPLAY_TIMEOUT:
            DisplayFsm::dispatch(EvDisplayTimeout{});
            break;
        case DISPLAY_EVT_WEATHER_UPDATE:
            DisplayFsm::dispatch(EvWeatherUpdate{});
            break;
        case DISPLAY_EVT_FORECAST_UPDATE:
            DisplayFsm::dispatch(EvForecastUpdate{});
            break;
        case DISPLAY_EVT_ALERT_RECEIVED:
            DisplayFsm::dispatch(EvAlertReceived{});
            break;
        case DISPLAY_EVT_RADAR_READY:
            DisplayFsm::dispatch(EvRadarReady{});
            break;
        case DISPLAY_EVT_ASTRO_TRIGGER:
            DisplayFsm::dispatch(EvAstroTrigger{});
            break;
        case DISPLAY_EVT_PHOTO_TRIGGER:
            DisplayFsm::dispatch(EvPhotoTrigger{});
            break;
        case DISPLAY_EVT_AMBIENT_TRIGGER:
            DisplayFsm::dispatch(EvAmbientTrigger{});
            break;
        case DISPLAY_EVT_GESTURE: {
            EvGesture e;
            e.dir = evt->gesture.dir;
            DisplayFsm::dispatch(e);
            break;
        }
        case DISPLAY_EVT_SETTINGS_BACK:
            DisplayFsm::dispatch(EvSettingsBack{});
            break;
        case DISPLAY_EVT_SURPRISE_MESSAGE: {
            EvSurpriseMessage e;
            e.json_str   = evt->surprise.json_str;
            e.duration_s = evt->surprise.duration_s;
            DisplayFsm::dispatch(e);
            break;
        }
        case DISPLAY_EVT_FORCE_STATE: {
            EvForceState e;
            e.state = evt->force_state.state;
            DisplayFsm::dispatch(e);
            break;
        }
        case DISPLAY_EVT_SCHEDULE_CONFIG: {
            EvScheduleConfig e;
            e.cfg = evt->schedule;
            DisplayFsm::dispatch(e);
            break;
        }
        case DISPLAY_EVT_CLOCK_UPDATE: {
            EvClockUpdate e;
            memset(&e.time, 0, sizeof(e.time));
            e.time.tm_hour = evt->clock.tm_hour;
            e.time.tm_min  = evt->clock.tm_min;
            e.time.tm_sec  = evt->clock.tm_sec;
            e.time.tm_mday = evt->clock.tm_mday;
            e.time.tm_mon  = evt->clock.tm_mon;
            e.time.tm_year = evt->clock.tm_year;
            e.time.tm_wday = evt->clock.tm_wday;
            DisplayFsm::dispatch(e);
            break;
        }
        case DISPLAY_EVT_REFRESH_BG:
        case DISPLAY_EVT_PHOTO_ADVANCE:
            // Handled in process_event before dispatch; nothing to do here.
            break;
    }
}

// ---------------------------------------------------------------------------
// Background image loading (moved from ui.c)
// ---------------------------------------------------------------------------

static bool is_gif_path(const char *path)
{
    const char *ext = strrchr(path, '.');
    return ext && strcasecmp(ext, ".gif") == 0;
}

// Translate an LVGL drive path ("A:/..", "B:/..") to a POSIX mount path.
static void bg_lvgl_to_fs_path(const char *lvgl_path, char *out, size_t out_sz)
{
    if (lvgl_path[0] && lvgl_path[1] == ':') {
        const char *mount = (lvgl_path[0] == 'B' || lvgl_path[0] == 'b')
                              ? "/spiffs" : "/sdcard";
        snprintf(out, out_sz, "%s%s", mount, lvgl_path + 2);
    } else {
        snprintf(out, out_sz, "%s", lvgl_path);
    }
}

static lv_obj_t *s_bg_img = NULL;
static lv_image_dsc_t *s_bg_dsc = NULL;   // persistent decoded background (non-GIF)

// Predecode a non-GIF background PNG ONCE (outside the LVGL lock — SD I/O + a
// ~2.5MB alloc) into a persistent SPIRAM descriptor. Handing LVGL a static
// descriptor stops it re-decoding the full-screen bitmap from disk whenever its
// image cache evicts it under memory pressure — the root cause of clock-screen
// lag and the occasional blank background. NULL → caller uses on-demand decode.
// MUST run in a deep-stack task (fsm_task): stb_image inflate needs ~8KB stack.
static lv_image_dsc_t *predecode_bg(const char *path)
{
    char fs_path[160];
    bg_lvgl_to_fs_path(path, fs_path, sizeof(fs_path));
    // RGB565 (opaque): matches the framebuffer so the full-screen background
    // redraws as a straight copy during animations — no per-frame ARGB->RGB565
    // conversion.
    lv_image_dsc_t *dsc = image_decode_png_file_rgb565(fs_path);
    if (dsc) {
        // The RGB565 full-screen blit renders vertically flipped on this panel;
        // flip so the background is upright (confirmed on hardware).
        image_decode_flip_vertical(dsc);
    } else {
        ESP_LOGW(TAG, "bg: predecode failed for '%s' (%s) — on-demand fallback",
                 path, fs_path);
    }
    return dsc;
}

// Create a looping, screen-sized GIF background. Caller holds the LVGL lock;
// this briefly drops it for the GIF decoder init, then re-locks.
static lv_obj_t *create_gif_bg(lv_obj_t *scr, const char *path)
{
    lv_obj_t *obj = lv_gif_create(scr);
    lv_gif_set_src(obj, path);

    lvgl_port_unlock();
    vTaskDelay(pdMS_TO_TICKS(50));
    lvgl_port_lock(0);

    lv_obj_set_width(obj, lv_obj_get_width(lv_scr_act()));
    lv_obj_set_height(obj, lv_obj_get_height(lv_scr_act()));
    lv_gif_set_loop_count(obj, -1);
    lv_gif_restart(obj);
    return obj;
}

// Create a static image background from a predecoded descriptor, or fall back
// to LVGL's on-demand decode of the path.
static lv_obj_t *create_static_bg(lv_obj_t *scr, const char *path, lv_image_dsc_t *dsc)
{
    lv_obj_t *obj = lv_img_create(scr);
    if (dsc) {
        if (s_bg_dsc) image_decode_free(s_bg_dsc);
        s_bg_dsc = dsc;
        lv_img_set_src(obj, dsc);
    } else {
        lv_img_set_src(obj, path);   // fallback: LVGL on-demand decode
    }
    return obj;
}

static void apply_bg_common_flags(lv_obj_t *obj)
{
    lv_obj_align(obj, LV_ALIGN_CENTER, 0, 0);
    lv_obj_move_background(obj);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(obj, LV_OBJ_FLAG_IGNORE_LAYOUT);
}

// Verify the background loaded; delete + null it out on failure. Publishes s_bg_img.
static void finalize_bg(lv_obj_t *obj, bool gif, const char *path)
{
    bool loaded = gif ? lv_gif_is_loaded(obj) : (lv_img_get_src(obj) != NULL);
    if (!loaded) {
        ESP_LOGW(TAG, "bg: failed to load '%s'", path);
        lv_obj_delete(obj);
        obj = NULL;
    } else {
        ESP_LOGI(TAG, "bg: loaded '%s'", path);
    }
    s_bg_img = obj;
}

static void load_background(lv_obj_t *scr, const clock_settings_t *cfg)
{
    if (!sdcard_is_mounted() || !cfg || !cfg->background_image[0]) {
        ESP_LOGW(TAG, "bg: SD not mounted or no background configured");
        return;
    }
    const char *path = cfg->background_image;
    bool gif = is_gif_path(path);
    ESP_LOGI(TAG, "bg: loading '%s' (gif=%d)", path, (int)gif);

    lv_image_dsc_t *dsc = gif ? NULL : predecode_bg(path);

    lvgl_port_lock(0);
    lv_obj_t *obj = gif ? create_gif_bg(scr, path)
                        : create_static_bg(scr, path, dsc);
    apply_bg_common_flags(obj);
    finalize_bg(obj, gif, path);
    lvgl_port_unlock();
}

// Sample the (predecoded) background under the full clock and set the clock text
// colour to a guaranteed-legible variant of the user's chosen colour (keep hue,
// adjust OKLCH lightness). The bg buffer is vertically flipped vs the screen
// (the panel flips the RGB565 blit), so the clock's top screen region maps to the
// bottom of the buffer. GIF backgrounds have no sampleable buffer → use the raw
// colour.
static void apply_clock_bg_contrast(const clock_settings_t *cfg)
{
    clock_widget_t *clk = DisplayFsm::get_clock();
    if (!clk || !cfg) return;

    lv_color_t user = lv_color_make((cfg->text_color >> 16) & 0xFF,
                                    (cfg->text_color >> 8) & 0xFF,
                                    cfg->text_color & 0xFF);
    lvgl_port_lock(0);
    if (s_bg_dsc) {
        lv_area_t a;
        clock_widget_full_area(&a);
        int32_t ih = (int32_t)s_bg_dsc->header.h;
        int32_t rh = a.y2 - a.y1 + 1;
        int32_t by1 = ih - 1 - a.y2;            // screen-Y → buffer-Y (vertical flip)
        if (by1 < 0) by1 = 0;
        lv_color_t bg = ui_image_region_mean(s_bg_dsc, a.x1, by1, a.x2 - a.x1 + 1, rh);
        lv_color_t legible = ui_legible(user, bg);
        clock_widget_set_color(clk, legible);
        ESP_LOGI(TAG, "clock bg-contrast: bg=#%02X%02X%02X user=#%02X%02X%02X -> #%02X%02X%02X",
                 bg.red, bg.green, bg.blue, user.red, user.green, user.blue,
                 legible.red, legible.green, legible.blue);
    } else {
        clock_widget_set_color(clk, user);
    }
    lvgl_port_unlock();
}

// Delete the current background (object + persistent descriptor) and reload from
// settings. MUST run in fsm_task — load_background() decodes with a deep stack
// outside the LVGL lock.
static void reload_background_from_settings(void)
{
    lv_obj_t *scr = DisplayFsm::get_screen();
    if (!scr) {
        ESP_LOGW(TAG, "reload_bg: screen not initialized");
        return;
    }

    lvgl_port_lock(0);
    if (s_bg_img) {
        lv_anim_delete(s_bg_img, NULL);
        lv_obj_delete(s_bg_img);
        s_bg_img = NULL;
    }
    lvgl_port_unlock();

    // Object is gone; safe to free its descriptor (nothing references it now).
    if (s_bg_dsc) {
        image_decode_free(s_bg_dsc);
        s_bg_dsc = NULL;
    }

    clock_settings_t cfg;
    if (settings_load(&cfg) == ESP_OK) {
        load_background(scr, &cfg);
        apply_clock_bg_contrast(&cfg);   // re-derive legible clock colour for new bg
    }
}

// ---------------------------------------------------------------------------
// Lottie hummingbird loading (moved from ui.c)
// ---------------------------------------------------------------------------

#if LV_USE_LOTTIE

#define CLOCK_LOTTIE_W          200
#define CLOCK_LOTTIE_H          200
#define CLOCK_LOTTIE_FPS        20
#define CLOCK_LOTTIE_PATH       "/sdcard/lottie/hummingbird.json"

static lv_obj_t *s_clock_lottie_widget = NULL;
static void     *s_clock_lottie_buf    = NULL;

static void load_clock_lottie(lv_obj_t *scr)
{
    // Allocate stride-aligned SPIRAM render buffer
    uint32_t stride = lv_draw_buf_width_to_stride(CLOCK_LOTTIE_W,
                                                    LV_COLOR_FORMAT_ARGB8888_PREMULTIPLIED);
    size_t buf_sz = (size_t)stride * CLOCK_LOTTIE_H;
    s_clock_lottie_buf = heap_caps_aligned_alloc(64, buf_sz, MALLOC_CAP_SPIRAM);
    if (!s_clock_lottie_buf) {
        ESP_LOGE(TAG, "lottie: SPIRAM alloc failed (%zu B)", buf_sz);
        return;
    }

    lvgl_port_lock(0);
    lv_obj_t *widget = lv_lottie_create(scr);
    if (!widget) {
        ESP_LOGE(TAG, "lottie: lv_lottie_create failed");
        lvgl_port_unlock();
        heap_caps_free(s_clock_lottie_buf);
        s_clock_lottie_buf = NULL;
        return;
    }
    if (lv_lottie_render_failed(widget)) {
        ESP_LOGE(TAG, "lottie: render task failed");
        lv_obj_delete(widget);
        lvgl_port_unlock();
        heap_caps_free(s_clock_lottie_buf);
        s_clock_lottie_buf = NULL;
        return;
    }
    lv_obj_set_size(widget, CLOCK_LOTTIE_W, CLOCK_LOTTIE_H);
    lv_lottie_set_buffer(widget, CLOCK_LOTTIE_W, CLOCK_LOTTIE_H, s_clock_lottie_buf);
    lv_obj_align(widget, LV_ALIGN_BOTTOM_LEFT, 16, -16);
    s_clock_lottie_widget = widget;
    lvgl_port_unlock();

    // Submit to shared loader task (deep recursive JSON parse by ThorVG)
    lottie_load_job_t job = {};
    job.widget     = widget;
    job.target_fps = CLOCK_LOTTIE_FPS;
    strlcpy(job.path, CLOCK_LOTTIE_PATH, sizeof(job.path));
    display_fsm_load_lottie(&job);
}

#endif /* LV_USE_LOTTIE */

// ---------------------------------------------------------------------------
// Gesture callback — registered on the active screen
// ---------------------------------------------------------------------------

// Debounce: LVGL fires LV_EVENT_GESTURE multiple times during a single
// finger drag.  Without a cooldown, one physical swipe triggers 2+ state
// transitions (e.g. radar → astronomy → photos instead of radar → astronomy).
#define GESTURE_DEBOUNCE_MS 500
static uint32_t s_last_gesture_tick = 0;

static void screen_gesture_cb(lv_event_t *e)
{
    (void)e;
    uint32_t now = lv_tick_get();
    if (now - s_last_gesture_tick < GESTURE_DEBOUNCE_MS) return;

    lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());
    if (dir == LV_DIR_TOP || dir == LV_DIR_LEFT || dir == LV_DIR_RIGHT) {
        s_last_gesture_tick = now;
        display_event_t evt = {};
        evt.type = DISPLAY_EVT_GESTURE;
        evt.gesture.dir = dir;
        display_fsm_send_event(&evt);
    }
}

// ---------------------------------------------------------------------------
// Clock update timer — polls every second, only enqueues when minute changes
// ---------------------------------------------------------------------------

static int s_last_minute = -1;

// Pixel jitter for burn-in mitigation. Every 10 minutes, nudge the clock
// container by ±2 px on a pseudo-random walk bounded to ±4 px total. The
// translation is purely visual (LVGL translate_x/y) and does not interact
// with the mode/animation positioning logic.
static int8_t s_jitter_x;
static int8_t s_jitter_y;

static void clock_jitter_cb(TimerHandle_t timer)
{
    (void)timer;

    // 1D random walk per axis, clamped to ±4. Step is -1, 0, or +1.
    uint32_t r = esp_random();
    int step_x = ((int)(r        & 0x3)) - 1;  // 0..3 → -1, 0, 1, 1 (slight + bias OK)
    int step_y = ((int)((r >> 8) & 0x3)) - 1;
    int nx = (int)s_jitter_x + step_x;
    int ny = (int)s_jitter_y + step_y;
    if (nx >  4) nx =  4; else if (nx < -4) nx = -4;
    if (ny >  4) ny =  4; else if (ny < -4) ny = -4;
    s_jitter_x = (int8_t)nx;
    s_jitter_y = (int8_t)ny;

    clock_widget_t *cw = DisplayFsm::get_clock();
    if (cw) {
        lvgl_port_lock(0);
        clock_widget_set_jitter(cw, s_jitter_x, s_jitter_y);
        lvgl_port_unlock();
        ESP_LOGI(TAG, "Clock jitter applied: dx=%d dy=%d", s_jitter_x, s_jitter_y);
    }
}

static void clock_tick_cb(TimerHandle_t timer)
{
    (void)timer;
    time_t now;
    struct tm ti;
    time_sync_get_local(&now, &ti);

    // Display shows HH:MM only — skip update if minute hasn't changed
    int cur_min = ti.tm_hour * 60 + ti.tm_min;
    if (cur_min == s_last_minute) return;
    s_last_minute = cur_min;

    display_event_t evt = {};
    evt.type = DISPLAY_EVT_CLOCK_UPDATE;
    evt.clock.tm_hour = ti.tm_hour;
    evt.clock.tm_min  = ti.tm_min;
    evt.clock.tm_sec  = ti.tm_sec;
    evt.clock.tm_mday = ti.tm_mday;
    evt.clock.tm_mon  = ti.tm_mon;
    evt.clock.tm_year = ti.tm_year;
    evt.clock.tm_wday = ti.tm_wday;

    if (s_event_queue) {
        xQueueSend(s_event_queue, &evt, 0);
    }
}

// ---------------------------------------------------------------------------
// FSM task — single consumer of event queue
// ---------------------------------------------------------------------------

// Clean the active screen and build the persistent UI (clock, alert banner, dot
// indicator, gesture handler) on lv_layer_top(). Returns the screen object.
static lv_obj_t *setup_screen_widgets(const clock_settings_t *cfg)
{
    lvgl_port_lock(0);

    lv_obj_t *scr = lv_scr_act();
    DisplayFsm::set_screen(scr);

    lv_obj_clean(scr);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(scr, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);

    // Persistent UI lives on lv_layer_top() so it always renders above the
    // state-specific widgets on the screen.
    lv_obj_t *top = lv_layer_top();
    lv_obj_clear_flag(top, LV_OBJ_FLAG_CLICKABLE);  // pass-through clicks

    clock_widget_t *cw = clock_widget_create(top);
    if (cw) {
        lv_color_t color = lv_color_make(
            (cfg->text_color >> 16) & 0xFF,
            (cfg->text_color >>  8) & 0xFF,
             cfg->text_color        & 0xFF);
        clock_widget_set_color(cw, color);
        DisplayFsm::set_clock(cw);
        ESP_LOGI(TAG, "ClockWidget created on layer_top, color=0x%06lX",
                 (unsigned long)cfg->text_color);
    } else {
        ESP_LOGE(TAG, "Failed to create ClockWidget");
    }

    alert_banner_t *ab = alert_banner_create(top);
    if (ab) {
        DisplayFsm::set_alert_banner(ab);
        ESP_LOGI(TAG, "AlertBanner created on layer_top");
    } else {
        ESP_LOGW(TAG, "Failed to create AlertBanner");
    }

    DisplayFsm::set_top_layer(top);
    DisplayFsm::create_dot_indicator();
    lv_obj_add_event_cb(scr, screen_gesture_cb, LV_EVENT_GESTURE, NULL);

    lvgl_port_unlock();
    return scr;
}

// Clock minute-tick poll (1s) + burn-in pixel jitter (10 min) timers.
static void start_periodic_timers(void)
{
    TimerHandle_t clock_timer = xTimerCreate("clk_tick", pdMS_TO_TICKS(1000),
                                             pdTRUE, NULL, clock_tick_cb);
    if (clock_timer) {
        xTimerStart(clock_timer, 0);
        ESP_LOGI(TAG, "Clock tick timer started (1s poll, minute-change update)");
    }

    TimerHandle_t jitter_timer = xTimerCreate("clk_jitter", pdMS_TO_TICKS(10 * 60 * 1000),
                                              pdTRUE, NULL, clock_jitter_cb);
    if (jitter_timer) {
        xTimerStart(jitter_timer, 0);
        ESP_LOGI(TAG, "Clock jitter timer started (10 min, ±4 px walk)");
    }
}

// Handle one dequeued event. Radar pre-decode and background reload run OUTSIDE
// the LVGL lock (slow stb_image work); everything else dispatches to the FSM.
// The [XFER] log reports how long the FSM held the LVGL lock during a transition
// — i.e. how long the display could not render. Useful for catching regressions
// where a heavy entry() builds too much under the lock.
static void process_event(const display_event_t *evt)
{
    if (evt->type == DISPLAY_EVT_RADAR_READY) {
        size_t rlen = 0;
        const uint8_t *rpng = nws_get_radar_png(&rlen);
        if (rpng && rlen > 0) radar_view_predecode(rpng, rlen);
    }
    if (evt->type == DISPLAY_EVT_REFRESH_BG) {
        reload_background_from_settings();
        return;   // not a state event — no FSM dispatch
    }
    if (evt->type == DISPLAY_EVT_PHOTO_ADVANCE) {
        // Decode the next photo OFF the LVGL lock (slow PNG decode), then swap
        // it in under the lock (fast). No-op if the photo state has exited.
        PhotoSlideshow::decode_next();    // off-lock (instant if prefetched)
        lvgl_port_lock(0);
        PhotoSlideshow::present();          // under lock: fast swap
        PhotoSlideshow::apply_clock_contrast();  // legible clock over this photo
        lvgl_port_unlock();
        PhotoSlideshow::prefetch();       // off-lock: decode next ahead
        return;   // not an FSM state event
    }

    lvgl_port_lock(0);
    int64_t t0 = esp_timer_get_time();   // measure HOLD only (post-acquire)
    dispatch_event(evt);
    int64_t hold = esp_timer_get_time() - t0;
    lvgl_port_unlock();

    if (hold > 20000) {
        ESP_LOGW(TAG, "[XFER] lock-hold=%lld us evt=%d — display could not render this long",
                 (long long)hold, evt->type);
    }
}

static void fsm_run_event_loop(void)
{
    display_event_t evt;
    while (true) {
        if (xQueueReceive(s_event_queue, &evt, portMAX_DELAY) == pdTRUE) {
            process_event(&evt);
        }
    }
}

static void fsm_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "FSM task started");

    clock_settings_t cfg;
    settings_load(&cfg);

    lv_obj_t *scr = setup_screen_widgets(&cfg);

    // Background + Lottie manage their own LVGL lock.
    load_background(scr, &cfg);
    apply_clock_bg_contrast(&cfg);   // legible clock colour over the bg image
#if LV_USE_LOTTIE
    load_clock_lottie(scr);
#endif

    // Preload the radar basemap off-lock so the first radar transition doesn't
    // stall on a ~2.4 MB SD read while holding the LVGL lock.
    radar_view_preload_map();

    display_scheduler_get()->init();

    lvgl_port_lock(0);
    DisplayFsm::start();   // enters ClockFull::entry() which does LVGL ops
    lvgl_port_unlock();

    start_periodic_timers();
    clock_tick_cb(NULL);   // initial clock update

    ESP_LOGI(TAG, "FSM initialized, entering event loop");
    fsm_run_event_loop();
}

// ---------------------------------------------------------------------------
// C API implementations
// ---------------------------------------------------------------------------

extern "C" void display_fsm_init(void)
{
    if (s_event_queue) {
        ESP_LOGW(TAG, "display_fsm_init: already initialized");
        return;
    }

    s_event_queue = xQueueCreate(FSM_QUEUE_DEPTH, sizeof(display_event_t));
    configASSERT(s_event_queue);

#if LV_USE_LOTTIE
    lottie_loader_init();
#endif

    BaseType_t ret = xTaskCreate(fsm_task, "fsm_task", FSM_TASK_STACK,
                                  NULL, FSM_TASK_PRIORITY, NULL);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create FSM task");
    }
}

extern "C" bool display_fsm_send_event(const display_event_t *evt)
{
    if (!s_event_queue || !evt) return false;
    BaseType_t ret = xQueueSend(s_event_queue, evt, pdMS_TO_TICKS(100));
    if (ret != pdTRUE) {
        ESP_LOGW(TAG, "Event queue full, dropping event type=%d", evt->type);
    }
    return ret == pdTRUE;
}

extern "C" const char *display_fsm_get_state_name(void)
{
    return DisplayFsm::get_state_name();
}

extern "C" display_state_id_t display_fsm_get_state_id(void)
{
    return DisplayFsm::get_state_id();
}

extern "C" clock_widget_t *display_fsm_get_clock(void)
{
    return DisplayFsm::get_clock();
}

extern "C" lv_obj_t *display_fsm_get_screen(void)
{
    return DisplayFsm::get_screen();
}

extern "C" void display_fsm_refresh_background(void)
{
    // Callable from the LVGL/settings callback context (shallow stack). The
    // reload decodes a PNG (stb_image needs ~8KB stack) outside the LVGL lock,
    // so defer the work to fsm_task via the event queue rather than running it
    // here.
    display_event_t evt;
    memset(&evt, 0, sizeof(evt));
    evt.type = DISPLAY_EVT_REFRESH_BG;
    if (!display_fsm_send_event(&evt)) {
        ESP_LOGW(TAG, "refresh_bg: failed to enqueue reload");
    }
}

extern "C" void display_fsm_show_clock_lottie(void)
{
#if LV_USE_LOTTIE
    if (s_clock_lottie_widget) {
        // Resume from cache — no re-parse needed
        lvgl_port_lock(0);
        lv_obj_clear_flag(s_clock_lottie_widget, LV_OBJ_FLAG_HIDDEN);
        lv_lottie_resume(s_clock_lottie_widget, CLOCK_LOTTIE_FPS);
        lvgl_port_unlock();
        ESP_LOGI(TAG, "Clock Lottie resumed");
    } else {
        // First time — full load
        load_clock_lottie(DisplayFsm::get_screen());
        ESP_LOGI(TAG, "Clock Lottie created");
    }
#endif
}

extern "C" void display_fsm_hide_clock_lottie(void)
{
#if LV_USE_LOTTIE
    if (s_clock_lottie_widget) {
        lvgl_port_lock(0);
        lv_lottie_pause(s_clock_lottie_widget);
        lv_obj_add_flag(s_clock_lottie_widget, LV_OBJ_FLAG_HIDDEN);
        lvgl_port_unlock();
    }
    ESP_LOGI(TAG, "Clock Lottie paused");
#endif
}

extern "C" bool display_fsm_load_lottie(const lottie_load_job_t *job)
{
#if LV_USE_LOTTIE
    if (!s_loader_queue || !job) return false;
    BaseType_t ret = xQueueSend(s_loader_queue, job, pdMS_TO_TICKS(500));
    if (ret != pdTRUE) {
        ESP_LOGW(TAG, "Lottie loader queue full, dropping '%s'", job->path);
    }
    return ret == pdTRUE;
#else
    (void)job;
    return false;
#endif
}

extern "C" void display_fsm_refresh_text_color(void)
{
    clock_widget_t *cw = DisplayFsm::get_clock();
    if (!cw) return;

    clock_settings_t cfg;
    if (settings_load(&cfg) != ESP_OK) return;

    lv_color_t color = lv_color_make(
        (cfg.text_color >> 16) & 0xFF,
        (cfg.text_color >>  8) & 0xFF,
         cfg.text_color        & 0xFF);

    lvgl_port_lock(0);
    clock_widget_set_color(cw, color);
    lvgl_port_unlock();
    ESP_LOGI(TAG, "Text color refreshed: 0x%06lX", (unsigned long)cfg.text_color);
}
