/**
 * @file display_fsm_task.cpp
 * v2: forecast icons rescanned off-lock; Lottie loaded while hidden is left paused.
 * @version 2
 * @brief Display FSM task: owns the event queue, background loading/contrast, timers and the Lottie loader, and dispatches to TinyFSM.
 */
// FreeRTOS task that owns the event queue and dispatches to TinyFSM.
// All LVGL operations happen under lvgl_port_lock in this task.

#include "display_fsm.h"
#include "sched_store.h"
#include "display_fsm_base.h"
#include "display_states.h"
#include "display_widgets.h"
#include "display_scheduler.h"
#include "frame_capture.h"
#include "image_decode.h"
#include "ui_contrast.h"
#include "ui_theme.h"
#include "ui_theme_lvgl.h"
#include "ui_theme_bind.h"
#include "theme_store.h"
#include "clock_animation.h"

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
#include "theme_pref.h"
#include "time_sync.h"
#include "sdcard.h"
#include "nws.h"

#include <inttypes.h>
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

/**
 * @brief Read a Lottie JSON file into a fresh SPIRAM buffer (off the LVGL lock).
 *
 * @utility
 * @version 1
 * @return Heap buffer (caller frees with heap_caps_free) with *size_out set, or NULL on open/alloc/read failure.
 */
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

/**
 * @brief Attach already-read Lottie data to the job's widget and retime it to the target FPS.
 *
 * Attach already-read Lottie data and retime to the target FPS. Holds the LVGL
 * lock across BOTH parse and finalize: the widget may be destroyed (e.g. user
 * swipes away from the weather screen) while we read the file off-lock. Widget
 * destruction runs under the LVGL lock on the FSM task, so taking the lock here
 * and validating the widget BEFORE touching it makes the parse+finalize atomic
 * w.r.t. deletion — closing the use-after-free where the loader called
 * tvg_animation_get_total_frame on a freed widget (this=0x1f8 crash).
 * ThorVG copies the buffer (copy=true), so the caller frees it afterwards.
 *
 * v2: a widget hidden while its file was loading (clock minimised, animation switched) is left
 *     paused; its owner resumes it on show, so it never renders hidden.
 * v3: target_fps caps the RENDER rate (lv_lottie_set_render_fps) at real-time playback.
 *     Stretching the duration by 60/target_fps played every Lottie at 1/3 speed (a 20 fps
 *     target turned the 8 s fog loop into 24 s, which looked static).
 * @utility
 * @version 3
 */
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
        lv_lottie_set_render_fps(job->widget, job->target_fps);   // real-time speed, capped renders
    }
    if (lv_obj_has_flag(job->widget, LV_OBJ_FLAG_HIDDEN)) {
        lv_lottie_pause(job->widget);  // hidden meanwhile: don't render off-screen until resumed
        ESP_LOGI(TAG, "lottie_load: '%s' loaded while hidden — left paused", job->path);
    }
    lvgl_port_unlock();
}

/**
 * @brief Persistent Lottie loader task: dequeue jobs, read the file off-lock, then parse/apply under the LVGL lock.
 *
 * @utility
 * @version 1
 */
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

/**
 * @brief Create the Lottie loader job queue and spawn the SPIRAM-stack loader task (idempotent).
 *
 * @utility
 * @version 1
 */
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

/**
 * @brief Map a C display_event_t onto the matching TinyFSM event and dispatch it to DisplayFsm.
 * v2: DISPLAY_EVT_RADAR_LOOP_LOAD is handled before dispatch (no FSM event).
 *
 * @utility
 * v3: themes replace the text colour (theme names, theme roles, theme events).
 * @version 3
 */
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
        case DISPLAY_EVT_REFRESH_THEME:
        case DISPLAY_EVT_PHOTO_ADVANCE:
        case DISPLAY_EVT_RADAR_LOOP_LOAD:
            // Handled in process_event before dispatch; nothing to do here.
            break;
    }
}

