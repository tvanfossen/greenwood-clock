/**
 * @file settings_pickers.c
 * v2: the brightness picker has no Global (profiles set it).
 * @version 2
 * @brief The schedule editor's pickers (see settings_pickers.h): rollers in a settings frame,
 *        Done reports and closes.
 */
#include "settings_pickers.h"
#include "settings_app.h"
#include "settings_frame.h"
#include "ui_theme_bind.h"

#include <stdio.h>
#include <string.h>

#define ROLLER_ROWS  3
#define OFFSET_STEP  15
#define OFFSET_MAX   180
#define OFFSETS      (2 * OFFSET_MAX / OFFSET_STEP + 1)
#define YEARS        6

static const char MONTHS[] = "Jan\nFeb\nMar\nApr\nMay\nJun\nJul\nAug\nSep\nOct\nNov\nDec";

/**
 * @brief A row of rollers in @p col (flex, centred).
 * v2: wraps (narrow frames); the frame area's width.
 * @version 2
 * @utility
 * @return The row.
 */
static lv_obj_t *roller_row(lv_obj_t *col)
{
    lv_obj_t *row = lv_obj_create(col);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, settings_content_w() - 12, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW_WRAP);   // a narrow frame wraps the year
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 12, 0);
    lv_obj_set_style_pad_row(row, 8, 0);
    return row;
}

/**
 * @brief A roller of @p opts in @p row, @p width wide, at @p sel.
 * @version 1
 * @utility
 * @return The roller.
 */
static lv_obj_t *roller(lv_obj_t *row, const char *opts, int32_t width, int sel)
{
    lv_obj_t *r = lv_roller_create(row);
    lv_roller_set_options(r, opts, LV_ROLLER_MODE_NORMAL);
    lv_obj_set_style_text_font(r, &lv_font_montserrat_24, 0);   // before sizing: rows fit it
    lv_roller_set_visible_row_count(r, ROLLER_ROWS);
    lv_obj_set_width(r, width);
    lv_roller_set_selected(r, (uint32_t)sel, LV_ANIM_OFF);
    return r;
}

/**
 * @brief "1\n2\n...\n@p n" (or zero-padded "00\n05..." with @p step and @p pad) into @p out.
 * @version 1
 * @utility
 */
static void number_options(char *out, size_t n, int first, int count, int step, bool pad)
{
    size_t at = 0;
    for (int i = 0; i < count && at < n; i++) {
        int v = first + i * step;
        at += (size_t)snprintf(out + at, n - at, pad ? "%s%02d" : "%s%d", i ? "\n" : "", v);
    }
}

// ---------------------------------------------------------------------------
// Time
// ---------------------------------------------------------------------------

typedef struct {
    const char        *title;
    sched_time_t       t;
    settings_time_cb_t cb;
    void              *ctx;
    lv_obj_t          *hour, *minute, *ampm, *offset;
} time_picker_t;

static time_picker_t s_time;

/**
 * @brief The offset roller's label for @p off minutes from @p sun ("sunrise"/"sunset").
 * @version 1
 * @utility
 */
static void offset_label(int off, const char *sun, char *out, size_t n)
{
    int a = off < 0 ? -off : off;
    const char *side = off < 0 ? "before" : "after";
    if (off == 0) snprintf(out, n, "At %s", sun);
    else if (a % 60 == 0) snprintf(out, n, "%dh %s %s", a / 60, side, sun);
    else if (a < 60) snprintf(out, n, "%dm %s %s", a, side, sun);
    else snprintf(out, n, "%dh %dm %s %s", a / 60, a % 60, side, sun);
}

/**
 * @brief Read the rollers into s_time.t (keeping its anchor).
 * @version 1
 * @utility
 */
static void time_read(void)
{
    sched_time_t *t = &s_time.t;
    if (t->anchor == SCHED_AT_CLOCK && s_time.hour) {
        int h = (int)lv_roller_get_selected(s_time.hour) + 1;   // 1..12
        int pm = (int)lv_roller_get_selected(s_time.ampm);
        t->min = (int16_t)(((h % 12) + 12 * pm) * 60 + 5 * (int)lv_roller_get_selected(s_time.minute));
    } else if (t->anchor != SCHED_AT_CLOCK && s_time.offset) {
        t->min = (int16_t)((int)lv_roller_get_selected(s_time.offset) * OFFSET_STEP - OFFSET_MAX);
    }
}

/**
 * @brief The anchor tapped (clock, sunrise, sunset): switch and rebuild.
 * @version 1
 * @callback
 */
static void anchor_cb(lv_event_t *e)
{
    sched_anchor_t a = (sched_anchor_t)(uintptr_t)lv_event_get_user_data(e);
    time_read();
    if (a != s_time.t.anchor) s_time.t.min = a == SCHED_AT_CLOCK ? 12 * 60 : 0;
    s_time.t.anchor = a;
    settings_app_refresh();
}

