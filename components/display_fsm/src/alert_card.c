/**
 * @file alert_card.c
 * @version 1
 * @brief Alert details card (see alert_card.h): a dialog panel whose header takes the alert's
 *        severity slot, with the when / where line, the scrollable description and
 *        instructions, and a pager through every active alert. Every colour is bound.
 */

#include "alert_card.h"
#include "nws.h"
#include "ui_theme_bind.h"
#include "display_widgets.h"   // alert_severity_slot
#include "esp_log.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

static const char *TAG = "alert_card";

#define CARD_W        880
#define CARD_H        500
#define HEADER_H      64
#define AUTO_CLOSE_MS 60000

/** The open card: its objects and its own copy of the alerts. */
typedef struct {
    lv_obj_t     *backdrop;
    lv_obj_t     *header;
    lv_obj_t     *title;
    lv_obj_t     *meta;
    lv_obj_t     *when;
    lv_obj_t     *body;
    lv_obj_t     *pager;
    lv_obj_t     *prev;
    lv_obj_t     *next;
    lv_timer_t   *timer;
    nws_alerts_t *alerts;
    int           index;
} card_t;

static card_t s_card;


/**
 * @brief Format epoch seconds @p t as local "Tue 7:00 PM" into @p out ("--" when unknown).
 * @version 1
 * @utility
 */
static void fmt_time(int64_t t, char *out, size_t n)
{
    static const char *const DAYS[7] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
    struct tm tm;
    time_t tt = (time_t)t;
    if (t > 0 && localtime_r(&tt, &tm)) {   // hour by hand: no padding, newlib has no %-I
        int h12 = tm.tm_hour % 12 ? tm.tm_hour % 12 : 12;
        snprintf(out, n, "%s %d:%02d %s", DAYS[tm.tm_wday], h12, tm.tm_min,
                 tm.tm_hour < 12 ? "AM" : "PM");
    } else {
        snprintf(out, n, "--");
    }
}

/**
 * @brief Show alert @p s_card.index: header fill and text, when / where, body, pager.
 * @version 1
 * @utility
 */
static void show_alert(void)
{
    const nws_alert_t *a = &s_card.alerts->alerts[s_card.index];
    ui_bind_fill(s_card.header, alert_severity_slot(a->severity));
    ui_bind_text(s_card.title, UI_ROLE_TEXT);   // re-judged on the new header fill
    ui_bind_text(s_card.meta, UI_ROLE_TEXT);
    lv_label_set_text(s_card.title, a->event[0] ? a->event : "Weather alert");
    lv_label_set_text_fmt(s_card.meta, "%s  |  %s  |  %s", a->severity, a->urgency, a->certainty);
    char from[32], until[32];
    fmt_time(a->onset, from, sizeof(from));
    fmt_time(a->expires, until, sizeof(until));
    lv_label_set_text_fmt(s_card.when, "From %s   until %s\n%s", from, until, a->area_desc);
    lv_label_set_text_fmt(s_card.body, "%s\n\n%s", a->headline, a->description);
    if (a->instruction[0]) {
        lv_label_ins_text(s_card.body, LV_LABEL_POS_LAST, "\n\nWhat to do:\n");
        lv_label_ins_text(s_card.body, LV_LABEL_POS_LAST, a->instruction);
    }
    lv_obj_scroll_to_y(lv_obj_get_parent(s_card.body), 0, LV_ANIM_OFF);
    int n = s_card.alerts->alert_count;
    lv_label_set_text_fmt(s_card.pager, "%d / %d", s_card.index + 1, n);
    lv_obj_set_flag(s_card.prev, LV_OBJ_FLAG_HIDDEN, n < 2);
    lv_obj_set_flag(s_card.next, LV_OBJ_FLAG_HIDDEN, n < 2);
    lv_obj_set_flag(s_card.pager, LV_OBJ_FLAG_HIDDEN, n < 2);
}

/**
 * @brief Close on a tap on the backdrop (outside the card: the card itself is clickable, so
 *        taps on it never reach the backdrop), or on the Close button.
 * @version 1
 * @callback
 */
static void close_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    alert_card_close();
}

/**
 * @brief Previous (@p user NULL) or next alert, wrapping; restarts the auto-close timer.
 * @version 1
 * @callback
 */
static void page_cb(lv_event_t *e)
{
    int n = s_card.alerts->alert_count;
    int step = lv_event_get_user_data(e) ? 1 : n - 1;
    s_card.index = (s_card.index + step) % n;
    lv_timer_reset(s_card.timer);
    show_alert();
}

/**
 * @brief Auto-close after AUTO_CLOSE_MS untouched.
 * @version 1
 * @callback
 */
static void timer_cb(lv_timer_t *t)
{
    LV_UNUSED(t);
    alert_card_close();
}

/**
 * @brief A bound label in @p parent.
 * @version 1
 * @utility
 * @return The label.
 */
static lv_obj_t *card_label(lv_obj_t *parent, const lv_font_t *font, ui_role_t role)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    ui_bind_text(l, role);
    return l;
}

/**
 * @brief A footer button labelled @p text calling @p cb with @p user.
 * @version 1
 * @utility
 * @return The button.
 */
static lv_obj_t *card_button(lv_obj_t *parent, const char *text, lv_align_t align, int32_t x,
                             lv_event_cb_t cb, void *user)
{
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_set_size(b, 130, 46);
    lv_obj_align(b, align, x, 0);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, user);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, text);
    lv_obj_center(l);
    return b;
}

/**
 * @brief The header strip: event title and severity line on the severity fill.
 * @version 1
 * @utility
 */