// ---------------------------------------------------------------------------
// Background image loading (moved from ui.c)
// ---------------------------------------------------------------------------

/**
 * @brief Check whether a path has a .gif extension (case-insensitive).
 *
 * @utility
 * @version 1
 * @return true if the path ends in .gif, false otherwise.
 */
static bool is_gif_path(const char *path)
{
    const char *ext = strrchr(path, '.');
    return ext && strcasecmp(ext, ".gif") == 0;
}

/**
 * @brief Translate an LVGL drive path ("A:/..", "B:/..") to a POSIX mount path.
 *
 * @utility
 * @version 1
 */
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

/**
 * @brief Predecode a non-GIF background PNG once into a persistent, top-down RGB565 SPIRAM descriptor.
 *
 * Predecode a non-GIF background PNG ONCE (outside the LVGL lock — SD I/O + a
 * ~2.5MB alloc) into a persistent SPIRAM descriptor. Handing LVGL a static
 * descriptor stops it re-decoding the full-screen bitmap from disk whenever its
 * image cache evicts it under memory pressure — the root cause of clock-screen
 * lag and the occasional blank background. NULL → caller uses on-demand decode.
 * MUST run in a deep-stack task (fsm_task): stb_image inflate needs ~8KB stack.
 * v2: no vertical flip. Decode is pinned top-down; the old flip only undid stb's
 *     accidental bottom-up decode on the IDF beta, and on v5.5.6 it inverted the bg.
 * v3: XRGB8888 so background redraws blit on the PPA.
 * v4: back to RGB565: the PPA ARGB path garbled partial redraws of the 1025 px wide
 *     (padded-stride) background on device.
 *
 * @utility
 * @version 4
 * @return Decoded descriptor, or NULL on failure (caller then skips the background).
 */
static lv_image_dsc_t *predecode_bg(const char *path)
{
    char fs_path[160];
    bg_lvgl_to_fs_path(path, fs_path, sizeof(fs_path));
    // RGB565 (opaque): matches the framebuffer so the full-screen background redraws as a
    // straight CPU copy. XRGB8888 (to blit on the PPA) garbled partial redraws on device:
    // the 1025 px wide image has padded rows, which the PPA ARGB path mis-strides.
    lv_image_dsc_t *dsc = image_decode_png_file_rgb565(fs_path);
    if (!dsc) {
        ESP_LOGW(TAG, "bg: predecode failed for '%s' (%s) — on-demand fallback",
                 path, fs_path);
    }
    return dsc;
}

/**
 * @brief Create a looping, screen-sized GIF background object.
 *
 * Create a looping, screen-sized GIF background. Caller holds the LVGL lock;
 * this briefly drops it for the GIF decoder init, then re-locks.
 *
 * @utility
 * @version 1
 * @return The new GIF object.
 */
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

/**
 * @brief Create a static image background from a predecoded descriptor, adopting it as the persistent s_bg_dsc.
 *
 * Create a static image background from a predecoded descriptor, or fall back
 * to LVGL's on-demand decode of the path.
 *
 * @utility
 * @version 1
 * @return The new image object, or NULL if no descriptor was decoded.
 */
static lv_obj_t *create_static_bg(lv_obj_t *scr, const char *path, lv_image_dsc_t *dsc)
{
    if (!dsc) {
        // Pre-decode failed (SD read error). Do NOT fall back to lv_img_set_src(path):
        // that makes LVGL decode the PNG straight from SD on the LVGL TASK (under the
        // lock), blocking the UI thread on a slow/failing read → display watchdog →
        // reset loop. Skip the bg instead (black screen + legible clock handle it);
        // it loads normally on a boot where the pre-decode succeeds.
        ESP_LOGW(TAG, "bg: predecode failed for '%s' — skipping (no on-UI-thread SD decode)", path);
        return NULL;
    }
    lv_obj_t *obj = lv_img_create(scr);
    if (s_bg_dsc) image_decode_free(s_bg_dsc);
    s_bg_dsc = dsc;
    lv_img_set_src(obj, dsc);
    return obj;
}

