// components/ui/ui.c

#include <math.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include "esp_log.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "lvgl.h"
#include "esp_lvgl_port.h"
#include "bsp/display.h"
#include "time_sync.h"
#include "screen_manager.h"
#include "sdcard.h"
#include "settings.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h"
#include <sys/stat.h>
#include <errno.h>

static const char *TAG = "ui";

// ─── Screen references ────────────────────────────────────────────────────────

static lv_obj_t *screen_clock    = NULL;
static lv_obj_t *screen_settings = NULL;
static lv_obj_t *screen_wifi     = NULL;
static lv_obj_t *screen_brightness = NULL;
static lv_obj_t *screen_about    = NULL;

static lv_obj_t *lbl_time;
static lv_obj_t *lbl_ampm;
static lv_obj_t *lbl_date;
static lv_obj_t *bg_img          = NULL;

static struct tm         saved_timeinfo;
static clock_settings_t  saved_settings;
static lv_obj_t         *box_time_ampm;

LV_FONT_DECLARE(nunito_48);
LV_FONT_DECLARE(nunito_256);

#define ICON_PAD    8

// ─── Private: clock helpers ───────────────────────────────────────────────────

/**
 * @brief Format time/AM-PM/date strings from a broken-down time structure.
 *
 * @param ti        Broken-down local time.
 * @param buf_time  Output buffer for HH:MM (12-hour), at least 6 bytes.
 * @param time_sz   Size of buf_time.
 * @param buf_ampm  Output buffer for "AM"/"PM", at least 3 bytes.
 * @param ampm_sz   Size of buf_ampm.
 * @param buf_date  Output buffer for day string, at least 30 bytes.
 * @param date_sz   Size of buf_date.
 */
static void clock_format_strings(const struct tm *ti,
                                  char *buf_time, size_t time_sz,
                                  char *buf_ampm, size_t ampm_sz,
                                  char *buf_date, size_t date_sz)
{
    int h12 = ti->tm_hour % 12;
    if (!h12) h12 = 12;
    snprintf(buf_time, time_sz, "%02d:%02d", h12, ti->tm_min);
    snprintf(buf_ampm, ampm_sz, "%s", ti->tm_hour < 12 ? "AM" : "PM");
    strftime(buf_date, date_sz, "%A, %B, %d", ti);
}

/**
 * @brief LVGL timer callback: update clock display labels.
 *
 * Reads the current local time, formats strings, and applies them to the
 * LVGL labels under the LVGL port lock.
 *
 * @param t LVGL timer handle (unused).
 */
static void clock_update_cb(lv_timer_t *t)
{
    (void)t;
    time_t    now;
    struct tm ti;
    time_sync_get_local(&now, &ti);
    ESP_LOGI(TAG, "clock_update_cb: %04d-%02d-%02d %02d:%02d:%02d",
             ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday,
             ti.tm_hour, ti.tm_min, ti.tm_sec);

    char buf_time[6], buf_ampm[3], buf_date[30];
    clock_format_strings(&ti,
                         buf_time, sizeof(buf_time),
                         buf_ampm, sizeof(buf_ampm),
                         buf_date, sizeof(buf_date));

    lvgl_port_lock(0);
    lv_label_set_text(lbl_time, buf_time);
    lv_label_set_text(lbl_ampm, buf_ampm);
    lv_label_set_text(lbl_date, buf_date);
    lvgl_port_unlock();
}

// ─── Private: touch and gesture callbacks ─────────────────────────────────────

/**
 * @brief LVGL event callback: log touch events on the clock screen (debug only).
 *
 * @param e LVGL event handle.
 */
static void clock_screen_touch_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED)       ESP_LOGI(TAG, "touch: PRESSED");
    else if (code == LV_EVENT_RELEASED) ESP_LOGI(TAG, "touch: RELEASED");
    else if (code == LV_EVENT_CLICKED)  ESP_LOGI(TAG, "touch: CLICKED");
}

/**
 * @brief LVGL event callback: handle swipe gestures on the clock screen.
 *
 * Swipe UP opens the settings menu. All other directions are logged.
 *
 * @param e LVGL event handle.
 */
static void clock_screen_gesture_cb(lv_event_t *e)
{
    (void)e;
    lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());
    switch (dir) {
        case LV_DIR_TOP:
            ESP_LOGI(TAG, "gesture: SWIPE UP — opening settings");
            screen_manager_push(SCREEN_SETTINGS_MENU);
            break;
        case LV_DIR_BOTTOM:
            ESP_LOGI(TAG, "gesture: SWIPE DOWN");
            break;
        case LV_DIR_LEFT:
            ESP_LOGI(TAG, "gesture: SWIPE LEFT");
            break;
        case LV_DIR_RIGHT:
            ESP_LOGI(TAG, "gesture: SWIPE RIGHT");
            break;
        default:
            break;
    }
}