static void build_header(lv_obj_t *card)
{
    s_card.header = lv_obj_create(card);
    lv_obj_remove_style_all(s_card.header);
    lv_obj_set_size(s_card.header, CARD_W, HEADER_H);
    lv_obj_set_style_bg_opa(s_card.header, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_hor(s_card.header, 20, 0);
    lv_obj_remove_flag(s_card.header, LV_OBJ_FLAG_SCROLLABLE);
    s_card.title = card_label(s_card.header, &lv_font_montserrat_24, UI_ROLE_TEXT);
    lv_obj_align(s_card.title, LV_ALIGN_TOP_LEFT, 0, 6);
    s_card.meta = card_label(s_card.header, &lv_font_montserrat_16, UI_ROLE_TEXT);
    lv_obj_align(s_card.meta, LV_ALIGN_BOTTOM_LEFT, 0, -6);
}

/**
 * @brief The when / where line, the scrollable body and the footer (pager, Close).
 * @version 1
 * @utility
 */
static void build_content(lv_obj_t *card)
{
    s_card.when = card_label(card, &lv_font_montserrat_16, UI_ROLE_TEXT_MUTED);
    lv_obj_set_width(s_card.when, CARD_W - 40);
    lv_label_set_long_mode(s_card.when, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_height(s_card.when, 44);
    lv_obj_align(s_card.when, LV_ALIGN_TOP_LEFT, 20, HEADER_H + 10);

    lv_obj_t *scroll = lv_obj_create(card);
    lv_obj_remove_style_all(scroll);
    lv_obj_set_size(scroll, CARD_W - 40, CARD_H - HEADER_H - 64 - 70);
    lv_obj_align(scroll, LV_ALIGN_TOP_LEFT, 20, HEADER_H + 60);
    lv_obj_set_scroll_dir(scroll, LV_DIR_VER);
    s_card.body = card_label(scroll, &lv_font_montserrat_20, UI_ROLE_TEXT);
    lv_obj_set_width(s_card.body, CARD_W - 60);
    lv_label_set_long_mode(s_card.body, LV_LABEL_LONG_MODE_WRAP);

    lv_obj_t *foot = lv_obj_create(card);
    lv_obj_remove_style_all(foot);
    lv_obj_set_size(foot, CARD_W - 40, 50);
    lv_obj_align(foot, LV_ALIGN_BOTTOM_MID, 0, -12);
    s_card.prev = card_button(foot, LV_SYMBOL_LEFT " Prev", LV_ALIGN_LEFT_MID, 0, page_cb, NULL);
    s_card.next = card_button(foot, "Next " LV_SYMBOL_RIGHT, LV_ALIGN_LEFT_MID, 140, page_cb,
                              (void *)1);
    s_card.pager = card_label(foot, &lv_font_montserrat_20, UI_ROLE_TEXT_MUTED);
    lv_obj_align(s_card.pager, LV_ALIGN_CENTER, 0, 0);
    card_button(foot, "Close", LV_ALIGN_RIGHT_MID, 0, close_cb, NULL);
}

/**
 * @brief Open the card at alert @p index with its own copy of @p alerts.
 * @version 1
 * @req REQ-ALERT-002
 */
void alert_card_open(lv_obj_t *layer, const nws_alerts_t *alerts, int index)
{
    alert_card_close();
    if (!alerts || alerts->alert_count <= 0) return;
    s_card.alerts = lv_malloc(sizeof(*alerts));
    if (!s_card.alerts) {
        ESP_LOGE(TAG, "out of memory for the alert copy (%u B)", (unsigned)sizeof(*alerts));
        return;
    }
    memcpy(s_card.alerts, alerts, sizeof(*alerts));
    s_card.index = (index >= 0 && index < alerts->alert_count) ? index : 0;

    s_card.backdrop = lv_obj_create(layer);
    lv_obj_remove_style_all(s_card.backdrop);
    lv_obj_set_size(s_card.backdrop, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_opa(s_card.backdrop, LV_OPA_60, 0);
    lv_obj_add_flag(s_card.backdrop, LV_OBJ_FLAG_CLICKABLE);   // swallow taps; outside = close
    lv_obj_add_event_cb(s_card.backdrop, close_cb, LV_EVENT_CLICKED, NULL);
    ui_bind_fill_role(s_card.backdrop, UI_ROLE_SCREEN);

    lv_obj_t *card = lv_obj_create(s_card.backdrop);
    lv_obj_set_size(card, CARD_W, CARD_H);
    lv_obj_center(card);
    lv_obj_set_style_pad_all(card, 0, 0);
    lv_obj_set_style_radius(card, 14, 0);
    lv_obj_set_style_clip_corner(card, true, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    ui_bind_fill(card, UI_SLOT_DIALOG);
    ui_bind_border(card, UI_ROLE_BORDER);
    build_header(card);
    build_content(card);
    s_card.timer = lv_timer_create(timer_cb, AUTO_CLOSE_MS, NULL);
    show_alert();
    ESP_LOGI(TAG, "opened at %d of %d", s_card.index + 1, alerts->alert_count);
}

/**
 * @brief Close the card if it is open.
 * @version 1
 * @req REQ-ALERT-002
 */
void alert_card_close(void)
{
    if (!s_card.backdrop) return;
    lv_timer_delete(s_card.timer);
    lv_obj_delete(s_card.backdrop);
    lv_free(s_card.alerts);
    memset(&s_card, 0, sizeof(s_card));
    ESP_LOGI(TAG, "closed");
}

/**
 * @brief Whether the card is open.
 * @version 1
 * @req REQ-ALERT-002
 * @return true while it shows.
 */
bool alert_card_is_open(void)
{
    return s_card.backdrop != NULL;
}