/**
 * @brief Center the background object, send it to the back, and make it non-scrollable and layout-ignored.
 *
 * @utility
 * @version 1
 */
static void apply_bg_common_flags(lv_obj_t *obj)
{
    lv_obj_align(obj, LV_ALIGN_CENTER, 0, 0);
    lv_obj_move_background(obj);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(obj, LV_OBJ_FLAG_IGNORE_LAYOUT);
}

/**
 * @brief Verify the background loaded; delete + null it out on failure. Publishes s_bg_img.
 *
 * @utility
 * @version 1
 */
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

/**
 * @brief Load the configured background (GIF or predecoded PNG) onto the screen, degrading gracefully on failure.
 *
 * @utility
 * @version 1
 */
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
    if (obj) {
        apply_bg_common_flags(obj);
        finalize_bg(obj, gif, path);
    } else {
        s_bg_img = NULL;   // no background this time — degrade gracefully
    }
    lvgl_port_unlock();
}

/**
 * @brief @p user made legible at the theme's text target (exact when it already reaches it)
 *        over the mean of @p img in screen area @p a (images are top-down and screen-sized),
 *        or over the screen's own colour when there is no image (GIF background, failed
 *        decode).
 * @utility
 * v3: the theme's contrast target (was AA).
 * v2: colours from the theme roles (ui_theme), no literals.
 * v4: the screen colour is the sampling fallback.
 * v5: the theme's ink via ui_theme_ink_on_picture (shared with the host preview).
 * @version 5
 * @return The colour to draw the text in.
 */
static lv_color_t legible_over(const lv_image_dsc_t *img, const lv_area_t *a)
{
    lv_obj_t *scr = DisplayFsm::get_screen();
    return ui_theme_ink_on_picture(img, a, scr ? lv_obj_get_style_bg_color(scr, LV_PART_MAIN)
                                               : ui_theme_color(UI_ROLE_SCREEN));
}

/**
 * @brief Show or hide the clock background image (no-op without one). A screen whose own
 *        content covers the whole display hides it: LVGL still draws covered objects (on the
 *        radar screen, ~18% of every redraw). Caller holds the LVGL lock.
 * v2: theme bindings sample the picture only while it shows.
 * @version 2
 * @req REQ-DISP-004
 */
