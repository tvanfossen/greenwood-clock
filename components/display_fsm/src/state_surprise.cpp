// components/display_fsm/src/state_surprise.cpp
//
// SurpriseMessage state — renders JSON layout DSL pushed via HTTP API.
// Uses s_surprise_json and s_surprise_duration_s stashed by base class
// before transit (since entry() can't access event parameters).

#include "display_states.h"
#include "display_scheduler.h"
#include "display_widgets.h"
#include "esp_log.h"
#include "esp_heap_caps.h"

static const char *TAG = "state_surprise";

// Static member definitions
json_layout_t *SurpriseMessage::s_layout = nullptr;

void SurpriseMessage::entry()
{
    set_state_info(DISPLAY_STATE_SURPRISE, "surprise");
    minimize_clock();

    if (s_surprise_json) {
        s_layout = json_layout_create(s_screen, s_surprise_json);
        if (!s_layout) {
            ESP_LOGE(TAG, "Failed to create layout from JSON");
            // Show fallback text
            lv_obj_t *lbl = lv_label_create(s_screen);
            lv_label_set_text(lbl, "Surprise!");
            lv_obj_set_style_text_color(lbl, lv_color_hex(0xff69b4), 0);
            lv_obj_set_style_text_font(lbl, &lv_font_montserrat_32, 0);
            lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 0);
        }

        // Free the SPIRAM-allocated JSON string (http_api allocated it)
        heap_caps_free((void *)s_surprise_json);
        s_surprise_json = nullptr;
    } else {
        // No JSON — show generic message
        lv_obj_t *lbl = lv_label_create(s_screen);
        lv_label_set_text(lbl, "Surprise!");
        lv_obj_set_style_text_color(lbl, lv_color_hex(0xff69b4), 0);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_32, 0);
        lv_obj_align(lbl, LV_ALIGN_CENTER, 0, 0);
    }

    // Set display duration via scheduler
    display_scheduler_get()->force_show(DISPLAY_STATE_SURPRISE);
    display_scheduler_get()->set_duration(DISPLAY_STATE_SURPRISE,
                                           s_surprise_duration_s * 1000);

    ESP_LOGI(TAG, "SurpriseMessage: entry (duration=%lus)",
             (unsigned long)s_surprise_duration_s);
}

void SurpriseMessage::exit()
{
    if (s_layout) { json_layout_destroy(s_layout); s_layout = NULL; }
    // Restore default surprise duration
    display_scheduler_get()->set_duration(DISPLAY_STATE_SURPRISE, 30000);
    ESP_LOGI(TAG, "SurpriseMessage: exit");
}

void SurpriseMessage::react(EvDisplayTimeout const &)
{
    transit<ClockFull>();
}

// Override to prevent re-entry while already showing a surprise
void SurpriseMessage::react(EvSurpriseMessage const &e)
{
    // Replace current surprise with new one
    if (s_layout) { json_layout_destroy(s_layout); s_layout = NULL; }
    s_surprise_json       = e.json_str;
    s_surprise_duration_s = e.duration_s > 0 ? e.duration_s : 30;

    if (s_surprise_json) {
        s_layout = json_layout_create(s_screen, s_surprise_json);
        heap_caps_free((void *)s_surprise_json);
        s_surprise_json = nullptr;
    }

    display_scheduler_get()->set_duration(DISPLAY_STATE_SURPRISE,
                                           s_surprise_duration_s * 1000);
    display_scheduler_get()->force_show(DISPLAY_STATE_SURPRISE);

    ESP_LOGI(TAG, "SurpriseMessage: replaced (duration=%lus)",
             (unsigned long)s_surprise_duration_s);
}

void SurpriseMessage::react(EvForceState const &e)
{
    if (e.state != DISPLAY_STATE_SURPRISE) {
        DisplayFsm::react(e);
    }
}