// ─── Private: GIF background helpers ─────────────────────────────────────────

/**
 * @brief Configure a GIF object for fullscreen infinite-loop background playback.
 *
 * Must be called with the LVGL port lock held.
 *
 * @param gif_obj LVGL GIF object to configure.
 */
static void optimize_gif_for_background(lv_obj_t *gif_obj)
{
    lv_obj_set_width(gif_obj,  lv_obj_get_width(lv_scr_act()));
    lv_obj_set_height(gif_obj, lv_obj_get_height(lv_scr_act()));
    lv_gif_set_loop_count(gif_obj, -1);
    lv_gif_restart(gif_obj);
    ESP_LOGI(TAG, "optimize_gif_for_background: fullscreen infinite loop set");
}

// ─── Private: background loading helpers ─────────────────────────────────────

/**
 * @brief Apply the shared section-box style to an LVGL object.
 *
 * Sets transparent background, no border, and rounded corners.
 *
 * @param obj LVGL object to style.
 */
static void ui_style_section(lv_obj_t *obj)
{
    lv_obj_set_style_bg_opa(obj,      LV_OPA_TRANSP,    0);
    lv_obj_set_style_border_color(obj, lv_color_white(), 0);
    lv_obj_set_style_border_width(obj, 0,                0);
    lv_obj_set_style_radius(obj,      16,               0);
    lv_obj_set_style_bg_color(obj,    lv_color_black(), 0);
}

/**
 * @brief Return true if the given path has a .gif extension (case-insensitive).
 *
 * @param path File path string.
 * @return true if path ends in ".gif".
 */
static bool ui_is_gif_path(const char *path)
{
    const char *ext = strrchr(path, '.');
    return ext != NULL && strcasecmp(ext, ".gif") == 0;
}

/**
 * @brief Create a background image LVGL object and set its common positioning flags.
 *
 * Creates either a GIF or static image object depending on is_gif, loads the
 * file, positions it centred, sends it to the background layer, and clears
 * scrollable/layout flags.
 *
 * @param scr    Parent screen object.
 * @param path   LVGL file path string (e.g. "A:/bg.gif").
 * @param is_gif true to create an lv_gif object; false for lv_img.
 * @return The created LVGL object.
 */
