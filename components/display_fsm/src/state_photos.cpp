/**
 * @file state_photos.cpp
 * @version 1
 * @brief PhotoSlideshow state: cycles PNGs from A:/photos every 7 s with off-lock decode, prefetch and a legible minimized clock.
 */
// PhotoSlideshow state — cycles through images from A:/photos/ directory.

#include "display_states.h"
#include "display_scheduler.h"
#include "display_widgets.h"
#include "ui_theme_bind.h"
#include "display_fsm.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"

static const char *TAG = "state_photos";

// Static member definitions
image_rotator_t *PhotoSlideshow::s_rotator      = nullptr;
lv_obj_t        *PhotoSlideshow::s_empty_label   = nullptr;
void            *PhotoSlideshow::s_advance_timer = nullptr;
bool             PhotoSlideshow::s_first_shown   = false;

// Auto-advance interval (milliseconds). At the default ~30s show duration this
// yields several photos per visit instead of just two.
#define PHOTO_ADVANCE_MS 7000

// Next photo index to show, persisted across visits so the slideshow cycles
// through the whole album over time instead of restarting at photo 0 each time.
static int s_next_photo_index = 0;

// FreeRTOS timer callback (timer-service task context) — just posts an event to
// the FSM queue. The decode (off-lock) and swap (under lock) happen on the FSM
// task in process_event, so the LVGL render task is never stalled by a decode.
/**
 * @brief FreeRTOS timer callback: post DISPLAY_EVT_PHOTO_ADVANCE to the FSM queue.
 * @version 1
 * @callback
 */
static void photo_advance_timer_cb(TimerHandle_t)
{
    display_event_t evt = {};
    evt.type = DISPLAY_EVT_PHOTO_ADVANCE;
    display_fsm_send_event(&evt);
}

// OFF-lock: advance the index (after the first photo) and decode it. Runs on the
// FSM task before the LVGL lock is taken — the slow PNG decode never blocks render.
/**
 * @brief Off the LVGL lock: advance to the next photo (except for the first) and decode it.
 * @version 1
 * @req REQ-PHOTO-001
 */
void PhotoSlideshow::decode_next()
{
    if (!s_rotator) return;
    if (s_first_shown) image_rotator_step(s_rotator);
    else               s_first_shown = true;
    image_rotator_decode_current(s_rotator);
}

// UNDER lock: swap to the decoded descriptor (fast).
/**
 * @brief Under the LVGL lock: swap the decoded photo in and colour the minimized clock for it.
 * v2: the clock's colour is judged against the photo behind it.
 * @version 2
 * @req REQ-PHOTO-001
 * @req REQ-BG-002
 */
void PhotoSlideshow::present()
{
    if (!s_rotator) return;
    image_rotator_present(s_rotator);
    const lv_image_dsc_t *photo = image_rotator_shown_dsc(s_rotator);
    clock_widget_t *clk = get_clock();
    if (clk && photo) clock_widget_set_minimized_color(clk, display_fsm_min_clock_color(photo));
}

// OFF-lock: decode the NEXT image ahead of time so the next advance is instant.
/**
 * @brief Off the LVGL lock: decode the next photo ahead of time.
 * @version 1
 * @req REQ-PHOTO-001
 */
void PhotoSlideshow::prefetch()
{
    if (s_rotator) image_rotator_prefetch_next(s_rotator);
}

/**
 * @brief Enter: minimize the clock and create the rotator; with no photos show a "No photos" panel, otherwise start the 7 s advance timer and post the first advance.
 * v2: colours from the theme roles (ui_theme), no literals.
 * v3: colours bound to the theme (ui_bind_*): repainted live on a theme change.
 * @version 3
 * @req REQ-PHOTO-001
 */
void PhotoSlideshow::entry()
{
    set_state_info(DISPLAY_STATE_PHOTOS, "photos");
    minimize_clock();

    s_first_shown = false;
    s_rotator = image_rotator_create(s_screen, "A:/photos", s_next_photo_index);

    if (!s_rotator || image_rotator_count(s_rotator) == 0) {
        // No photos — show message on a surface panel
        s_empty_label = lv_obj_create(s_screen);
        lv_obj_set_size(s_empty_label, 400, 120);
        lv_obj_align(s_empty_label, LV_ALIGN_CENTER, 0, 0);
        lv_obj_set_style_bg_opa(s_empty_label, LV_OPA_70, 0);
        ui_bind_fill(s_empty_label, UI_SLOT_CHIP);
        lv_obj_set_style_radius(s_empty_label, 12, 0);
        lv_obj_set_style_border_width(s_empty_label, 0, 0);
        lv_obj_set_style_pad_all(s_empty_label, 16, 0);
        lv_obj_clear_flag(s_empty_label, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_t *lbl = lv_label_create(s_empty_label);
        lv_label_set_text(lbl, "No photos\nUpload via web control");
        ui_bind_text(lbl, UI_ROLE_TEXT_MUTED);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_24, 0);
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 0);

        fade_in(s_empty_label);
        ESP_LOGW(TAG, "PhotoSlideshow: no images found");
    } else {
        // Periodic advance via a FreeRTOS timer (posts PHOTO_ADVANCE to the FSM
        // queue). Post one now so the first photo decodes off-lock and appears
        // without stalling this transition.
        TimerHandle_t t = xTimerCreate("photo_adv", pdMS_TO_TICKS(PHOTO_ADVANCE_MS),
                                       pdTRUE, nullptr, photo_advance_timer_cb);
        s_advance_timer = t;
        if (t) xTimerStart(t, 0);

        display_event_t evt = {};
        evt.type = DISPLAY_EVT_PHOTO_ADVANCE;
        display_fsm_send_event(&evt);

        ESP_LOGI(TAG, "PhotoSlideshow: entry (%d images, advance every %dms)",
                 image_rotator_count(s_rotator), PHOTO_ADVANCE_MS);
    }
}

/**
 * @brief Leave: delete the advance timer and empty-panel, remember the next photo index for the next visit, and destroy the rotator.
 * @version 1
 * @req REQ-PHOTO-001
 */
void PhotoSlideshow::exit()
{
    if (s_advance_timer) {
        xTimerDelete((TimerHandle_t)s_advance_timer, 0);
        s_advance_timer = nullptr;
    }
    if (s_empty_label) { lv_obj_delete(s_empty_label); s_empty_label = NULL; }
    if (s_rotator) {
        // Resume after the last-shown photo on the next visit so the album
        // cycles through over time instead of repeating the first two.
        int count = image_rotator_count(s_rotator);
        if (count > 0) {
            s_next_photo_index = (image_rotator_index(s_rotator) + 1) % count;
        }
        image_rotator_destroy(s_rotator);
        s_rotator = NULL;
    }
    ESP_LOGI(TAG, "PhotoSlideshow: exit (next start=%d)", s_next_photo_index);
}

/**
 * @brief Display timeout: return to ClockFull.
 * @version 1
 * @req REQ-PHOTO-001
 * @req REQ-DISP-002
 */
void PhotoSlideshow::react(EvDisplayTimeout const &)
{
    transit<ClockFull>();
}