/**
 * @brief Done: report the time and close.
 * @version 1
 * @callback
 */
static void time_done_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    time_read();
    sched_time_t t = s_time.t;
    settings_app_pop();
    if (s_time.cb) s_time.cb(t, s_time.ctx);
}

/**
 * @brief The clock-time rollers: hour, minute (5-minute steps), AM/PM.
 * @version 1
 * @utility
 */
static void clock_rollers(lv_obj_t *col)
{
    static char hours[48], minutes[48];
    number_options(hours, sizeof(hours), 1, 12, 1, false);
    number_options(minutes, sizeof(minutes), 0, 12, 5, true);
    int h = s_time.t.min / 60, m = s_time.t.min % 60;
    lv_obj_t *row = roller_row(col);
    s_time.hour = roller(row, hours, 90, (h + 11) % 12);
    s_time.minute = roller(row, minutes, 90, m / 5);
    s_time.ampm = roller(row, "AM\nPM", 90, h >= 12);
}

/**
 * @brief The sun-offset roller.
 * v2: the roller fits the frame width.
 * @version 2
 * @utility
 */
static void offset_roller(lv_obj_t *col)
{
    static char opts[OFFSETS * 32];
    const char *sun = s_time.t.anchor == SCHED_AT_SUNRISE ? "sunrise" : "sunset";
    size_t at = 0;
    for (int i = 0; i < OFFSETS; i++) {
        char l[32];
        offset_label(i * OFFSET_STEP - OFFSET_MAX, sun, l, sizeof(l));
        at += (size_t)snprintf(opts + at, sizeof(opts) - at, "%s%s", i ? "\n" : "", l);
    }
    int sel = (s_time.t.min + OFFSET_MAX) / OFFSET_STEP;
    s_time.offset = roller(roller_row(col), opts, LV_MIN(400, settings_content_w() - 24), sel < 0 ? 0 : (sel >= OFFSETS ? OFFSETS - 1 : sel));
}

/**
 * @brief The time picker screen.
 * @version 1
 * @utility
 * @return The frame.
 */
static lv_obj_t *time_screen(lv_obj_t *parent, void *ctx)
{
    LV_UNUSED(ctx);
    static const char *const ANCHORS[] = { "Clock time", "Sunrise", "Sunset" };
    lv_obj_t *frame = settings_frame_create(parent, s_time.title, settings_app_back_cb);
    settings_title_button(frame, LV_SYMBOL_OK " Done", time_done_cb, NULL);
    lv_obj_t *col = settings_column(frame);
    settings_segmented(settings_field(col, "From"), ANCHORS, 3, (int)s_time.t.anchor, anchor_cb, 0);
    s_time.hour = s_time.minute = s_time.ampm = s_time.offset = NULL;
    if (s_time.t.anchor == SCHED_AT_CLOCK) clock_rollers(col);
    else offset_roller(col);
    return frame;
}

/**
 * @brief Push a time picker.
 * @version 1
 * @req REQ-BG-004
 */
void settings_pick_time(const char *title, sched_time_t t, settings_time_cb_t cb, void *ctx)
{
    s_time = (time_picker_t){ title, t, cb, ctx, NULL, NULL, NULL, NULL };
    if (t.anchor == SCHED_AT_CLOCK) s_time.t.min = (int16_t)(t.min - t.min % 5);
    settings_app_push(time_screen, NULL);
}

// ---------------------------------------------------------------------------
// Dates
// ---------------------------------------------------------------------------

typedef struct {
    sched_date_t        from, to;
    bool                with_year;
    settings_dates_cb_t cb;
    void               *ctx;
    lv_obj_t           *roll[2][3];   // [from/to][month, day, year]
    int                 base_year;
} dates_picker_t;

static dates_picker_t s_dates;

/**
 * @brief Read one date's rollers (the day clamped to its month).
 * @version 1
 * @utility
 * @return The date.
 */
static sched_date_t date_read(int which)
{
    static const uint8_t DAYS[12] = { 31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    lv_obj_t **r = s_dates.roll[which];
    int m = (int)lv_roller_get_selected(r[0]) + 1;
    int d = (int)lv_roller_get_selected(r[1]) + 1;
    int y = r[2] ? s_dates.base_year + (int)lv_roller_get_selected(r[2]) : 0;
    if (d > DAYS[m - 1]) d = DAYS[m - 1];
    return (sched_date_t){ (uint16_t)y, (uint8_t)m, (uint8_t)d };
}

/**
 * @brief Done: report the range and close.
 * @version 1
 * @callback
 */
static void dates_done_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    sched_date_t from = date_read(0), to = date_read(1);
    settings_app_pop();
    if (s_dates.cb) s_dates.cb(from, to, s_dates.ctx);
}