static lv_obj_t *ui_bg_create_object(lv_obj_t *scr, const char *path, bool is_gif)
{
    lv_obj_t *obj;
    if (is_gif) {
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
    return obj;
}

/**
 * @brief Release the LVGL lock, delay 50 ms, re-acquire, then optimise the GIF.
 *
 * The 50 ms delay allows the GIF decoder to initialise before the loop/size
 * parameters are applied. Caller must hold the LVGL lock before this call;
 * the lock is re-held on return.
 *
 * @param obj GIF LVGL object to configure.
 */
static void ui_bg_finalize_gif(lv_obj_t *obj)
{
    lvgl_port_unlock();
    vTaskDelay(pdMS_TO_TICKS(50));
    lvgl_port_lock(0);
    optimize_gif_for_background(obj);
}

/**
 * @brief Verify that a background image object loaded successfully.
 *
 * Deletes the object and returns false if the image/GIF failed to load.
 * Must be called with the LVGL lock held.
 *
 * @param obj    The LVGL background object to verify.
 * @param is_gif true if obj is a GIF object.
 * @param path   Path string used only for log messages.
 * @return true if the image is loaded; false if it was deleted.
 */
static bool ui_bg_verify(lv_obj_t *obj, bool is_gif, const char *path)
{
    bool loaded = is_gif ? lv_gif_is_loaded(obj) : (lv_img_get_src(obj) != NULL);
    if (!loaded) {
        ESP_LOGW(TAG, "ui_bg_verify: background failed to load from %s", path);
        lv_obj_del(obj);
        return false;
    }
    ESP_LOGI(TAG, "ui_bg_verify: background loaded from %s", path);
    return true;
}

/**
 * @brief Load the configured background image from the SD card.
 *
 * Returns NULL immediately if the SD card is not mounted or no image is
 * configured. Otherwise creates the LVGL object, applies GIF optimisation
 * if needed (with a brief lock-release delay), and verifies the load.
 *
 * This function manages its own LVGL port lock — it must be called without
 * the lock already held.
 *
 * @param scr      Parent screen object.
 * @param settings Clock settings containing the background_image path.
 * @return Loaded LVGL object, or NULL if loading failed or skipped.
 */

/**
 * @brief Create, optionally finalise, and verify a background object under the LVGL lock.
 *
 * Acquires and releases the LVGL lock internally (may briefly release it for
 * GIF initialisation delay). Caller must not hold the lock.
 *
 * @param scr    Parent screen object.
 * @param path   LVGL file path string.
 * @param is_gif true if the path refers to a GIF file.
 * @return Created object on success, NULL if verification failed.
 */
static lv_obj_t *ui_bg_load_locked(lv_obj_t *scr, const char *path, bool is_gif)
{
    lvgl_port_lock(0);
    lv_obj_t *obj = ui_bg_create_object(scr, path, is_gif);
    if (is_gif) ui_bg_finalize_gif(obj);
    bool ok = ui_bg_verify(obj, is_gif, path);
    lvgl_port_unlock();
    return ok ? obj : NULL;
}

static lv_obj_t *ui_bg_try_load(lv_obj_t *scr, const clock_settings_t *settings)
{
    if (!sdcard_is_mounted() || !settings || !settings->background_image[0]) {
        ESP_LOGW(TAG, "ui_bg_try_load: SD not mounted or no background configured");
        return NULL;
    }
    const char *path   = settings->background_image;
    bool        is_gif = ui_is_gif_path(path);
    ESP_LOGI(TAG, "ui_bg_try_load: loading %s (gif=%d)", path, (int)is_gif);
    return ui_bg_load_locked(scr, path, is_gif);
}

// ─── Private: clock Lottie animation ─────────────────────────────────────────

#if LV_USE_LOTTIE

#define CLOCK_LOTTIE_W          200
#define CLOCK_LOTTIE_H          200
#define CLOCK_LOTTIE_FPS        20
#define CLOCK_LOTTIE_PATH       "/sdcard/hummingbird.json"
#define CLOCK_LOTTIE_LOAD_STACK (64 * 1024)  /* DRAM — hummingbird parse is deep recursive */

static lv_obj_t *s_clock_lottie_widget = NULL;
static void     *s_clock_lottie_buf    = NULL;

typedef struct {
    lv_obj_t *widget;
    char      path[256];
    uint32_t  fps;
} clock_lottie_arg_t;

/**
 * @brief Allocate the 64-byte-aligned SPIRAM render buffer for the clock Lottie.
 * @return true on success; sets s_clock_lottie_buf.
 */
static bool clock_lottie_alloc_buf(void)
{
    uint32_t stride = lv_draw_buf_width_to_stride(CLOCK_LOTTIE_W,
                                                   LV_COLOR_FORMAT_ARGB8888_PREMULTIPLIED);
    size_t buf_sz = (size_t)stride * CLOCK_LOTTIE_H;
    ESP_LOGI(TAG, "clock_lottie: alloc SPIRAM buf %zu B (stride=%lu)", buf_sz, (unsigned long)stride);
    s_clock_lottie_buf = heap_caps_aligned_alloc(64, buf_sz, MALLOC_CAP_SPIRAM);
    if (!s_clock_lottie_buf) {
        ESP_LOGE(TAG, "clock_lottie: SPIRAM alloc failed");
        return false;
    }
    ESP_LOGI(TAG, "clock_lottie: buf at %p", s_clock_lottie_buf);
    return true;
}

/**
 * @brief Create the lv_lottie widget and verify its render task started.
 * @return Widget or NULL on failure.
 */
static lv_obj_t *clock_lottie_create_obj(lv_obj_t *scr)
{
    lv_obj_t *widget = lv_lottie_create(scr);
    if (!widget) {
        ESP_LOGE(TAG, "clock_lottie: lv_lottie_create failed");
        return NULL;
    }
    if (lv_lottie_render_failed(widget)) {
        ESP_LOGE(TAG, "clock_lottie: render task failed (PSRAM exhausted?)");
        lv_obj_delete(widget);
        return NULL;
    }
    return widget;
}

/**
 * @brief Size, buffer, and align the Lottie widget on the clock screen.
 */
static void clock_lottie_setup_widget(lv_obj_t *widget)
{
    lv_obj_set_size(widget, CLOCK_LOTTIE_W, CLOCK_LOTTIE_H);
    lv_lottie_set_buffer(widget, CLOCK_LOTTIE_W, CLOCK_LOTTIE_H, s_clock_lottie_buf);
    lv_obj_align(widget, LV_ALIGN_BOTTOM_LEFT, 0, -16);
    s_clock_lottie_widget = widget;
    ESP_LOGI(TAG, "clock_lottie: widget=%p placed BOTTOM_LEFT", widget);
}

/**
 * @brief Rescale the lv_anim driving the Lottie from the default 60fps to @p fps.
 *
 * lv_lottie.c hardcodes 60fps on load.  Called while the LVGL lock is held.
 */
static void clock_lottie_set_fps(lv_obj_t *widget, uint32_t fps)
{
    lv_anim_t *a = lv_lottie_get_anim(widget);
    if (!a || fps == 0) return;
    uint32_t dur = lv_anim_get_time(a);
    lv_anim_set_duration(a, dur * 60 / fps);
    a->act_time = 0;
    ESP_LOGI(TAG, "clock_lottie: fps=%lu dur=%lu→%lu ms", (unsigned long)fps,
             (unsigned long)dur, (unsigned long)(dur * 60 / fps));
}

/**
 * @brief Check that the Lottie file exists on the SD card.
 */
static bool clock_lottie_check_file(const char *path)
{
    struct stat st;
    if (stat(path, &st) != 0) {
        ESP_LOGE(TAG, "clock_lottie: file not found '%s' errno=%d", path, errno);
        return false;
    }
    ESP_LOGI(TAG, "clock_lottie: file found, size=%lld B", (long long)st.st_size);
    return true;
}

/**
 * @brief Log entry stats for the clock lottie load task.
 */
static void clock_lottie_log_entry(const char *path, const void *probe)
{
    ESP_LOGI(TAG, "clock_lottie_load_task: start path='%s' stack=%p", path, probe);
    ESP_LOGI(TAG, "clock_lottie_load_task: heap=%lu SPIRAM=%lu stack_hw=%lu",
             (unsigned long)esp_get_free_heap_size(),
             (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
             (unsigned long)uxTaskGetStackHighWaterMark(NULL) * sizeof(StackType_t));
}

/**
 * @brief Acquire LVGL lock, load the Lottie file, and set target FPS.
 */
static void clock_lottie_load_and_set_fps(lv_obj_t *widget, const char *path, uint32_t fps)
{
    ESP_LOGI(TAG, "clock_lottie: acquiring LVGL lock");
    lvgl_port_lock(0);
    ESP_LOGI(TAG, "clock_lottie: calling lv_lottie_set_src_file");
    lv_lottie_set_src_file(widget, path);
    ESP_LOGI(TAG, "clock_lottie: lv_lottie_set_src_file returned");
    clock_lottie_set_fps(widget, fps);
    lvgl_port_unlock();
}

/**
 * @brief Load task: runs on a 64 KB DRAM stack to parse hummingbird.json.
 */
static void clock_lottie_load_task(void *arg)
{
    clock_lottie_arg_t *a = (clock_lottie_arg_t *)arg;
    lv_obj_t *widget = a->widget;
    uint32_t  fps    = a->fps;
    char path[256];
    strlcpy(path, a->path, sizeof(path));
    free(a);

    volatile uint8_t _probe = 0;
    clock_lottie_log_entry(path, (const void *)&_probe);

    if (clock_lottie_check_file(path)) {
        clock_lottie_load_and_set_fps(widget, path, fps);
    }
    vTaskDeleteWithCaps(NULL);
}

/**
 * @brief Spawn the clock lottie load task on a DRAM stack.
 */
static void clock_lottie_spawn(lv_obj_t *widget, const char *path)
{
    clock_lottie_arg_t *a = malloc(sizeof(*a));
    if (!a) {
        ESP_LOGE(TAG, "clock_lottie: failed to alloc task arg");
        return;
    }
    a->widget = widget;
    a->fps    = CLOCK_LOTTIE_FPS;
    strlcpy(a->path, path, sizeof(a->path));
    ESP_LOGI(TAG, "clock_lottie: spawning load task (stack=%d B, DRAM)", CLOCK_LOTTIE_LOAD_STACK);
    BaseType_t ret = xTaskCreateWithCaps(clock_lottie_load_task, "clk_lottie",
                                         CLOCK_LOTTIE_LOAD_STACK, a, 5, NULL,
                                         MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "clock_lottie: task spawn failed (ret=%d)", (int)ret);
        free(a);
    }
}

/**
 * @brief Create and start the hummingbird Lottie on the clock screen.
 *
 * Allocates the SPIRAM render buffer, creates the widget under the LVGL lock,
 * then spawns the load task to parse hummingbird.json on a DRAM stack.
 * Safe to call from any task (manages its own lock).
 *
 * @param scr Clock screen object.
 */
static void ui_clock_add_lottie(lv_obj_t *scr)
{
    if (!clock_lottie_alloc_buf()) return;

    lvgl_port_lock(0);
    lv_obj_t *widget = clock_lottie_create_obj(scr);
    if (widget) clock_lottie_setup_widget(widget);
    lvgl_port_unlock();

    if (widget) {
        clock_lottie_spawn(widget, CLOCK_LOTTIE_PATH);
    } else {
        free(s_clock_lottie_buf);
        s_clock_lottie_buf = NULL;
    }
}

#endif /* LV_USE_LOTTIE */

// ─── Private: clock screen builders ──────────────────────────────────────────

/**
 * @brief Derive the display text colour from settings, defaulting to white.
 *
 * @param settings Clock settings; may be NULL.
 * @return LVGL colour value.
 */
static lv_color_t ui_color_from_settings(const clock_settings_t *settings)
{
    if (!settings) return lv_color_white();
    uint32_t rgb = settings->text_color;
    ESP_LOGI(TAG, "ui_color_from_settings: 0x%06lX", (unsigned long)rgb);
    return lv_color_make((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
}

/**
 * @brief Create and position the time box container on the clock screen.
 *
 * Sets module-level box_time_ampm. Must be called with the LVGL lock held.
 *
 * @param scr Parent screen object.
 */
static void ui_create_time_box_container(lv_obj_t *scr)
{
    box_time_ampm = lv_obj_create(scr);
    ui_style_section(box_time_ampm);
    lv_obj_clear_flag(box_time_ampm, LV_OBJ_FLAG_SCROLLABLE);
    int w = lv_obj_get_width(scr) - ICON_PAD * 2;
    lv_obj_set_size(box_time_ampm, w, 350);
    lv_obj_align(box_time_ampm, LV_ALIGN_TOP_MID, 0, ICON_PAD);
}

/**
 * @brief Apply inner padding to the time box container.
 *
 * Must be called after ui_create_time_box_container(). Requires LVGL lock.
 */
static void ui_set_time_box_padding(void)
{
    lv_obj_set_style_pad_left(box_time_ampm,  ICON_PAD, 0);
    lv_obj_set_style_pad_right(box_time_ampm, ICON_PAD, 0);
    lv_obj_set_style_pad_top(box_time_ampm,   ICON_PAD, 0);
}

/**
 * @brief Create the primary time label (HH:MM) inside the time box.
 *
 * Sets module-level lbl_time. Must be called with the LVGL lock held.
 *
 * @param box   Parent time box object.
 * @param color Text colour.
 */
static void ui_create_time_labels(lv_obj_t *box, lv_color_t color)
{
    lbl_time = lv_label_create(box);
    lv_obj_set_style_text_font(lbl_time,  &nunito_256,         0);
    lv_obj_set_style_text_color(lbl_time, color,               0);
    lv_obj_set_style_text_align(lbl_time, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(lbl_time, LV_ALIGN_TOP_MID, 0, 24);
}

/**
 * @brief Create the AM/PM and date labels inside the time box.
 *
 * Sets module-level lbl_ampm and lbl_date. Must be called after
 * ui_create_time_labels(). Requires LVGL lock.
 *
 * @param box   Parent time box object.
 * @param color Text colour.
 */
static void ui_create_ampm_date_labels(lv_obj_t *box, lv_color_t color)
{
    lbl_ampm = lv_label_create(box);
    lv_obj_set_style_text_font(lbl_ampm,  &nunito_48, 0);
    lv_obj_set_style_text_color(lbl_ampm, color,      0);
    lv_obj_align_to(lbl_ampm, lbl_time, LV_ALIGN_OUT_RIGHT_MID, 160, 0);

    lbl_date = lv_label_create(box);
    lv_obj_set_style_text_font(lbl_date,  &nunito_48,          0);
    lv_obj_set_style_text_color(lbl_date, color,               0);
    lv_obj_set_style_text_align(lbl_date, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(lbl_date, LV_ALIGN_BOTTOM_MID, 0, -16);
}

/**
 * @brief Create the complete time/date box on the clock screen.
 *
 * Orchestrates container creation, padding, and label creation.
 * Must be called with the LVGL lock held.
 *
 * @param scr   Parent screen object.
 * @param color Text colour for all labels.
 */
static void ui_create_time_box(lv_obj_t *scr, lv_color_t color)
{
    ui_create_time_box_container(scr);
    ui_set_time_box_padding();
    ui_create_time_labels(box_time_ampm, color);
    ui_create_ampm_date_labels(box_time_ampm, color);
}

/**
 * @brief Register clock timers and touch/gesture event callbacks.
 *
 * Creates 60-second clock update timer and 30-minute weather update timer.
 * Registers touch and gesture callbacks on the screen object.
 * Must be called with the LVGL lock held.
 *
 * @param scr Clock screen object.
 */
static void ui_register_clock_callbacks(lv_obj_t *scr)
{
    lv_timer_create(clock_update_cb, 60000, NULL);
    lv_obj_add_event_cb(scr, clock_screen_touch_cb,   LV_EVENT_PRESSED,  NULL);
    lv_obj_add_event_cb(scr, clock_screen_touch_cb,   LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(scr, clock_screen_touch_cb,   LV_EVENT_CLICKED,  NULL);
    lv_obj_add_event_cb(scr, clock_screen_gesture_cb, LV_EVENT_GESTURE,  NULL);
}

/**
 * @brief Configure the clock screen background colour and base flags.
 *
 * Must be called with the LVGL lock held.
 *
 * @param scr Clock screen object.
 */
static void ui_clock_setup_screen(lv_obj_t *scr)
{
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(scr, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER,      LV_PART_MAIN);
}

/**
 * @brief Build all LVGL clock screen objects under the LVGL lock.
 *
 * Creates the time box, info row, and registers all timers and callbacks.
 *
 * @param scr      Clock screen object (already configured).
 * @param settings Clock settings for colour derivation.
 */
static void ui_clock_build_locked(lv_obj_t *scr, const clock_settings_t *settings)
{
    ui_clock_setup_screen(scr);
    lv_color_t color = ui_color_from_settings(settings);
    ui_create_time_box(scr, color);
    ui_register_clock_callbacks(scr);
}

/**
 * @brief Perform post-lock clock initialisation steps.
 *
 * Loads the background (manages its own lock), triggers the initial data
 * callbacks (which manage their own locks and network I/O), and registers
 * the screen with the screen manager.
 *
 * @param scr      Clock screen object.
 * @param settings Clock settings containing the background path.
 */
static void ui_clock_post_init(lv_obj_t *scr, const clock_settings_t *settings)
{
    bg_img = ui_bg_try_load(scr, settings);
    clock_update_cb(NULL);
    screen_manager_set_clock_screen(scr);
#if LV_USE_LOTTIE
    ui_clock_add_lottie(scr);
#endif
    ESP_LOGI(TAG, "ui_clock_post_init: complete, heap=%lu",
             (unsigned long)esp_get_free_heap_size());
}

// ─── Public: clock screen ────────────────────────────────────────────────────

/**
 * @brief Initialise and display the main clock screen.
 *
 * Creates all LVGL objects, loads the configured background, triggers
 * initial time and weather fetches, and registers the screen with the
 * screen manager. This function is safe to call from any task.
 *
 * @param ti0      Initial time (unused; callbacks fetch current time directly).
 * @param settings Clock configuration (background path, text colour, etc.).
 */
void ui_clock_init(const struct tm *ti0, const clock_settings_t *settings)
{
    (void)ti0;
    ESP_LOGI(TAG, "ui_clock_init: start");
    screen_manager_init();

    lvgl_port_lock(0);
    lv_obj_t *scr = lv_scr_act();
    screen_clock = scr;
    ui_clock_build_locked(scr, settings);
    lvgl_port_unlock();

    ui_clock_post_init(scr, settings);
    ESP_LOGI(TAG, "ui_clock_init: complete");
}

// ─── Private: splash screen helpers ──────────────────────────────────────────

/**
 * @brief Create and load the splash image from SPIFFS onto the given screen.
 *
 * Must be called with the LVGL lock held.
 *
 * @param scr Parent screen object.
 */
static void ui_splash_load_image(lv_obj_t *scr)
{
    lv_obj_t *img = lv_img_create(scr);
    lv_img_set_src(img, "B:/splash.png");
    lv_obj_align(img, LV_ALIGN_CENTER, 0, 0);
    const void *src = lv_img_get_src(img);
    if (!src) {
        ESP_LOGW(TAG, "ui_splash_load_image: failed to load B:/splash.png");
    } else {
        ESP_LOGI(TAG, "ui_splash_load_image: loaded, src=%p", src);
    }
}

// ─── Public: splash screen ───────────────────────────────────────────────────

/**
 * @brief Display the splash screen from SPIFFS.
 *
 * Clears the current screen, sets a black background, and loads
 * "B:/splash.png" (SPIFFS). Used during boot before Wi-Fi is ready.
 */
void ui_show_splash(void)
{
    ESP_LOGI(TAG, "ui_show_splash: start");
    lvgl_port_lock(0);
    lv_obj_t *scr = lv_scr_act();
    lv_obj_clean(scr);
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    ui_splash_load_image(scr);
    lvgl_port_unlock();
    ESP_LOGI(TAG, "ui_show_splash: complete");
}

// ─── Private: start screen helpers ───────────────────────────────────────────

/**
 * @brief Callback invoked when the Start Clock button is pressed.
 *
 * Clears the current screen and transitions to the clock UI using the
 * previously saved time and settings from ui_show_start_screen().
 *
 * @param e LVGL event handle (unused).
 */
static void start_button_cb(lv_event_t *e)
{
    (void)e;
    ESP_LOGI(TAG, "start_button_cb: launching clock UI");
    lvgl_port_lock(0);
    lv_obj_t *scr = lv_scr_act();
    lv_obj_clean(scr);
    lvgl_port_unlock();
    ui_clock_init(&saved_timeinfo, &saved_settings);
}

static volatile bool s_clock_launched = false;

void ui_launch_clock(void)
{
    if (s_clock_launched) {
        ESP_LOGW(TAG, "ui_launch_clock: already launched, ignoring duplicate call");
        return;
    }
    s_clock_launched = true;
    ESP_LOGI(TAG, "ui_launch_clock: launching clock UI");
    lvgl_port_lock(0);
    lv_obj_t *scr = lv_scr_act();
    lv_obj_clean(scr);
    lvgl_port_unlock();
    ui_clock_init(&saved_timeinfo, &saved_settings);
}

/**
 * @brief Create the "Greenwood Clock" title label on the start screen.
 *
 * Must be called with the LVGL lock held.
 *
 * @param scr Parent screen object.
 */
static void ui_start_create_title(lv_obj_t *scr)
{
    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "Greenwood Clock");
    lv_obj_set_style_text_font(title,  &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(title, lv_color_white(),       0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -200);
}

/**
 * @brief Create the status info text label on the start screen.
 *
 * Must be called with the LVGL lock held.
 *
 * @param scr Parent screen object.
 */
static void ui_start_create_info_text(lv_obj_t *scr)
{
    lv_obj_t *info = lv_label_create(scr);
    lv_label_set_text(info, "System Ready\nOTA updates available from this screen");
    lv_obj_set_style_text_align(info,  LV_TEXT_ALIGN_CENTER,  0);
    lv_obj_set_style_text_color(info,  lv_color_hex(0x808080), 0);
    lv_obj_align(info, LV_ALIGN_CENTER, 0, -120);
}

/**
 * @brief Create the OTA URL section label on the start screen.
 *
 * Must be called with the LVGL lock held.
 *
 * @param scr Parent screen object.
 */
static void ui_start_create_ota_url_label(lv_obj_t *scr)
{
    lv_obj_t *lbl = lv_label_create(scr);
    lv_label_set_text(lbl, "OTA Server URL:");
    lv_obj_set_style_text_color(lbl, lv_color_white(), 0);
    lv_obj_align(lbl, LV_ALIGN_CENTER, 0, -60);
}

/**
 * @brief Create a single OTA server URL text area at the given horizontal offset.
 *
 * Must be called with the LVGL lock held.
 *
 * @param scr      Parent screen object.
 * @param x_offset Horizontal offset from screen centre in pixels.
 * @param url      Initial URL text; uses placeholder if empty.
 */
static void ui_start_create_ota_textarea(lv_obj_t *scr, int x_offset, const char *url)
{
    lv_obj_t *ta = lv_textarea_create(scr);
    lv_obj_set_size(ta, 520, 40);
    lv_obj_align(ta, LV_ALIGN_CENTER, x_offset, -20);
    lv_textarea_set_one_line(ta, true);
    lv_textarea_set_placeholder_text(ta, "http://192.168.1.96:8000");
    lv_textarea_set_text(ta, url[0] ? url : "http://192.168.1.96:8000");
}

/**
 * @brief Create the header section (title + info text) on the start screen.
 *
 * Must be called with the LVGL lock held.
 *
 * @param scr Parent screen object.
 */
static void ui_start_create_header(lv_obj_t *scr)
{
    ui_start_create_title(scr);
    ui_start_create_info_text(scr);
}

/**
 * @brief Create the OTA URL section (label + two text areas) on the start screen.
 *
 * Must be called with the LVGL lock held.
 *
 * @param scr Parent screen object.
 * @param url Initial URL text to pre-fill in both text areas.
 */
static void ui_start_create_ota_section(lv_obj_t *scr, const char *url)
{
    ui_start_create_ota_url_label(scr);
    ui_start_create_ota_textarea(scr, -135, url);
    ui_start_create_ota_textarea(scr,  135, url);
}

/**
 * @brief Create an OTA update button at the given horizontal offset.
 *
 * Must be called with the LVGL lock held.
 *
 * @param scr Parent screen object.
 * @param x   Horizontal offset from screen centre in pixels.
 */
static void ui_start_create_ota_btn(lv_obj_t *scr, int x)
{
    lv_obj_t *btn = lv_btn_create(scr);
    lv_obj_set_size(btn, 200, 60);
    lv_obj_align(btn, LV_ALIGN_CENTER, x, 150);
    lv_obj_add_event_cb(btn, start_button_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, LV_SYMBOL_DOWNLOAD " Update");
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_20, 0);
    lv_obj_center(lbl);
}

/**
 * @brief Create the Start Clock button on the start screen.
 *
 * Must be called with the LVGL lock held.
 *
 * @param scr Parent screen object.
 */
static void ui_start_create_start_btn(lv_obj_t *scr)
{
    lv_obj_t *btn = lv_btn_create(scr);
    lv_obj_set_size(btn, 200, 60);
    lv_obj_align(btn, LV_ALIGN_CENTER, 0, 60);
    lv_obj_add_event_cb(btn, start_button_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl = lv_label_create(btn);
    lv_label_set_text(lbl, "Start Clock");
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_24, 0);
    lv_obj_center(lbl);
}

/**
 * @brief Create the Start and OTA button group on the start screen.
 *
 * Must be called with the LVGL lock held.
 *
 * @param scr Parent screen object.
 */
static void ui_start_create_buttons(lv_obj_t *scr)
{
    ui_start_create_start_btn(scr);
    ui_start_create_ota_btn(scr, -220);
    ui_start_create_ota_btn(scr,  220);
}

/**
 * @brief Clear the active LVGL screen and set a black background.
 *
 * Must be called with the LVGL lock held.
 *
 * @return The active (now cleared) screen object.
 */
static lv_obj_t *ui_prepare_screen(void)
{
    lv_obj_t *scr = lv_scr_act();
    lv_obj_clean(scr);
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    return scr;
}

// ─── Public: start screen ────────────────────────────────────────────────────

/**
 * @brief Pre-populate saved context used by ui_launch_clock().
 *
 * Called from main before ui_show_splash() so that ui_clock_init() receives
 * real time and settings rather than zero-initialized values.
 *
 * @param ti       Current local time; may be NULL (no change).
 * @param settings Clock configuration; may be NULL (no change).
 */
void ui_set_clock_context(const struct tm *ti, const clock_settings_t *settings)
{
    if (ti)       saved_timeinfo = *ti;
    if (settings) saved_settings = *settings;
}

/**
 * @brief Display the boot start screen with system status and OTA entry point.
 *
 * Saves the provided time and settings for later use by start_button_cb.
 * Creates the title, info text, OTA URL inputs, and navigation buttons.
 *
 * @param timeinfo Current time (saved for later use); may be NULL.
 * @param settings Clock configuration (saved for later use); may be NULL.
 */
void ui_show_start_screen(const struct tm *timeinfo, const clock_settings_t *settings)
{
    ESP_LOGI(TAG, "ui_show_start_screen: start");
    if (timeinfo) saved_timeinfo = *timeinfo;
    if (settings) saved_settings = *settings;

    const char *url = (settings && settings->ota_server_url[0])
                      ? settings->ota_server_url : "";

    lvgl_port_lock(0);
    lv_obj_t *scr = ui_prepare_screen();
    ui_start_create_header(scr);
    ui_start_create_ota_section(scr, url);
    ui_start_create_buttons(scr);
    lvgl_port_unlock();
    ESP_LOGI(TAG, "ui_show_start_screen: complete");
}

// ─── Private: text colour helper ─────────────────────────────────────────────

/**
 * @brief Apply a text colour to all clock screen labels under the LVGL lock.
 *
 * @param color New text colour to apply.
 */
static void ui_apply_text_color(lv_color_t color)
{
    lvgl_port_lock(0);
    if (lbl_time) lv_obj_set_style_text_color(lbl_time, color, LV_PART_MAIN);
    if (lbl_ampm) lv_obj_set_style_text_color(lbl_ampm, color, LV_PART_MAIN);
    if (lbl_date) lv_obj_set_style_text_color(lbl_date, color, LV_PART_MAIN);
    lvgl_port_unlock();
}

// ─── Public: refresh functions ────────────────────────────────────────────────

/**
 * @brief Delete the current background object under the LVGL lock, if present.
 *
 * Clears the global bg_img pointer after deletion.
 */
static void ui_bg_delete_old(void)
{
    lvgl_port_lock(0);
    if (bg_img) {
        lv_obj_del(bg_img);
        bg_img = NULL;
    }
    lvgl_port_unlock();
}

/**
 * @brief Reload the background image from current settings.
 *
 * Deletes the existing background (if any), then loads the newly configured
 * image from the SD card. Safe to call at any time after ui_clock_init().
 */
void ui_refresh_background(void)
{
    ESP_LOGI(TAG, "ui_refresh_background: start");

    if (!screen_clock) {
        ESP_LOGW(TAG, "ui_refresh_background: clock screen not initialised");
        return;
    }

    clock_settings_t cfg;
    if (settings_load(&cfg) != ESP_OK) {
        ESP_LOGE(TAG, "ui_refresh_background: settings_load failed");
        return;
    }

    ui_bg_delete_old();
    bg_img = ui_bg_try_load(screen_clock, &cfg);
    ESP_LOGI(TAG, "ui_refresh_background: complete, bg=%s", bg_img ? "loaded" : "none");
}

/**
 * @brief Reload the text colour from current settings and apply to all labels.
 *
 * Reads the text_color field from NVS settings and updates lbl_time,
 * lbl_ampm, and lbl_date.
 */
void ui_refresh_text_color(void)
{
    ESP_LOGI(TAG, "ui_refresh_text_color: start");

    if (!screen_clock) {
        ESP_LOGW(TAG, "ui_refresh_text_color: clock screen not initialised");
        return;
    }

    clock_settings_t cfg;
    if (settings_load(&cfg) != ESP_OK) {
        ESP_LOGE(TAG, "ui_refresh_text_color: settings_load failed");
        return;
    }

    ESP_LOGI(TAG, "ui_refresh_text_color: applying 0x%06lX", (unsigned long)cfg.text_color);
    ui_apply_text_color(lv_color_hex(cfg.text_color));
    ESP_LOGI(TAG, "ui_refresh_text_color: complete");
}