extern "C" void display_fsm_set_bg_visible(bool visible)
{
    if (!s_bg_img) return;
    if (visible) lv_obj_remove_flag(s_bg_img, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(s_bg_img, LV_OBJ_FLAG_HIDDEN);
    ui_bind_set_picture(visible ? s_bg_dsc : NULL);   // text backdrops follow what shows
}

/**
 * @brief The minimized clock's colour over @p behind (a screen's own content under the
 *        minimized clock: radar map, photo), or over the background picture when NULL.
 *        Caller holds the LVGL lock.
 * v2: colours from the theme roles (ui_theme), no literals.
 * v3: the theme's ink (legible_over no longer takes a colour).
 * @version 3
 * @req REQ-BG-002
 * @return The user's colour, minimally adjusted for contrast.
 */
extern "C" lv_color_t display_fsm_min_clock_color(const lv_image_dsc_t *behind)
{
    lv_area_t ma;
    clock_widget_min_area(&ma);
    return legible_over(behind ? behind : s_bg_dsc, &ma);
}

/**
 * @brief Apply the user's text colour to the clock: exact where it is legible, its OKLCH
 *        lightness minimally shifted where not, judged against what is behind each layout
 *        (the full clock's and the minimized clock's area of the background picture). The
 *        label shadows add to that.
 * v2: minimal OKLCH adjustment against the background (was the exact colour everywhere,
 *     after the old extreme push had turned a chosen white black).
 * v3: the ink is the current theme's (was the settings / profile text colour).
 *
 * @utility
 * v4: the top-bar colour, judged against the strip behind the bar.
 * @version 4
 * @req REQ-BG-002
 */
static void apply_clock_user_color(void)
{
    clock_widget_t *clk = DisplayFsm::get_clock();
    if (!clk) return;

    lv_color_t user = ui_oklch_to_color(ui_theme_get()->ink);
    lvgl_port_lock(0);
    clock_widget_set_user_color(clk, user);
    lv_area_t fa;
    clock_widget_full_area(&fa);
    clock_widget_set_color(clk, legible_over(s_bg_dsc, &fa));
    lv_area_t ba;
    clock_widget_bar_area(&ba);
    clock_widget_set_bar_color(clk, legible_over(s_bg_dsc, &ba));
    clock_widget_set_minimized_base_color(clk, display_fsm_min_clock_color(NULL));
    lvgl_port_unlock();
}

/**
 * @brief Replace the current background with @p cfg's without a blank frame: the new image
 *        is decoded and placed behind the old one, then the old one is deleted. If the new
 *        one fails to load, the old one stays.
 *
 * MUST run in fsm_task — load_background() decodes with a deep stack outside the LVGL lock.
 *
 * @utility
 * v2: clock text uses the user's exact colour (legibility via label shadows), no bg sampling.
 * v3: takes the (effective) settings to apply instead of loading them.
 * v4: swaps after decoding the new image (it deleted the old one first: ~1.3 s of black
 *     screen at every background change, seen on device with the schedule).
 * v5: themes replace the text colour (theme names, theme roles, theme events).
 * v6: bound text is re-judged against the new picture.
 * @version 6
 */
static void reload_background(const clock_settings_t *cfg)
{
    lv_obj_t *scr = DisplayFsm::get_screen();
    if (!scr) {
        ESP_LOGW(TAG, "reload_bg: screen not initialized");
        return;
    }
    // Take ownership of the current background so load_background() neither frees its
    // descriptor nor reports it as the result.
    lv_obj_t *old_img = s_bg_img;
    lv_image_dsc_t *old_dsc = s_bg_dsc;
    s_bg_img = NULL;
    s_bg_dsc = NULL;

    load_background(scr, cfg);   // new image behind the old one (move_background)

    if (!s_bg_img) {   // failed or nothing configured: keep what was showing
        s_bg_img = old_img;
        s_bg_dsc = old_dsc;
        ESP_LOGW(TAG, "reload_bg: new background not loaded, keeping the current one");
    } else {
        lvgl_port_lock(0);
        if (old_img) {
            lv_anim_delete(old_img, NULL);
            lv_obj_delete(old_img);
        }
        ui_bind_set_picture(s_bg_dsc);   // before the old one is freed: bound text re-judged
        lvgl_port_unlock();
        if (old_dsc) image_decode_free(old_dsc);   // its object is gone
    }
    apply_clock_user_color();
}

static int          s_bg_profile = -2;   // schedule profile last applied (-1: none, -2: not yet)
static clock_anim_t s_bg_anim = CLOCK_ANIM_COUNT;   // animation last applied by a profile

/**
 * @brief Effective settings: the global settings with the showing schedule profile's
 *        overrides (image, brightness) applied; its theme is applied by apply_theme.
 * v2: no text colour (the theme carries it).
 * v3: the profile from the daily/events schedule (sched_store), not the weekly grid.
 * @version 3
 * @req REQ-BG-004
 * @return The showing profile's index (-1: none, the global settings); it is copied to
 *         @p prof when non-NULL.
 */
static int effective_settings(clock_settings_t *cfg, sched_rule_t *prof)
{
    settings_load(cfg);
    time_t now = time(NULL);
    struct tm ti;
    localtime_r(&now, &ti);
    sched_rule_t p;
    int idx = sched_store_active(&ti, &p);
    if (p.image[0]) strlcpy(cfg->background_image, p.image, sizeof(cfg->background_image));
    if (p.brightness != SCHED_INHERIT) cfg->brightness = (uint8_t)p.brightness;
    if (prof) *prof = p;
    return idx;
}

/**
 * @brief Select the profile's animation (or the stored preference) if it changed.
 * v2: the profile names its animation (unknown or empty: the preference).
 * @version 2
 * @utility
 */
static void apply_profile_anim(const sched_rule_t *p)
{
    clock_anim_t named = p->anim[0] ? clock_anim_from_name(p->anim) : CLOCK_ANIM_COUNT;
    clock_anim_t anim = named != CLOCK_ANIM_COUNT ? named : clock_anim_pref_get();
    if (anim == s_bg_anim) return;
    s_bg_anim = anim;
    clock_animation_select(anim);
}

static char s_theme_stem[BG_THEME_LEN] = "";   // theme file last installed ("" = none yet)

/**
 * @brief Install the effective theme (the profile's when it names one, else the global
 *        preference) unless it is already installed (always when @p force). Loads the file
 *        off the LVGL lock, falling back to the built-in theme. fsm_task only.
 * v2: repaints every theme binding.
 * v3: takes a schedule profile (sched_rule_t).
 * @version 3
 * @req REQ-UI-003
 * @req REQ-BG-004
 */
static void apply_theme(const sched_rule_t *p, bool force)
{
    char stem[BG_THEME_LEN];
    if (p && p->theme[0]) strlcpy(stem, p->theme, sizeof(stem));
    else theme_pref_get(stem);
    if (!force && strcmp(stem, s_theme_stem) == 0) return;

    ui_theme_t t;
    bool ok = theme_store_load(stem, &t);
    lvgl_port_lock(0);
    ui_theme_set(ok ? &t : NULL);
    ui_theme_lvgl_refresh();
    ui_bind_refresh_all();   // every bound fill, border and text, live
    lvgl_port_unlock();
    strlcpy(s_theme_stem, stem, sizeof(s_theme_stem));
    ESP_LOGI(TAG, "theme '%s' -> %s%s", stem, ui_theme_get()->name,
             ok ? "" : " (file missing or invalid: built-in theme)");
}

/**
 * @brief Apply the active schedule profile when it changed (or always when @p force):
 *        theme, background, brightness and clock animation. fsm_task only (decodes).
 * v2: the profile's theme (was its text colour).
 * v3: profiles from the daily/events schedule.
 * @version 3
 * @req REQ-BG-004
 */
static void apply_bg_profile(bool force)
{
    clock_settings_t cfg;
    sched_rule_t p;
    int idx = effective_settings(&cfg, &p);
    if (!force && idx == s_bg_profile) return;
    ESP_LOGI(TAG, "bg profile %d -> %d '%s' (image '%s', brightness %u%%, theme '%s')",
             s_bg_profile, idx, idx >= 0 ? p.name : "(global)", cfg.background_image, cfg.brightness,
             p.theme[0] ? p.theme : "global");
    s_bg_profile = idx;
    apply_theme(&p, force);
    reload_background(&cfg);
    display_fsm_refresh_brightness(cfg.brightness);
    apply_profile_anim(&p);
}

// ---------------------------------------------------------------------------
// Gesture callback — registered on the active screen
// ---------------------------------------------------------------------------

// Debounce: LVGL fires LV_EVENT_GESTURE multiple times during a single
// finger drag.  Without a cooldown, one physical swipe triggers 2+ state
// transitions (e.g. radar → astronomy → photos instead of radar → astronomy).
#define GESTURE_DEBOUNCE_MS 500
static uint32_t s_last_gesture_tick = 0;

/**
 * @brief Debounced LV_EVENT_GESTURE handler that enqueues a DISPLAY_EVT_GESTURE for top/left/right swipes.
 *
 * @callback
 * @version 1
 */
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

/**
 * @brief Burn-in mitigation timer: random-walk the clock's pixel offset by one step per axis, clamped to +/-4 px.
 *
 * @callback
 * @version 1
 */
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

/**
 * @brief 1 s timer: enqueue a DISPLAY_EVT_CLOCK_UPDATE only when the local minute has changed.
 *
 * @callback
 * @version 1
 */
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

/**
 * @brief Clean the active screen and build the persistent UI (clock, alert banner, dot indicator, gesture handler) on lv_layer_top().
 *
 * Clean the active screen and build the persistent UI (clock, alert banner, dot
 * indicator, gesture handler) on lv_layer_top(). Returns the screen object.
 *
 * @utility
 * v2: colours from the theme roles (ui_theme), no literals.
 * v3: no settings parameter (the clock takes the theme's colour).
 * v4: screen fill bound to SCREEN.
 * @version 4
 * @return The active screen object.
 */
static lv_obj_t *setup_screen_widgets(void)
{
    lvgl_port_lock(0);

    ui_theme_lvgl_init(lv_display_get_default());   // stock widgets draw with the theme
    lv_obj_t *scr = lv_scr_act();
    DisplayFsm::set_screen(scr);

    lv_obj_clean(scr);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    ui_bind_fill_role(scr, UI_ROLE_SCREEN);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);

    // Persistent UI lives on lv_layer_top() so it always renders above the
    // state-specific widgets on the screen.
    lv_obj_t *top = lv_layer_top();
    lv_obj_clear_flag(top, LV_OBJ_FLAG_CLICKABLE);  // pass-through clicks

    clock_widget_t *cw = clock_widget_create(top);   // in the theme's text colour
    if (cw) {
        DisplayFsm::set_clock(cw);
        ESP_LOGI(TAG, "ClockWidget created on layer_top");
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

/**
 * @brief Start the clock minute-tick poll (1s) and burn-in pixel jitter (10 min) timers.
 *
 * Clock minute-tick poll (1s) + burn-in pixel jitter (10 min) timers.
 *
 * @utility
 * @version 1
 */
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

/**
 * @brief Handle one dequeued event: off-lock radar/background/photo work, otherwise FSM dispatch under the LVGL lock.
 *
 * Handle one dequeued event. Radar pre-decode and background reload run OUTSIDE
 * the LVGL lock (slow stb_image work); everything else dispatches to the FSM.
 * The [XFER] log reports how long the FSM held the LVGL lock during a transition
 * — i.e. how long the display could not render. Useful for catching regressions
 * where a heavy entry() builds too much under the lock.
 *
 * v2: a forecast update rescans the forecast icons off-lock before dispatch.
 * @utility
 * v3: clock text uses the user's exact colour (legibility via label shadows), no bg sampling.
 * v4: radar-loop frames decode off-lock, one per event (RadarOverlay::loop_step).
 * v5: background schedule: the minute tick and REFRESH_BG apply the active profile.
 * v6: passes the radar image's valid time to the predecode.
 * v7: themes replace the text colour (theme names, theme roles, theme events).
 * v8: schedule profiles from the daily/events schedule.
 * @version 8
 */
static void process_event(const display_event_t *evt)
{
    if (evt->type == DISPLAY_EVT_RADAR_LOOP_LOAD) {
        RadarOverlay::loop_step();
        return;   // not a state event — no FSM dispatch
    }
    if (evt->type == DISPLAY_EVT_RADAR_READY) {
        size_t rlen = 0;
        const uint8_t *rpng = nws_get_radar_png(&rlen);
        if (rpng && rlen > 0) radar_view_predecode(rpng, rlen, nws_get_radar_ts());
    }
    if (evt->type == DISPLAY_EVT_FORECAST_UPDATE) {
        forecast_icons_rescan();   // SD stat()s off-lock; the strip update below only reads the cache
    }
    if (evt->type == DISPLAY_EVT_REFRESH_BG) {
        apply_bg_profile(true);   // settings or schedule changed: re-apply the effective profile
        return;   // not a state event — no FSM dispatch
    }
    if (evt->type == DISPLAY_EVT_REFRESH_THEME) {
        clock_settings_t cfg;
        sched_rule_t prof;
        effective_settings(&cfg, &prof);
        theme_store_rescan();
        apply_theme(&prof, true);
        apply_clock_user_color();
        return;   // not a state event — no FSM dispatch
    }
    if (evt->type == DISPLAY_EVT_CLOCK_UPDATE) {
        apply_bg_profile(false);  // minute tick: switch profile at a slot boundary
    }
    if (evt->type == DISPLAY_EVT_PHOTO_ADVANCE) {
        // Decode the next photo OFF the LVGL lock (slow PNG decode), then swap
        // it in under the lock (fast). No-op if the photo state has exited.
        PhotoSlideshow::decode_next();    // off-lock (instant if prefetched)
        lvgl_port_lock(0);
        PhotoSlideshow::present();          // under lock: fast swap
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

/**
 * @brief Block forever on the FSM event queue, processing each received event.
 *
 * @utility
 * @version 1
 */
static void fsm_run_event_loop(void)
{
    display_event_t evt;
    while (true) {
        if (xQueueReceive(s_event_queue, &evt, portMAX_DELAY) == pdTRUE) {
            process_event(&evt);
        }
    }
}

/**
 * @brief FSM task entry: build the screen, background, animation and scheduler, start the FSM and timers, then run the event loop.
 *
 * v2: resolves the forecast icons off-lock before the first weather screen can be built.
 * @utility
 * v3: clock text uses the user's exact colour (legibility via label shadows), no bg sampling.
 * v4: starts from the effective settings (active background schedule profile applied).
 * v5: hooks the debug frame recorder to the display.
 * v6: installs the effective theme (and lists the installed ones) before building widgets.
 * v7: bound text samples the background picture.
 * v8: loads the daily/events schedule first.
 * @version 8
 */
static void fsm_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "FSM task started");

    clock_settings_t cfg;
    sched_rule_t prof;
    sched_store_init();            // SD read (or converting the weekly schedule) off-lock
    s_bg_profile = effective_settings(&cfg, &prof);   // global settings + active schedule profile
    theme_store_rescan();          // SD listing off-lock; the theme menus read the cache
    apply_theme(&prof, true);      // before any widget is built, so all draw with it

    lv_obj_t *scr = setup_screen_widgets();

    // Background + Lottie manage their own LVGL lock.
    load_background(scr, &cfg);
    lvgl_port_lock(0);
    ui_bind_set_picture(s_bg_dsc);   // what bound text sits on
    lvgl_port_unlock();
    apply_clock_user_color();
    clock_animation_init(scr);   // user-selected clock animation (hummingbird / snowfall / none)
    s_bg_anim = clock_anim_pref_get();
    apply_profile_anim(&prof);
    if (prof.brightness != BG_INHERIT) display_fsm_refresh_brightness(cfg.brightness);

    // Preload the radar basemap off-lock so the first radar transition doesn't
    // stall on a ~2.4 MB SD read while holding the LVGL lock.
    radar_view_preload_map();
    forecast_icons_rescan();   // SD stat()s off-lock; the forecast strip only reads the cache

    display_scheduler_get()->init();

    lvgl_port_lock(0);
    frame_capture_attach();
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

/**
 * @brief Create the FSM event queue, the Lottie loader (if enabled), and the FSM task (idempotent).
 *
 * @version 1
 * @req REQ-DISP-001
 */
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

/**
 * @brief Enqueue an event for the FSM task, waiting up to 100 ms for queue space.
 *
 * @version 1
 * @req REQ-DISP-001
 * @return true if the event was queued, false if uninitialized, evt is NULL, or the queue stayed full.
 */
extern "C" bool display_fsm_send_event(const display_event_t *evt)
{
    if (!s_event_queue || !evt) return false;
    BaseType_t ret = xQueueSend(s_event_queue, evt, pdMS_TO_TICKS(100));
    if (ret != pdTRUE) {
        ESP_LOGW(TAG, "Event queue full, dropping event type=%d", evt->type);
    }
    return ret == pdTRUE;
}

/**
 * @brief Get the name of the current FSM state.
 *
 * @version 1
 * @req REQ-DISP-001
 * @return Current state name string.
 */
extern "C" const char *display_fsm_get_state_name(void)
{
    return DisplayFsm::get_state_name();
}

/**
 * @brief Get the id of the current FSM state.
 *
 * @version 1
 * @req REQ-DISP-001
 * @return Current display_state_id_t.
 */
extern "C" display_state_id_t display_fsm_get_state_id(void)
{
    return DisplayFsm::get_state_id();
}

/**
 * @brief Get the persistent clock widget.
 *
 * @version 1
 * @req REQ-DISP-001
 * @return Clock widget, or NULL if not yet created.
 */
extern "C" clock_widget_t *display_fsm_get_clock(void)
{
    return DisplayFsm::get_clock();
}

/**
 * @brief Get the FSM's active screen object.
 *
 * @version 1
 * @req REQ-DISP-001
 * @return Screen object, or NULL if not yet initialized.
 */
extern "C" lv_obj_t *display_fsm_get_screen(void)
{
    return DisplayFsm::get_screen();
}

/**
 * @brief Request a background reload by enqueueing DISPLAY_EVT_REFRESH_BG for fsm_task.
 *
 * @version 1
 * @req REQ-BG-001
 */
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

/**
 * @brief Show the user-selected clock animation (FSM ClockFull / restore_clock).
 * @version 2
 * @req REQ-ANIM-001
 */
extern "C" void display_fsm_show_clock_lottie(void)
{
    clock_animation_show();
}

/**
 * @brief Hide the clock animation whenever the clock is minimised or covered.
 * @version 2
 * @req REQ-ANIM-001
 */
extern "C" void display_fsm_hide_clock_lottie(void)
{
    clock_animation_hide();
}

/**
 * @brief Queue a Lottie load job for the shared loader task (no-op when Lottie is disabled).
 *
 * @version 1
 * @req REQ-LOTTIE-001
 * @return true if the job was queued, false if unavailable, job is NULL, or the queue stayed full.
 */
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

/**
 * @brief Apply backlight brightness live; no-op until the display (and its backlight PWM) is up.
 * @version 1
 * @req REQ-API-002
 * @req REQ-UI-001
 */
extern "C" void display_fsm_refresh_brightness(uint8_t percent)
{
    if (!DisplayFsm::get_clock()) {   // display not initialised yet (launch window)
        ESP_LOGI(TAG, "brightness %u%% saved; applies at display init", percent);
        return;
    }
    esp_err_t err = bsp_display_brightness_set(percent);
    if (err != ESP_OK) ESP_LOGW(TAG, "brightness set %u%% failed: %s", percent, esp_err_to_name(err));
}

/**
 * @brief Request a theme re-apply (the global preference or the active profile's changed, or
 *        a theme file was added) by enqueueing DISPLAY_EVT_REFRESH_THEME for fsm_task, which
 *        reads the files off the LVGL lock. Callable from LVGL callbacks and HTTP handlers.
 * @version 1
 * @req REQ-UI-003
 */
extern "C" void display_fsm_refresh_theme(void)
{
    display_event_t evt;
    memset(&evt, 0, sizeof(evt));
    evt.type = DISPLAY_EVT_REFRESH_THEME;
    if (!display_fsm_send_event(&evt)) {
        ESP_LOGW(TAG, "refresh_theme: failed to enqueue");
    }
}