/**
 * @brief One date's rollers under @p label.
 * @version 1
 * @utility
 */
static void date_rollers(lv_obj_t *col, const char *label, int which, sched_date_t d)
{
    static char days[128], years[48];
    number_options(days, sizeof(days), 1, 31, 1, false);
    number_options(years, sizeof(years), s_dates.base_year, YEARS, 1, false);
    settings_text(col, label, &lv_font_montserrat_20, UI_ROLE_TEXT_MUTED);
    lv_obj_t *row = roller_row(col);
    s_dates.roll[which][0] = roller(row, MONTHS, 110, d.month - 1);
    s_dates.roll[which][1] = roller(row, days, 90, d.day - 1);
    s_dates.roll[which][2] = s_dates.with_year ? roller(row, years, 120, d.year - s_dates.base_year) : NULL;
}

/**
 * @brief The date range picker screen.
 * @version 1
 * @utility
 * @return The frame.
 */
static lv_obj_t *dates_screen(lv_obj_t *parent, void *ctx)
{
    LV_UNUSED(ctx);
    lv_obj_t *frame = settings_frame_create(parent, "Dates", settings_app_back_cb);
    settings_title_button(frame, LV_SYMBOL_OK " Done", dates_done_cb, NULL);
    lv_obj_t *col = settings_column(frame);
    date_rollers(col, "First day", 0, s_dates.from);
    date_rollers(col, "Last day", 1, s_dates.to);
    return frame;
}

/**
 * @brief Push a date range picker.
 * @version 1
 * @req REQ-BG-004
 */
void settings_pick_dates(sched_date_t from, sched_date_t to, bool with_year,
                         settings_dates_cb_t cb, void *ctx)
{
    memset(&s_dates, 0, sizeof(s_dates));
    s_dates.from = from;
    s_dates.to = to;
    s_dates.with_year = with_year;
    s_dates.cb = cb;
    s_dates.ctx = ctx;
    s_dates.base_year = from.year ? from.year : 2026;
    settings_app_push(dates_screen, NULL);
}

// ---------------------------------------------------------------------------
// Brightness
// ---------------------------------------------------------------------------

typedef struct {
    int                   pct;
    settings_percent_cb_t cb;
    void                 *ctx;
    lv_obj_t             *slider;
    lv_obj_t             *label;
} percent_picker_t;

static percent_picker_t s_pct;

/**
 * @brief The slider moved: show its value.
 * @version 1
 * @callback
 */
static void pct_moved_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    lv_label_set_text_fmt(s_pct.label, "%d %%", (int)lv_slider_get_value(s_pct.slider));
}

/**
 * @brief Done: report and close.
 * v2: always a value (no Global).
 * @version 2
 * @callback
 */
static void pct_done_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    int pct = (int)lv_slider_get_value(s_pct.slider);
    settings_app_pop();
    if (s_pct.cb) s_pct.cb(pct, s_pct.ctx);
}

/**
 * @brief The brightness picker screen: a slider and its value.
 * v2: no Global: profiles set their brightness.
 * v3: the slider fits the frame width.
 * @version 3
 * @utility
 * @return The frame.
 */
static lv_obj_t *pct_screen(lv_obj_t *parent, void *ctx)
{
    LV_UNUSED(ctx);
    int pct = s_pct.pct >= 5 && s_pct.pct <= 100 ? s_pct.pct : 60;
    lv_obj_t *frame = settings_frame_create(parent, "Brightness", settings_app_back_cb);
    settings_title_button(frame, LV_SYMBOL_OK " Done", pct_done_cb, NULL);
    lv_obj_t *col = settings_column(frame);
    lv_obj_t *row = settings_field(col, "Brightness");
    s_pct.slider = lv_slider_create(row);
    lv_obj_set_width(s_pct.slider, LV_MIN(420, settings_content_w() - 110));
    lv_slider_set_range(s_pct.slider, 5, 100);
    lv_slider_set_value(s_pct.slider, pct, LV_ANIM_OFF);
    lv_obj_add_event_cb(s_pct.slider, pct_moved_cb, LV_EVENT_VALUE_CHANGED, NULL);
    s_pct.label = settings_text(row, "", &lv_font_montserrat_24, UI_ROLE_TEXT);
    pct_moved_cb(NULL);
    return frame;
}

/**
 * @brief Push a brightness picker.
 * v2: no Global.
 * @version 2
 * @req REQ-BG-004
 */
void settings_pick_percent(int pct, settings_percent_cb_t cb, void *ctx)
{
    s_pct = (percent_picker_t){ pct, cb, ctx, NULL, NULL };
    settings_app_push(pct_screen, NULL);
}
