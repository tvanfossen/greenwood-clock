// components/display_fsm/src/display_fsm_task.cpp
//
// FreeRTOS task that owns the event queue and dispatches to TinyFSM.
// All LVGL operations happen under lvgl_port_lock in this task.

#include "display_fsm.h"
#include "display_fsm_base.h"
#include "display_states.h"
#include "display_widgets.h"
#include "display_scheduler.h"

#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "freertos/idf_additions.h"
#include "settings.h"
#include "time_sync.h"
#include "sdcard.h"

#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <errno.h>

static const char *TAG = "fsm_task";

#define FSM_QUEUE_DEPTH   16
#define FSM_TASK_STACK     (8 * 1024)
#define FSM_TASK_PRIORITY  5

static QueueHandle_t s_event_queue = NULL;

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

static lv_obj_t *s_bg_img = NULL;

static void load_background(lv_obj_t *scr, const clock_settings_t *cfg)
{
    if (!sdcard_is_mounted() || !cfg || !cfg->background_image[0]) {
        ESP_LOGW(TAG, "bg: SD not mounted or no background configured");
        return;
    }
    const char *path = cfg->background_image;
    bool gif = is_gif_path(path);
    ESP_LOGI(TAG, "bg: loading '%s' (gif=%d)", path, (int)gif);

    lvgl_port_lock(0);
    lv_obj_t *obj;
    if (gif) {
        obj = lv_gif_create(scr);
        lv_gif_set_src(obj, path);
    } else {
        obj = lv_img_create(scr);
        lv_img_set_src(obj, path);
    }
    lv_obj_align(obj, LV_ALIGN_CENTER, 0, 0);
    lv_obj_move_background(obj);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(obj, LV_OBJ_FLAG_IGNORE_LAYOUT);

    if (gif) {
        // Brief unlock for GIF decoder init, then optimize
        lvgl_port_unlock();
        vTaskDelay(pdMS_TO_TICKS(50));
        lvgl_port_lock(0);
        lv_obj_set_width(obj, lv_obj_get_width(lv_scr_act()));
        lv_obj_set_height(obj, lv_obj_get_height(lv_scr_act()));
        lv_gif_set_loop_count(obj, -1);
        lv_gif_restart(obj);
    }

    bool loaded = gif ? lv_gif_is_loaded(obj) : (lv_img_get_src(obj) != NULL);
    if (!loaded) {
        ESP_LOGW(TAG, "bg: failed to load '%s'", path);
        lv_obj_del(obj);
        obj = NULL;
    } else {
        ESP_LOGI(TAG, "bg: loaded '%s'", path);
    }
    lvgl_port_unlock();
    s_bg_img = obj;
}

// ---------------------------------------------------------------------------
// Lottie hummingbird loading (moved from ui.c)
// ---------------------------------------------------------------------------

#if LV_USE_LOTTIE

#define CLOCK_LOTTIE_W          250
#define CLOCK_LOTTIE_H          250
#define CLOCK_LOTTIE_FPS        20
#define CLOCK_LOTTIE_PATH       "/sdcard/hummingbird.json"
#define CLOCK_LOTTIE_LOAD_STACK (64 * 1024)

static lv_obj_t *s_clock_lottie_widget = NULL;
static void     *s_clock_lottie_buf    = NULL;

typedef struct {
    lv_obj_t *widget;
    char      path[256];
    uint32_t  fps;
} lottie_load_arg_t;

static void lottie_load_task(void *arg)
{
    lottie_load_arg_t *a = (lottie_load_arg_t *)arg;
    lv_obj_t *widget = a->widget;
    uint32_t  fps    = a->fps;
    char path[256];
    strlcpy(path, a->path, sizeof(path));
    free(a);

    ESP_LOGI(TAG, "lottie_load: start path='%s'", path);

    struct stat st;
    if (stat(path, &st) != 0) {
        ESP_LOGE(TAG, "lottie_load: file not found '%s' errno=%d", path, errno);
        vTaskDeleteWithCaps(NULL);
        return;
    }
    ESP_LOGI(TAG, "lottie_load: file found, size=%lld B", (long long)st.st_size);

    lvgl_port_lock(0);
    lv_lottie_set_src_file(widget, path);
    // Rescale from default 60fps to target fps
    lv_anim_t *anim = lv_lottie_get_anim(widget);
    if (anim && fps > 0) {
        uint32_t dur = lv_anim_get_time(anim);
        lv_anim_set_duration(anim, dur * 60 / fps);
        anim->act_time = 0;
        ESP_LOGI(TAG, "lottie_load: fps=%lu dur=%lu→%lu ms",
                 (unsigned long)fps, (unsigned long)dur, (unsigned long)(dur * 60 / fps));
    }
    lvgl_port_unlock();
    vTaskDeleteWithCaps(NULL);
}

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
        free(s_clock_lottie_buf);
        s_clock_lottie_buf = NULL;
        return;
    }
    if (lv_lottie_render_failed(widget)) {
        ESP_LOGE(TAG, "lottie: render task failed");
        lv_obj_delete(widget);
        lvgl_port_unlock();
        free(s_clock_lottie_buf);
        s_clock_lottie_buf = NULL;
        return;
    }
    lv_obj_set_size(widget, CLOCK_LOTTIE_W, CLOCK_LOTTIE_H);
    lv_lottie_set_buffer(widget, CLOCK_LOTTIE_W, CLOCK_LOTTIE_H, s_clock_lottie_buf);
    lv_obj_align(widget, LV_ALIGN_BOTTOM_LEFT, 16, -16);
    s_clock_lottie_widget = widget;
    lvgl_port_unlock();

    // Spawn load task on DRAM stack (deep recursive JSON parse)
    lottie_load_arg_t *a = (lottie_load_arg_t *)malloc(sizeof(*a));
    if (!a) { ESP_LOGE(TAG, "lottie: alloc arg failed"); return; }
    a->widget = widget;
    a->fps    = CLOCK_LOTTIE_FPS;
    strlcpy(a->path, CLOCK_LOTTIE_PATH, sizeof(a->path));
    BaseType_t ret = xTaskCreateWithCaps(lottie_load_task, "clk_lottie",
                                          CLOCK_LOTTIE_LOAD_STACK, a, 5, NULL,
                                          MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "lottie: task spawn failed");
        free(a);
    }
}

