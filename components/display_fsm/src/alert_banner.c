// components/display_fsm/src/alert_banner.c
//
// Layer 1: AlertBanner — colored banner with scrolling headline text.
// Composited on top of any display state.
// Caller must hold LVGL lock for all functions.

#include "display_widgets.h"
#include "ui_contrast.h"
#include "nws.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "alert_banner";

// Local alias — canonical value is ALERT_BANNER_HEIGHT in display_widgets.h
#define BANNER_HEIGHT   ALERT_BANNER_HEIGHT

// Circular-scroll pacing. We CANNOT use lv_anim_speed*(): its encoding truncates the
// max scroll time to ~10s (lv_anim_speed_clamped caps max_time at 10000ms), so any
// long alert headline clamps to a fast 10s/cycle regardless of the px/s asked for.
// Instead set a PLAIN anim_duration in ms, sized to the headline length so the crawl
// speed stays ~constant. At font 20 (~11px/char), ~240ms/char ≈ 46px/s — a slow,
// readable ticker (vs the old ~200-400px/s from the 10s clamp).
#define SCROLL_MS_PER_CHAR  240
#define SCROLL_MIN_MS       8000

struct alert_banner_t {
    lv_obj_t *container;
    lv_obj_t *lbl_headline;
    bool      visible;
};

static lv_color_t severity_color(const char *severity)
{
    if (strcmp(severity, "Extreme") == 0) return lv_color_hex(0xCC0000);  // dark red
    if (strcmp(severity, "Severe") == 0)  return lv_color_hex(0xE76F51);  // coral/red
    if (strcmp(severity, "Moderate") == 0) return lv_color_hex(0xE9C46A); // yellow
    if (strcmp(severity, "Minor") == 0)   return lv_color_hex(0x4A6FA5);  // blue
    return lv_color_hex(0xE76F51);  // default to coral
}

alert_banner_t *alert_banner_create(lv_obj_t *parent)
{
    alert_banner_t *b = lv_malloc(sizeof(alert_banner_t));
    if (!b) {
        ESP_LOGE(TAG, "Failed to allocate alert_banner_t");
        return NULL;
    }
    memset(b, 0, sizeof(*b));

    // Full-width banner at top of screen
    lv_color_t default_bg = lv_color_hex(0xE76F51);
    b->container = lv_obj_create(parent);
    lv_obj_set_size(b->container, 1024, BANNER_HEIGHT);
    lv_obj_align(b->container, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(b->container, default_bg, 0);
    lv_obj_set_style_bg_opa(b->container, LV_OPA_90, 0);
    lv_obj_set_style_border_width(b->container, 0, 0);
    lv_obj_set_style_pad_all(b->container, 0, 0);
    lv_obj_set_style_radius(b->container, 0, 0);
    lv_obj_clear_flag(b->container, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(b->container, LV_OBJ_FLAG_HIDDEN);

    // Scrolling headline label — colour chosen for contrast vs the banner bg.
    b->lbl_headline = lv_label_create(b->container);
    lv_obj_set_style_text_font(b->lbl_headline, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(b->lbl_headline, ui_text_on(default_bg), 0);
    lv_label_set_long_mode(b->lbl_headline, LV_LABEL_LONG_SCROLL_CIRCULAR);
    // Plain-ms duration (recomputed per headline in alert_banner_show); see the
    // SCROLL_MS_PER_CHAR note above for why we avoid lv_anim_speed*().
    lv_obj_set_style_anim_duration(b->lbl_headline, 20000, 0);
    lv_obj_set_width(b->lbl_headline, 1000);
    lv_obj_align(b->lbl_headline, LV_ALIGN_CENTER, 0, 0);
    lv_label_set_text(b->lbl_headline, "");

    b->visible = false;
    ESP_LOGI(TAG, "AlertBanner created");
    return b;
}

void alert_banner_destroy(alert_banner_t *b)
{
    if (!b) return;
    if (b->container) {
        lv_anim_delete(b->container, NULL);
        lv_obj_delete(b->container);
    }
    lv_free(b);
    ESP_LOGI(TAG, "AlertBanner destroyed");
}

void alert_banner_show(alert_banner_t *b, const nws_alert_t *alert)
{
    if (!b || !alert) return;

    // Set severity color + a contrast-matched headline colour (yellow/Moderate
    // needs black text; the dark severities need white).
    lv_color_t bg = severity_color(alert->severity);
    lv_obj_set_style_bg_color(b->container, bg, 0);
    lv_obj_set_style_text_color(b->lbl_headline, ui_text_on(bg), 0);

    // Set headline text — circular scroll handles overflow
    char text[320];
    int len = snprintf(text, sizeof(text), "  %s — %s  ", alert->event, alert->headline);

    // Size the scroll duration to the headline length (constant slow px/s crawl).
    // Set BEFORE set_text so the scroll animation is (re)created with this duration.
    uint32_t dur_ms = (uint32_t)(len > 0 ? len : 1) * SCROLL_MS_PER_CHAR;
    if (dur_ms < SCROLL_MIN_MS) dur_ms = SCROLL_MIN_MS;
    lv_obj_set_style_anim_duration(b->lbl_headline, dur_ms, 0);
    lv_label_set_text(b->lbl_headline, text);

    // Show
    lv_obj_clear_flag(b->container, LV_OBJ_FLAG_HIDDEN);
    b->visible = true;

    ESP_LOGW(TAG, "Alert shown: %s (%s)", alert->event, alert->severity);
}

void alert_banner_hide(alert_banner_t *b)
{
    if (!b) return;
    lv_obj_add_flag(b->container, LV_OBJ_FLAG_HIDDEN);
    b->visible = false;
}

bool alert_banner_is_visible(const alert_banner_t *b)
{
    return b ? b->visible : false;
}
