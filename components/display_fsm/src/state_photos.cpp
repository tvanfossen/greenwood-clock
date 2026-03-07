// components/display_fsm/src/state_photos.cpp
//
// PhotoSlideshow state — cycles through images from A:/photos/ directory.

#include "display_states.h"
#include "display_scheduler.h"
#include "display_widgets.h"
#include "esp_log.h"

static const char *TAG = "state_photos";

// Static member definitions
image_rotator_t *PhotoSlideshow::s_rotator      = nullptr;
lv_obj_t        *PhotoSlideshow::s_empty_label   = nullptr;
lv_timer_t      *PhotoSlideshow::s_advance_timer = nullptr;

// Auto-advance interval (milliseconds)
#define PHOTO_ADVANCE_MS 15000

static void photo_advance_timer_cb(lv_timer_t *timer)
{
    image_rotator_t *rotator = (image_rotator_t *)lv_timer_get_user_data(timer);
    if (rotator) {
        image_rotator_advance(rotator);
    }
}

void PhotoSlideshow::entry()
{
    set_state_info(DISPLAY_STATE_PHOTOS, "photos");
    minimize_clock();

    s_rotator = image_rotator_create(s_screen, "A:/photos");

    if (!s_rotator || image_rotator_count(s_rotator) == 0) {
        // No photos — show message
        s_empty_label = lv_label_create(s_screen);
        lv_label_set_text(s_empty_label, "No photos\nUpload via web control");
        lv_obj_set_style_text_color(s_empty_label, lv_color_hex(0x808080), 0);
        lv_obj_set_style_text_font(s_empty_label, &lv_font_montserrat_24, 0);
        lv_obj_set_style_text_align(s_empty_label, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(s_empty_label, LV_ALIGN_CENTER, 0, 0);
        fade_in(s_empty_label);
        ESP_LOGW(TAG, "PhotoSlideshow: no images found");
    } else {
        // Start auto-advance timer (runs in LVGL context, already under lock)
        s_advance_timer = lv_timer_create(photo_advance_timer_cb,
                                           PHOTO_ADVANCE_MS, s_rotator);
        ESP_LOGI(TAG, "PhotoSlideshow: entry (%d images, advance every %dms)",
                 image_rotator_count(s_rotator), PHOTO_ADVANCE_MS);
    }
}

void PhotoSlideshow::exit()
{
    if (s_advance_timer) { lv_timer_delete(s_advance_timer); s_advance_timer = NULL; }
    if (s_empty_label) { lv_obj_del(s_empty_label); s_empty_label = NULL; }
    if (s_rotator) { image_rotator_destroy(s_rotator); s_rotator = NULL; }
    ESP_LOGI(TAG, "PhotoSlideshow: exit");
}

void PhotoSlideshow::react(EvDisplayTimeout const &)
{
    transit<ClockFull>();
}