#endif /* LV_USE_LOTTIE */

// ---------------------------------------------------------------------------
// Gesture callback — registered on the active screen
// ---------------------------------------------------------------------------

static void screen_gesture_cb(lv_event_t *e)
{
    (void)e;
    lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());
    if (dir == LV_DIR_TOP) {
        display_event_t evt = {};
        evt.type = DISPLAY_EVT_GESTURE;
        evt.gesture.dir = LV_DIR_TOP;
        display_fsm_send_event(&evt);
    }
}

// ---------------------------------------------------------------------------
// Clock update timer — fires every second, sends DISPLAY_EVT_CLOCK_UPDATE
// ---------------------------------------------------------------------------

static void clock_tick_cb(TimerHandle_t timer)
{
    (void)timer;
    time_t now;
    struct tm ti;
    time_sync_get_local(&now, &ti);

    display_event_t evt = {};
    evt.type = DISPLAY_EVT_CLOCK_UPDATE;
    evt.clock.tm_hour = ti.tm_hour;
    evt.clock.tm_min  = ti.tm_min;
    evt.clock.tm_sec  = ti.tm_sec;
    evt.clock.tm_mday = ti.tm_mday;
    evt.clock.tm_mon  = ti.tm_mon;
    evt.clock.tm_year = ti.tm_year;
    evt.clock.tm_wday = ti.tm_wday;
    display_fsm_send_event(&evt);
}

// ---------------------------------------------------------------------------
// FSM task — single consumer of event queue
// ---------------------------------------------------------------------------

static void fsm_task(void *arg)
{
    (void)arg;
    ESP_LOGI(TAG, "FSM task started");

    // Load settings for background + text color
    clock_settings_t cfg;
    settings_load(&cfg);

    // --- Set up the screen: background, ClockWidget, gestures ---
    lvgl_port_lock(0);

    lv_obj_t *scr = lv_scr_act();
    DisplayFsm::set_screen(scr);

    // Clean screen and set base style (replaces ui_clock_setup_screen)
    lv_obj_clean(scr);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(scr, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, LV_PART_MAIN);

    // Create ClockWidget — FSM now owns the clock display
    clock_widget_t *cw = clock_widget_create(scr);
    if (cw) {
        lv_color_t color = lv_color_make(
            (cfg.text_color >> 16) & 0xFF,
            (cfg.text_color >>  8) & 0xFF,
             cfg.text_color        & 0xFF);
        clock_widget_set_color(cw, color);
        DisplayFsm::set_clock(cw);
        ESP_LOGI(TAG, "ClockWidget created, color=0x%06lX", (unsigned long)cfg.text_color);
    } else {
        ESP_LOGE(TAG, "Failed to create ClockWidget");
    }

    // Create AlertBanner — composited on top of all states
    alert_banner_t *ab = alert_banner_create(scr);
    if (ab) {
        DisplayFsm::set_alert_banner(ab);
        ESP_LOGI(TAG, "AlertBanner created");
    } else {
        ESP_LOGW(TAG, "Failed to create AlertBanner");
    }

    // Register gesture callback on the screen
    lv_obj_add_event_cb(scr, screen_gesture_cb, LV_EVENT_GESTURE, NULL);

    lvgl_port_unlock();

    // Load background image from settings (manages its own LVGL lock)
    load_background(scr, &cfg);

    // Load Lottie hummingbird animation (manages its own LVGL lock)
#if LV_USE_LOTTIE
    load_clock_lottie(scr);
#endif

    // Initialize scheduler
    display_scheduler_get()->init();

    // Start TinyFSM — enters ClockFull::entry()
    DisplayFsm::start();

    // Start clock update timer (fires every 1 second)
    TimerHandle_t clock_timer = xTimerCreate("clk_tick", pdMS_TO_TICKS(1000),
                                              pdTRUE, NULL, clock_tick_cb);
    if (clock_timer) {
        xTimerStart(clock_timer, 0);
        ESP_LOGI(TAG, "Clock tick timer started (1s)");
    }

    // Send initial clock update immediately
    clock_tick_cb(NULL);

    ESP_LOGI(TAG, "FSM initialized, entering event loop");

    // Main event loop
    display_event_t evt;
    while (true) {
        if (xQueueReceive(s_event_queue, &evt, portMAX_DELAY) == pdTRUE) {
            lvgl_port_lock(0);
            dispatch_event(&evt);
            lvgl_port_unlock();
        }
    }
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
