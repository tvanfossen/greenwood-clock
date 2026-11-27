/**
 * @file clock_widget.c
 * @version 1
 * @brief ClockWidget: time/AM-PM/date labels with full, minimized and topbar modes and a snapshot morph between them.
 */
// Layer 1: ClockWidget — reusable time/date display with full/minimized modes.
// Extracted from components/ui/ui.c clock label management.
//
// Caller must hold LVGL lock for all functions.

#include "display_widgets.h"
#include "ui_theme.h"
#include "refresh_probe.h"
#include "text_roll.h"
#include "src/misc/lv_area_private.h"  /* lv_area_intersect / lv_area_join (private API) */
#include "esp_log.h"
#include "esp_timer.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "clock_widget";

LV_FONT_DECLARE(nunito_48);
LV_FONT_DECLARE(nunito_128);
LV_FONT_DECLARE(nunito_256);
// 8 bpp static-bitmap time digits (inv gen-fonts): blended straight from flash, anti-aliased;
// the 1 bpp fonts were re-expanded per glyph for every small dirty area (snowfall).
LV_FONT_DECLARE(nunito_clock_256);
LV_FONT_DECLARE(nunito_clock_128);
LV_FONT_DECLARE(nunito_clock_48);

// ---------------------------------------------------------------------------
// Internal struct
// ---------------------------------------------------------------------------

struct clock_widget_t {
    lv_obj_t    *container;     // invisible container for grouping
    lv_obj_t    *lbl_time;      // HH:MM
    lv_obj_t    *lbl_ampm;      // AM/PM
    lv_obj_t    *lbl_date;      // Day, Month DD
    // Drop shadows: one sibling label behind each text label (linked via the text label's
    // user_data), mirroring its text/font/position/visibility, in a colour contrasting with
    // the text. They keep the user's exact text colour legible over any background.
    lv_obj_t    *sh_time;
    lv_obj_t    *sh_ampm;
    lv_obj_t    *sh_date;
    clock_mode_t mode;
    lv_color_t   user_color;  // raw NVS text colour — the OKLCH base for all modes
    lv_color_t   color;       // FULL-mode colour (legible over the bg image)
    lv_color_t   min_color;   // MINIMIZED-mode colour (current; states may override)
    lv_color_t   bar_color;   // TOPBAR-mode colour (legible over the strip it sits on)
    lv_color_t   min_base;    // default MINIMIZED colour (legible over the bg image,
                              // used by states that show the clock bg behind the clock)
    struct tm    live;        // last live time from clock_widget_update
    bool         live_valid;
    bool         held;        // showing a held time (clock_widget_hold): live ticks not shown
    struct tm    hold_t;      // the held time
    time_t       shown;       // the time on the labels (roll direction)
};

static void show_time(clock_widget_t *w, const struct tm *ti);
static time_t minute_key(const struct tm *t);

// Run once the clock is at rest (morph finished or none running); see clock_widget_when_settled.
static void (*s_settled_cb)(void);
static clock_widget_t *s_morph_w;      // widget being morphed (NULL when no morph runs)
static clock_mode_t    s_morph_from;   // layout on screen during the morph (the source)

#define ROLL_MS     500         // held-time change (radar loop: mostly after its crossfade)
#define SHADOW_OPA  LV_OPA_60   // shadow text opacity (on top of the label's own opa)

/**
 * @brief Create a shadow label in @p parent (call BEFORE the text label so it renders behind).
 * @version 1
 * @req REQ-BG-002
 * @return The shadow label.
 */
static lv_obj_t *shadow_create(lv_obj_t *parent)
{
    lv_obj_t *sh = lv_label_create(parent);
    lv_obj_set_style_text_opa(sh, SHADOW_OPA, 0);
    lv_obj_remove_flag(sh, LV_OBJ_FLAG_CLICKABLE);
    lv_label_set_text(sh, "");
    return sh;
}

/**
 * @brief Mirror a text label onto its linked shadow: text, font, alignment, visibility and
 *        opacity, offset down-right by ~1/64 of the font's line height (min 2 px).
 *        Requires the label's layout to be up to date.
 * @version 1
 * @req REQ-BG-002
 */
static void shadow_sync(lv_obj_t *lbl)
{
    lv_obj_t *sh = (lv_obj_t *)lv_obj_get_user_data(lbl);
    if (!sh) return;
    const lv_font_t *font = lv_obj_get_style_text_font(lbl, 0);
    lv_label_set_text(sh, lv_label_get_text(lbl));
    lv_obj_set_style_text_font(sh, font, 0);
    lv_obj_set_style_text_align(sh, lv_obj_get_style_text_align(lbl, 0), 0);
    lv_obj_set_style_opa(sh, lv_obj_get_style_opa(lbl, 0), 0);
    int32_t off = LV_MAX(2, lv_font_get_line_height(font) / 64);
    lv_obj_align_to(sh, lbl, LV_ALIGN_TOP_LEFT, off, off);
    if (lv_obj_has_flag(lbl, LV_OBJ_FLAG_HIDDEN)) lv_obj_add_flag(sh, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_remove_flag(sh, LV_OBJ_FLAG_HIDDEN);
}

// ---------------------------------------------------------------------------
// Position constants for full / minimized modes
// ---------------------------------------------------------------------------

// The alert banner is at the bottom of the screen (LAYOUT_BOTTOM_CLEAR), so every mode
// starts at the top.
//
// Full mode:      1008×350 at (8, 8)
// Minimized:      420×180  at (588, 8)
// Topbar:         1008×56  at (8, 4)
#define FULL_X  8
#define FULL_Y  8
#define FULL_W  1008
#define FULL_H  350
#define MIN_W   420
#define MIN_X   (1024 - MIN_W - 16)  // right-aligned with 16px margin
#define MIN_Y   8
#define MIN_H   180
#define BAR_X   8
#define BAR_Y   4
#define BAR_W   1008
#define BAR_H   56

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/**
 * @brief Format @p ti as 12-hour "H:MM", "AM"/"PM" and a "Weekday, Month, DD" date string.
 * v2: no leading zero on the hour ("9:05", not "09:05"). The time label is auto-width and
 *     centre-aligned, and the morph and AM/PM placement are centre/edge relative, so the
 *     narrower text stays centred.
 * @version 2
 * @req REQ-CLOCK-001
 */
static void format_time(const struct tm *ti, char *buf_time, size_t time_sz,
                        char *buf_ampm, size_t ampm_sz,
                        char *buf_date, size_t date_sz)
{
    int h12 = ti->tm_hour % 12;
    if (!h12) h12 = 12;
    snprintf(buf_time, time_sz, "%d:%02d", h12, ti->tm_min);
    snprintf(buf_ampm, ampm_sz, "%s", ti->tm_hour < 12 ? "AM" : "PM");
    strftime(buf_date, date_sz, "%A, %B, %d", ti);
}

/**
 * @brief Place AM/PM beside the time for layout @p mode (baseline-aligned in FULL/MINIMIZED).
 * @version 1
 * @utility
 */
static void align_ampm(clock_widget_t *w, clock_mode_t mode)
{
    switch (mode) {
        case CLOCK_MODE_FULL:
            lv_obj_align_to(w->lbl_ampm, w->lbl_time, LV_ALIGN_OUT_RIGHT_BOTTOM, 8, -43);
            break;
        case CLOCK_MODE_TOPBAR:
            lv_obj_align_to(w->lbl_ampm, w->lbl_time, LV_ALIGN_OUT_RIGHT_MID, 6, 0);
            break;
        case CLOCK_MODE_MINIMIZED:
        default:
            lv_obj_align_to(w->lbl_ampm, w->lbl_time, LV_ALIGN_OUT_RIGHT_BOTTOM, 8, -17);
            break;
    }
}

// Apply fonts and inner label alignment for full mode.
// Does NOT touch container position/size — that's handled by animation or snap_to().
/**
 * @brief Full-mode layout: 256pt time, 48pt AM/PM baseline-aligned to it, 48pt date at the bottom; does not move the container.
 * v2: time uses the 8 bpp static-bitmap digit font (same metrics).
 * v3: AM/PM and date use the 8 bpp static-bitmap 48 pt font (same metrics).
 * v4: AM/PM placed by align_ampm.
 * @version 4
 * @req REQ-CLOCK-001
 */
static void apply_full_layout(clock_widget_t *w)
{
    // Time: 256pt, centered in container
    lv_obj_set_style_text_font(w->lbl_time, &nunito_clock_256, 0);
    lv_obj_align(w->lbl_time, LV_ALIGN_TOP_MID, 0, 24);

    // AM/PM: 48pt, visual text-bottom aligned with time.
    // y_ofs = -(nunito_256.base_line - nunito_48.base_line) = -(53 - 10) = -43
    // Verified: LV_ALIGN_OUT_RIGHT_BOTTOM sets y = h(base)-h(obj)+y_ofs = 265-50-43 = 172
    //   time baseline  = 265 - 53 = 212
    //   ampm baseline  = 172 + 50 - 10 = 212  (same pixel row)
    lv_obj_set_style_text_font(w->lbl_ampm, &nunito_clock_48, 0);
    align_ampm(w, CLOCK_MODE_FULL);

    // Date: 48pt, bottom center
    lv_obj_set_style_text_font(w->lbl_date, &nunito_clock_48, 0);
    lv_obj_align(w->lbl_date, LV_ALIGN_BOTTOM_MID, 0, -16);

    lv_obj_clear_flag(w->container, LV_OBJ_FLAG_HIDDEN);
}

// Apply fonts and inner label alignment for minimized mode.
// Does NOT touch container position/size — that's handled by animation or snap_to().
/**
 * @brief Minimized-mode layout: 128pt time shifted left so time + 48pt AM/PM are centred, 24pt date below; does not move the container.
 * v2: time uses the 8 bpp static-bitmap digit font (same metrics).
 * v3: AM/PM uses the 8 bpp static-bitmap 48 pt font (same metrics).
 * v4: AM/PM placed by align_ampm.
 * @version 4
 * @req REQ-CLOCK-002
 */
static void apply_minimized_layout(clock_widget_t *w)
{
    // Time: 128pt. Shift left so the time + AM/PM *group* is centred in the
    // container — otherwise centring the time alone pushes the 48pt AM/PM past
    // the right screen edge (only "P"/"A" visible).
    lv_obj_set_style_text_font(w->lbl_time, &nunito_clock_128, 0);
    lv_obj_align(w->lbl_time, LV_ALIGN_TOP_MID, -40, 0);

    // AM/PM: 48pt, visual text-bottom aligned with time.
    // y_ofs = -(nunito_128.base_line - nunito_48.base_line) = -(27 - 10) = -17
    // Verified: y = h(base)-h(obj)+y_ofs = 133-50-17 = 66
    //   time baseline  = 133 - 27 = 106
    //   ampm baseline  = 66 + 50 - 10 = 106  (same pixel row)
    lv_obj_set_style_text_font(w->lbl_ampm, &nunito_clock_48, 0);
    align_ampm(w, CLOCK_MODE_MINIMIZED);

    // Date: 24pt, below time
    lv_obj_set_style_text_font(w->lbl_date, &lv_font_montserrat_24, 0);
    lv_obj_align(w->lbl_date, LV_ALIGN_BOTTOM_MID, 0, -4);

    lv_obj_clear_flag(w->container, LV_OBJ_FLAG_HIDDEN);
}

// Apply fonts and inner label alignment for topbar mode (thin strip, settings use).
// Does NOT touch container position/size — that's handled by animation or snap_to().
/**
 * @brief Topbar-mode layout: 48pt time left, 24pt AM/PM beside it, 24pt date right-aligned; does not move the container.
 * v2: AM/PM placed by align_ampm.
 * @version 2
 * @req REQ-CLOCK-002
 */
static void apply_topbar_layout(clock_widget_t *w)
{
    // Time: 48pt, left-aligned
    lv_obj_set_style_text_font(w->lbl_time, &nunito_48, 0);
    lv_obj_align(w->lbl_time, LV_ALIGN_LEFT_MID, 8, 0);

    // AM/PM: 24pt, right of time
    lv_obj_set_style_text_font(w->lbl_ampm, &lv_font_montserrat_24, 0);
    align_ampm(w, CLOCK_MODE_TOPBAR);

    // Date: 24pt, right-aligned
    lv_obj_set_style_text_font(w->lbl_date, &lv_font_montserrat_24, 0);
    lv_obj_align(w->lbl_date, LV_ALIGN_RIGHT_MID, -8, 0);

    lv_obj_clear_flag(w->container, LV_OBJ_FLAG_HIDDEN);
}

static void mode_geometry(clock_mode_t mode, int32_t *x, int32_t *y, int32_t *w, int32_t *h);
static void apply_effective_color(clock_widget_t *w);
static void kill_fade(void);
static void finalize_morph(bool crossfade);
static void snap_cache_release(void);

/**
 * @brief Re-sync all three shadows with their labels (updates layout first so positions are current).
 * @version 1
 * @req REQ-BG-002
 */
static void shadows_sync(clock_widget_t *w)
{
    lv_obj_update_layout(w->container);
    shadow_sync(w->lbl_time);
    shadow_sync(w->lbl_ampm);
    shadow_sync(w->lbl_date);
}

/**
 * @brief Write the screen area occupied by the full-mode clock into @p out (no-op if NULL).
 * @version 1
 * @req REQ-CLOCK-001
 * @req REQ-BG-002
 */
void clock_widget_full_area(lv_area_t *out)
{
    if (!out) return;
    out->x1 = FULL_X;
    out->y1 = FULL_Y;
    out->x2 = FULL_X + FULL_W - 1;
    out->y2 = FULL_Y + FULL_H - 1;
}

// Snap container to a mode's target position/size immediately (no animation).
/**
 * @brief Move and resize the clock container to @p mode's geometry immediately, without animation.
 * @version 1
 * @utility
 */
static void snap_to(clock_widget_t *w, clock_mode_t mode)
{
    int32_t x, y, mw, mh;
    mode_geometry(mode, &x, &y, &mw, &mh);
    lv_obj_set_pos(w->container, x, y);
    lv_obj_set_size(w->container, mw, mh);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

/**
 * @brief Allocate a clock widget on @p parent: transparent container with white time, AM/PM and date labels in full mode.
 * v2: creates a drop-shadow label behind each text label.
 * v3: colours from the theme roles (ui_theme), no literals.
 * v4: a top-bar colour of its own.
 * @version 4
 * @req REQ-CLOCK-001
 * @return New widget, or NULL if allocation fails.
 */
clock_widget_t *clock_widget_create(lv_obj_t *parent)
{
    clock_widget_t *w = lv_malloc(sizeof(clock_widget_t));
    if (!w) {
        ESP_LOGE(TAG, "Failed to allocate clock_widget_t");
        return NULL;
    }
    memset(w, 0, sizeof(*w));
    w->user_color = ui_theme_color(UI_ROLE_TEXT);
    w->color      = w->user_color;
    w->min_color  = w->user_color;   // until the backdrop is sampled
    w->min_base   = w->user_color;
    w->bar_color  = w->user_color;
    w->mode       = CLOCK_MODE_FULL;

    // Transparent container — groups labels for easy repositioning.
    // OVERFLOW_VISIBLE: AM/PM is positioned LV_ALIGN_OUT_RIGHT_BOTTOM past
    // the time label's right edge; in MINIMIZED mode that pushes it just
    // past the container's right edge. Without overflow-visible the "M"
    // gets clipped, leaving only "P".
    w->container = lv_obj_create(parent);
    lv_obj_set_style_bg_opa(w->container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(w->container, 0, 0);
    lv_obj_set_style_pad_all(w->container, 8, 0);
    lv_obj_clear_flag(w->container, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(w->container, LV_OBJ_FLAG_OVERFLOW_VISIBLE);

    // Time label (shadow first so it renders behind)
    w->sh_time  = shadow_create(w->container);
    w->lbl_time = lv_label_create(w->container);
    lv_obj_set_user_data(w->lbl_time, w->sh_time);
    lv_obj_set_style_text_color(w->lbl_time, w->color, 0);
    lv_obj_set_style_text_align(w->lbl_time, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(w->lbl_time, "12:00");

    // AM/PM label
    w->sh_ampm  = shadow_create(w->container);
    w->lbl_ampm = lv_label_create(w->container);
    lv_obj_set_user_data(w->lbl_ampm, w->sh_ampm);
    lv_obj_set_style_text_color(w->lbl_ampm, w->color, 0);
    lv_label_set_text(w->lbl_ampm, "PM");

    // Date label
    w->sh_date  = shadow_create(w->container);
    w->lbl_date = lv_label_create(w->container);
    lv_obj_set_user_data(w->lbl_date, w->sh_date);
    lv_obj_set_style_text_color(w->lbl_date, w->color, 0);
    lv_obj_set_style_text_align(w->lbl_date, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(w->lbl_date, "");

    apply_full_layout(w);
    snap_to(w, CLOCK_MODE_FULL);
    apply_effective_color(w);
    shadows_sync(w);
    ESP_LOGI(TAG, "ClockWidget created");
    return w;
}

/**
 * @brief Cancel any in-flight crossfade, delete the widget's LVGL objects and free it (no-op if NULL).
 * v2: also settles any morph and frees the snapshot cache.
 * v3: drops a pending settled callback first (it must not run during teardown).
 * v4: finishes a time roll first (it animates the labels being deleted).
 * @version 4
 * @req REQ-CLOCK-001
 */
void clock_widget_destroy(clock_widget_t *w)
{
    if (!w) return;
    s_settled_cb = NULL;
    text_roll_finish();   // its objects are about to go
    kill_fade();   // drop any in-flight crossfade snapshot before teardown
    finalize_morph(false);
    snap_cache_release();
    if (w->container) {
        lv_anim_delete(w->container, NULL);
        lv_obj_delete(w->container);
    }
    lv_free(w);
    ESP_LOGI(TAG, "ClockWidget destroyed");
}

/**
 * @brief Set the time, AM/PM and date labels from @p ti and re-align AM/PM for the current mode.
 * v2: re-syncs the label shadows after the text changes.
 * v3: during a morph, aligns AM/PM for the layout on screen (the morph's source), not the
 *     target mode: a tick mid-morph moved AM/PM by the target's offset (flicker).
 * v4: records the live time and shows it unless a held time is shown (clock_widget_hold).
 * v5: a time roll still running is finished first (it may be rolling to the old live time).
 * @version 5
 * @req REQ-CLOCK-001
 */
void clock_widget_update(clock_widget_t *w, const struct tm *ti)
{
    if (!w || !ti) return;
    w->live = *ti;
    w->live_valid = true;
    if (w->held) return;
    text_roll_finish();
    show_time(w, ti);
}

/**
 * @brief @p t as a comparable minute: seconds dropped, DST resolved by mktime (live ticks
 *        carry tm_isdst 0, frame times from localtime_r the real flag).
 * @version 1
 * @utility
 * @return Epoch seconds of the minute.
 */
static time_t minute_key(const struct tm *t)
{
    struct tm k = *t;
    k.tm_sec = 0;
    k.tm_isdst = -1;
    return mktime(&k);
}

/**
 * @brief text_roll swap callback: show the time the clock should now show (held or live).
 * @version 1
 * @callback
 */
static void roll_swap_cb(void *user)
{
    clock_widget_t *w = (clock_widget_t *)user;
    show_time(w, w->held ? &w->hold_t : &w->live);
}

/**
 * @brief Change the shown time to the held (or live) one with a roll starting after
 *        @p delay_ms: up when it is later, down when earlier (the radar loop wrapping to its
 *        oldest frame). Instant mid-morph (the morph owns the labels) or when the minute does
 *        not change.
 * @version 1
 * @utility
 */
static void roll_to(clock_widget_t *w, uint32_t delay_ms)
{
    time_t to = minute_key(w->held ? &w->hold_t : &w->live);
    if (s_morph_w || to == w->shown) {
        text_roll_finish();
        roll_swap_cb(w);
        return;
    }
    lv_obj_t *const objs[] = { w->lbl_time, w->sh_time, w->lbl_ampm, w->sh_ampm };
    text_roll_start(objs, 4, to < w->shown, delay_ms, ROLL_MS, roll_swap_cb, w);
}

/**
 * @brief Show @p t until clock_widget_release; live updates are kept but not shown meanwhile.
 * v2: rolls the time to @p t (see roll_to), starting after @p delay_ms.
 * @version 2
 * @req REQ-RADAR-003
 */
void clock_widget_hold(clock_widget_t *w, const struct tm *t, uint32_t delay_ms)
{
    if (!w || !t) return;
    w->held = true;
    w->hold_t = *t;
    roll_to(w, delay_ms);
}

/**
 * @brief End a hold: show the latest live time again.
 * v2: rolls the time back to the live one (see roll_to), starting after @p delay_ms.
 * @version 2
 * @req REQ-RADAR-003
 */
void clock_widget_release(clock_widget_t *w, uint32_t delay_ms)
{
    if (!w || !w->held) return;
    w->held = false;
    if (w->live_valid) roll_to(w, delay_ms);
}

/**
 * @brief Set the labels to @p ti (time, AM/PM, date) for the layout on screen.
 * v2 of clock_widget_update's body (split out so a held time can be shown).
 * v3: records the time shown (the roll's direction).
 * @version 3
 * @utility
 */
static void show_time(clock_widget_t *w, const struct tm *ti)
{
    w->shown = minute_key(ti);
    char buf_time[6], buf_ampm[3], buf_date[30];
    format_time(ti, buf_time, sizeof(buf_time),
                buf_ampm, sizeof(buf_ampm),
                buf_date, sizeof(buf_date));

    lv_label_set_text(w->lbl_time, buf_time);
    lv_label_set_text(w->lbl_ampm, buf_ampm);
    lv_label_set_text(w->lbl_date, buf_date);

    // Re-align AM/PM after time text change (width varies by digit glyphs), for the layout
    // actually on screen: mid-morph that is still the source mode's.
    align_ampm(w, s_morph_w ? s_morph_from : w->mode);
    shadows_sync(w);
}

// Snapshot morph. LVGL re-rasterizes a transform layer every frame, so scaling
// the live 256pt clock was jagged. Instead we rasterize the full clock to an
// image ONCE, then animate a scaled lv_image of that static bitmap — the source
// pixels never change, so each frame is just a (PPA-accelerated) scale-blit, no
// re-raster. Crisp (high-res snapshot, only ever downscaled) AND smooth.
static lv_obj_t      *s_morph_img;     // temporary scaled image of the snapshot
static lv_draw_buf_t *s_morph_snap;    // the snapshot buffer (freed at completion)
static clock_mode_t    s_morph_target;
static int32_t s_mx0, s_my0, s_msc0;   // source pos + scale (256 = 1.0x)
static int32_t s_mx1, s_my1, s_msc1;   // target pos + scale
static lv_color_t s_mc0;               // source text colour (the snapshot is rendered in it)
static lv_area_t  s_morph_time_rel;    // time label's area inside this morph's snapshot
static lv_point_t s_morph_origin;      // this morph's snapshot crop origin in the container
// Snapshot cache: a FULL-layout time snapshot depends only on the time text and its colour,
// so it is reused across morphs until the minute changes (rendering it is the bulk of a
// morph's LVGL-lock time). Two slots: a minimise and the following restore start from
// different colours (FULL colour vs minimized colour). The cache OWNS the buffers;
// morph/crossfade free a snapshot only once it is no longer cached.
#define SNAP_SLOTS 2
typedef struct {
    lv_draw_buf_t *buf;
    char           text[8];
    lv_color_t     color;
    lv_area_t      time_rel;   // time label's area inside the snapshot
    lv_point_t     origin;     // snapshot (crop) origin in the container
} snap_slot_t;
static snap_slot_t s_snap_cache[SNAP_SLOTS];
static int         s_snap_last;   // slot used most recently (the other one is evicted)

/**
 * @brief True if @p buf is held by a snapshot cache slot (the cache then owns and frees it).
 * @version 1
 * @utility
 * @return Whether the buffer is cached.
 */
static bool snap_is_cached(const lv_draw_buf_t *buf)
{
    bool cached = false;
    for (int i = 0; i < SNAP_SLOTS; i++) cached |= (s_snap_cache[i].buf == buf);
    return cached;
}
// Morph measurement: refresh rate/gaps during the morph and the handoff geometry error.
static refresh_probe_t s_morph_probe;
static bool            s_morph_probe_attached;
static int64_t         s_morph_t0_us;

/** The PPA scales exactly only in 1/16 steps (PPA_LL_SRM_SCALING_FRAG_MAX); other scales go
 *  to the software renderer, ~37 ms per morph frame. */
#define MORPH_SCALE_STEP 16

/**
 * @brief lv_anim exec callback: place and scale the morph snapshot at progress @p prog (0 source .. 256 target).
 * v2: no per-frame recolour. PPA declines recoloured images, so the recolour forced a software
 *     scale + recolour of the whole 1008x350 snapshot every frame: measured 6.5 Hz, ~140 ms
 *     render per frame. The snapshot is now rendered in the source colour; the colour change
 *     happens in the end crossfade.
 * v3: between the endpoints the scale snaps to MORPH_SCALE_STEP so the PPA scales it; the
 *     time's centre still follows the unsnapped path, so only its size steps (6% a step,
 *     finer than the ~6-10 frames a morph gets).
 * @version 3
 * @callback
 */
static void morph_exec_cb(void *var, int32_t prog)   // prog: 0 (source) .. 256 (target)
{
    lv_obj_t *img = (lv_obj_t *)var;
    int32_t x  = s_mx0  + (int32_t)((int64_t)(s_mx1  - s_mx0)  * prog / 256);
    int32_t y  = s_my0  + (int32_t)((int64_t)(s_my1  - s_my0)  * prog / 256);
    int32_t sc = s_msc0 + (int32_t)((int64_t)(s_msc1 - s_msc0) * prog / 256);
    if (prog > 0 && prog < 256) {
        int32_t q = (sc + MORPH_SCALE_STEP / 2) / MORPH_SCALE_STEP * MORPH_SCALE_STEP;
        int32_t cx = (s_morph_time_rel.x1 + s_morph_time_rel.x2) / 2;   // time centre in the snapshot
        int32_t cy = (s_morph_time_rel.y1 + s_morph_time_rel.y2) / 2;
        x += cx * (sc - q) / 256;   // keep that centre where the unsnapped scale puts it
        y += cy * (sc - q) / 256;
        sc = q;
    }
    if(sc < 1) sc = 1;
    lv_obj_set_pos(img, x, y);
    lv_image_set_scale(img, (uint32_t)sc);
}

// Where to place + how far to scale the (256pt, centre-rendered) FULL-clock
// snapshot so its TIME text lands exactly on the real clock of `mode` — same
// glyph size, centre-x and top-y. This makes the snapshot→real handoff seamless:
// the dominant time text doesn't jump in size or position when the morph ends.
// (Date/AM-PM reflow between layouts and only roughly match — the end crossfade
// hides that peripheral difference.)
/**
 * @brief Compute where to place and how far to scale the full-clock snapshot so its time text lands on the real clock of @p mode.
 * v2: the snapshot is cropped; (@p ox, @p oy) is the crop's origin in the container.
 * @version 2
 * @req REQ-DISP-004
 */
static void morph_appearance(clock_mode_t mode, int32_t ox, int32_t oy,
                             int32_t *x, int32_t *y, int32_t *scale)
{
    const int32_t pad = 8;
    const int32_t snap_tcx  = pad + (FULL_W - 2 * pad) / 2 - ox;  // time centre-x in snapshot
    const int32_t snap_ttop = pad + 24 - oy;                      // time top-y in snapshot

    if (mode == CLOCK_MODE_MINIMIZED) {
        const int32_t tcx  = MIN_X + pad + (MIN_W - 2 * pad) / 2 - 40;  // -40: layout shift
        const int32_t ttop = MIN_Y + pad;
        *scale = 128;                                        // 128pt / 256pt
        *x = tcx  - snap_tcx  * *scale / 256;
        *y = ttop - snap_ttop * *scale / 256;
    } else if (mode == CLOCK_MODE_TOPBAR) {
        int32_t gx, gy, gw, gh;
        mode_geometry(CLOCK_MODE_TOPBAR, &gx, &gy, &gw, &gh);
        *scale = (FULL_W > 0) ? 256 * gw / FULL_W : 256;
        *x = gx + ox * *scale / 256;
        *y = gy + oy * *scale / 256;
    } else {  // FULL — snapshot's native geometry
        *x = FULL_X + ox; *y = FULL_Y + oy; *scale = 256;
    }
}

/**
 * @brief Apply the inner label layout for @p mode (minimized for unknown modes).
 * @version 1
 * @utility
 */
static void apply_layout_for_mode(clock_widget_t *w, clock_mode_t mode)
{
    switch (mode) {
        case CLOCK_MODE_FULL:      apply_full_layout(w);      break;
        case CLOCK_MODE_TOPBAR:    apply_topbar_layout(w);    break;
        case CLOCK_MODE_MINIMIZED:
        default:                   apply_minimized_layout(w); break;
    }
}

// A completed morph hands its snapshot image off here to dissolve out OVER the
// real clock (already restored to final position underneath). The last morph
// frame is a downscaled, blurry bitmap of the 256pt clock; the real clock is
// crisp at its mode's own font/layout — a hard swap pops (size jump + blur→crisp).
// Crossfading the two hides that pop.
static lv_obj_t      *s_fade_img;
static lv_draw_buf_t *s_fade_snap;

/**
 * @brief lv_anim exec callback: set the object's opacity to @p v.
 * v2: fades a text label's linked shadow with it (snapshot images have no user_data).
 * @version 2
 * @callback
 */
static void fade_opa_cb(void *var, int32_t v)
{
    lv_obj_set_style_opa((lv_obj_t *)var, (lv_opa_t)v, 0);
    lv_obj_t *sh = (lv_obj_t *)lv_obj_get_user_data((lv_obj_t *)var);
    if (sh) lv_obj_set_style_opa(sh, (lv_opa_t)v, 0);   // a text label's shadow fades with it
}

// Tear down any in-flight crossfade immediately (its anim, image, and buffer).
/**
 * @brief Immediately tear down any in-flight crossfade: its animation, image and snapshot buffer.
 * v2: does not destroy the snapshot if it is the morph snapshot cache.
 * v3: checks every cache slot (snap_is_cached).
 * @version 3
 * @utility
 */
static void kill_fade(void)
{
    if (s_fade_img) {
        lv_anim_delete(s_fade_img, NULL);
        lv_obj_delete(s_fade_img);
        s_fade_img = NULL;
    }
    if (s_fade_snap && !snap_is_cached(s_fade_snap)) lv_draw_buf_destroy(s_fade_snap);
    s_fade_snap = NULL;
}

/**
 * @brief lv_anim completion callback: dispose of the finished crossfade.
 * @version 1
 * @callback
 */
static void fade_done_cb(lv_anim_t *a)
{
    LV_UNUSED(a);
    kill_fade();
}

/**
 * @brief Log one finished morph: refresh count, rate, longest refresh and gap, and how far the
 *        snapshot's final time rect lands from the real label it hands off to (px; 0 = seamless).
 * v2: reads this morph's snapshot time rect (s_morph_time_rel; the cache has two slots).
 * v3: also logs dirty areas per refresh.
 * @version 3
 * @req REQ-DISP-004
 */
static void morph_report(clock_widget_t *w, bool natural)
{
    const refresh_probe_t *p = &s_morph_probe;
    int64_t dur = esp_timer_get_time() - s_morph_t0_us;
    uint32_t refrs = p->refrs ? p->refrs : 1;
    lv_obj_update_layout(w->container);
    lv_area_t real;
    lv_obj_get_coords(w->lbl_time, &real);
    int32_t sc = s_msc1;
    const lv_area_t *rel = &s_morph_time_rel;
    int32_t px1 = s_mx1 + rel->x1 * sc / 256, py1 = s_my1 + rel->y1 * sc / 256;
    int32_t px2 = s_mx1 + (rel->x2 + 1) * sc / 256 - 1;
    int32_t py2 = s_my1 + (rel->y2 + 1) * sc / 256 - 1;
    ESP_LOGI(TAG, "[MORPH] -> %d %s in %lld ms: %lu refreshes (%.1f Hz), render avg %lld us, "
             "refresh avg %lld us max %lld us, max gap %lld us, %lu dirty areas/refresh; handoff snapshot "
             "(%ld,%ld)-(%ld,%ld) real (%ld,%ld)-(%ld,%ld)",
             (int)s_morph_target, natural ? "done" : "interrupted", (long long)(dur / 1000),
             (unsigned long)p->refrs, dur > 0 ? (double)p->refrs * 1e6 / (double)dur : 0.0,
             p->render_us / refrs, p->refr_us / refrs, p->refr_max_us, p->gap_max_us,
             (unsigned long)(p->areas / refrs), (long)px1, (long)py1, (long)px2, (long)py2,
             (long)real.x1, (long)real.y1, (long)real.x2, (long)real.y2);
    refresh_probe_reset(&s_morph_probe, false);
}

// Restore the real clock at the target layout, then dispose of the morph
// snapshot image — by dissolving it out (crossfade=true, natural completion) or
// instantly (crossfade=false, an interrupting morph settling this one). Idempotent.
// Strictly serial: an interrupted morph can never orphan its snapshot on the layer.
/**
 * @brief Restore the real clock at the morph's target layout and colour, then dissolve the snapshot out (@p crossfade) or delete it instantly; idempotent.
 * v2: re-syncs the label shadows at the target layout.
 * v3: logs the morph's frame rate and handoff error (morph_report).
 * v4: checks every snapshot cache slot (snap_is_cached).
 * v5: runs the pending settled callback (clock_widget_when_settled) last.
 * v6: AM/PM and the date are no longer faded in here (they fade in during the morph's second
 *     half, morph_midpoint_cb); they are just made opaque.
 * @version 6
 * @req REQ-DISP-004
 */
static void finalize_morph(bool crossfade)
{
    clock_widget_t *w = s_morph_w;
    if (!w) return;

    // Stop the AM/PM+date fade-out and restore the real clock at the TARGET layout.
    // Only the TIME was hidden during the morph (the container stayed visible), so
    // just un-hide it and move the labels to their target positions.
    lv_anim_delete(w->lbl_ampm, fade_opa_cb);
    lv_anim_delete(w->lbl_date, fade_opa_cb);
    lv_obj_remove_flag(w->lbl_time, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(w->container, LV_OBJ_FLAG_HIDDEN);
    snap_to(w, s_morph_target);
    apply_layout_for_mode(w, s_morph_target);
    apply_effective_color(w);
    morph_report(w, crossfade);

    // AM/PM + date faded in at the target layout over the morph's second half; whatever
    // is left of that fade ends here.
    lv_obj_set_style_opa(w->lbl_ampm, LV_OPA_COVER, 0);
    lv_obj_set_style_opa(w->lbl_date, LV_OPA_COVER, 0);
    if (crossfade && s_morph_img && s_morph_snap) {
        kill_fade();                          // only one dissolve at a time
        lv_anim_delete(s_morph_img, NULL);    // stop the scale anim
        s_fade_img  = s_morph_img;
        s_fade_snap = s_morph_snap;
        lv_anim_t fa;
        lv_anim_init(&fa);
        lv_anim_set_var(&fa, s_fade_img);
        lv_anim_set_duration(&fa, 140);
        lv_anim_set_values(&fa, LV_OPA_COVER, LV_OPA_TRANSP);
        lv_anim_set_exec_cb(&fa, fade_opa_cb);
        lv_anim_set_completed_cb(&fa, fade_done_cb);
        lv_anim_start(&fa);
    } else {
        // Instant settle (an interrupting morph).
        if (s_morph_img) { lv_anim_delete(s_morph_img, NULL); lv_obj_delete(s_morph_img); }
        if (s_morph_snap && !snap_is_cached(s_morph_snap)) lv_draw_buf_destroy(s_morph_snap);
    }
    s_morph_img  = NULL;
    s_morph_snap = NULL;
    s_morph_w    = NULL;
    shadows_sync(w);
    void (*settled)(void) = s_settled_cb;
    s_settled_cb = NULL;
    if (settled) settled();
}

/**
 * @brief lv_anim completion callback for the morph: finalize with a crossfade.
 * @version 1
 * @callback
 */
static void morph_done_cb(lv_anim_t *a)
{
    LV_UNUSED(a);
    finalize_morph(true);   // natural completion → dissolve the snapshot out
}

// Get position/size for a mode (avoids repeating constants in animation logic).
/**
 * @brief Return position and size of the clock container for @p mode.
 * @version 1
 * @utility
 */
static void mode_geometry(clock_mode_t mode, int32_t *x, int32_t *y, int32_t *w, int32_t *h)
{
    switch (mode) {
        case CLOCK_MODE_FULL:      *x = FULL_X; *y = FULL_Y; *w = FULL_W; *h = FULL_H; break;
        case CLOCK_MODE_TOPBAR:    *x = BAR_X;  *y = BAR_Y;  *w = BAR_W;  *h = BAR_H;  break;
        case CLOCK_MODE_MINIMIZED:
        default:                   *x = MIN_X;  *y = MIN_Y;  *w = MIN_W;  *h = MIN_H;  break;
    }
}

// Effective label colour for the current mode: the theme ink made legible over the full
// clock's backdrop, or over the minimized clock's (each state sets that one from what is
// behind it).
/**
 * @brief The text colour of @p mode: the full, top-bar or minimized one.
 * @version 1
 * @utility
 * @return The colour.
 */
static lv_color_t mode_color(const clock_widget_t *w, clock_mode_t mode)
{
    lv_color_t c = w->min_color;
    if (mode == CLOCK_MODE_FULL) c = w->color;
    else if (mode == CLOCK_MODE_TOPBAR) c = w->bar_color;
    return c;
}

/**
 * @brief Apply the mode's effective text colour (full colour in FULL mode, minimized colour otherwise) to all three labels.
 * v2: also colours the drop shadows (contrasting black/white).
 * v3: colours from the theme roles (ui_theme), no literals.
 * v4: the top bar has its own colour (it borrowed the minimized one, judged for the top-right corner).
 * @version 4
 * @req REQ-BG-002
 */
static void apply_effective_color(clock_widget_t *w)
{
    lv_color_t c = mode_color(w, w->mode);
    lv_obj_set_style_text_color(w->lbl_time, c, 0);
    lv_obj_set_style_text_color(w->lbl_ampm, c, 0);
    lv_obj_set_style_text_color(w->lbl_date, c, 0);
    lv_color_t sc = ui_theme_shadow_for(c);   // the theme's dark or light extreme
    lv_obj_set_style_text_color(w->sh_time, sc, 0);
    lv_obj_set_style_text_color(w->sh_ampm, sc, 0);
    lv_obj_set_style_text_color(w->sh_date, sc, 0);
}

/**
 * @brief Drop one cache slot, destroying its buffer unless a morph or crossfade still shows it
 *        (that owner then frees it, since it is no longer cached).
 * @version 1
 * @utility
 */
static void snap_slot_release(snap_slot_t *slot)
{
    lv_draw_buf_t *old = slot->buf;
    memset(slot, 0, sizeof(*slot));
    if (old && old != s_morph_snap && old != s_fade_snap) lv_draw_buf_destroy(old);
}

/**
 * @brief Drop every snapshot cache slot (see snap_slot_release).
 * v2: two colour-keyed slots.
 * @version 2
 * @utility
 */
static void snap_cache_release(void)
{
    for (int i = 0; i < SNAP_SLOTS; i++) snap_slot_release(&s_snap_cache[i]);
}

/** Transparent margin kept around the cropped snapshot, and the crop width alignment. Without
 *  them the PPA's scaled edge column/row showed as a striped line along the snapshot's edges. */
#define SNAP_CROP_MARGIN 16
#define SNAP_CROP_ALIGN  16

/**
 * @brief Copy the part of the container snapshot @p full covered by the time label and its
 *        shadow, plus a transparent margin, into a new buffer; write the crop's
 *        container-relative area to @p crop.
 * v2: SNAP_CROP_MARGIN margin and SNAP_CROP_ALIGN width: a tight crop put the PPA scaled-edge
 *     artifacts on visible pixels (striped column and line along the edges, seen on device).
 * @version 2
 * @utility
 * @return The cropped buffer, or NULL on allocation failure.
 */
static lv_draw_buf_t *snap_crop(clock_widget_t *w, const lv_draw_buf_t *full, lv_area_t *crop)
{
    lv_area_t tc, sc, cc, whole;
    lv_obj_get_coords(w->lbl_time, &tc);
    lv_obj_get_coords(w->sh_time, &sc);
    lv_obj_get_coords(w->container, &cc);
    lv_area_join(crop, &tc, &sc);
    lv_area_move(crop, -cc.x1, -cc.y1);
    lv_area_increase(crop, SNAP_CROP_MARGIN, SNAP_CROP_MARGIN);
    int32_t grow = (SNAP_CROP_ALIGN - lv_area_get_width(crop) % SNAP_CROP_ALIGN) % SNAP_CROP_ALIGN;
    crop->x2 += grow;
    lv_area_set(&whole, 0, 0, full->header.w - 1, full->header.h - 1);
    if (!lv_area_intersect(crop, crop, &whole)) return NULL;
    lv_draw_buf_t *out = lv_draw_buf_create(lv_area_get_width(crop), lv_area_get_height(crop),
                                            LV_COLOR_FORMAT_ARGB8888, LV_STRIDE_AUTO);
    if (out) lv_draw_buf_copy(out, NULL, full, crop);
    return out;
}

/**
 * @brief Render the FULL-layout time, with its drop shadow, in @p color into @p slot, cropped
 *        to the time and its shadow. Leaves the labels in FULL layout and colour @p color,
 *        AM/PM and date hidden.
 * v2: cropped (snap_crop): the full 1008x350 container snapshot made the PPA scale ~2.5x the
 *     pixels every frame and redrew the background under its transparent margins.
 * v3: colours from the theme roles (ui_theme), no literals.
 * @version 3
 * @req REQ-DISP-004
 * @return The snapshot, or NULL if rendering failed (OOM).
 */
static lv_draw_buf_t *snap_render(clock_widget_t *w, lv_color_t color, snap_slot_t *slot)
{
    snap_to(w, CLOCK_MODE_FULL);
    apply_full_layout(w);
    lv_obj_set_style_text_color(w->lbl_time, color, 0);
    lv_obj_set_style_text_color(w->sh_time, ui_theme_shadow_for(color), 0);
    lv_obj_add_flag(w->lbl_ampm, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(w->lbl_date, LV_OBJ_FLAG_HIDDEN);
    shadows_sync(w);   // the time's shadow is in the snapshot, AM/PM's and the date's are not
    lv_draw_buf_t *full = lv_snapshot_take(w->container, LV_COLOR_FORMAT_ARGB8888);
    if (!full) return NULL;
    lv_area_t crop;
    lv_draw_buf_t *snap = snap_crop(w, full, &crop);
    lv_draw_buf_destroy(full);
    if (!snap) return NULL;
    lv_area_t tc, cc;
    lv_obj_get_coords(w->lbl_time, &tc);
    lv_obj_get_coords(w->container, &cc);
    snap_slot_release(slot);
    slot->buf = snap;
    slot->color = color;
    slot->origin = (lv_point_t){ crop.x1, crop.y1 };
    strlcpy(slot->text, lv_label_get_text(w->lbl_time), sizeof(slot->text));
    lv_area_set(&slot->time_rel, tc.x1 - cc.x1, tc.y1 - cc.y1, tc.x2 - cc.x1, tc.y2 - cc.y1);
    lv_area_move(&slot->time_rel, -crop.x1, -crop.y1);
    return snap;
}

/**
 * @brief Return the FULL-layout time snapshot for the current time text in @p color: a cached
 *        one when text and colour match, else render one into the least recently used slot.
 *        Sets s_morph_time_rel. On a render, leaves the labels as snap_render does.
 * v2: includes the time's drop shadow; rendered in the morph's source colour (no recolour),
 *     two colour-keyed cache slots; records the time label's area in the snapshot.
 * v4: also hands over the snapshot's crop origin (s_morph_origin).
 * @version 4
 * @req REQ-DISP-004
 * @return Snapshot buffer (owned by the cache), or NULL if rendering failed (OOM).
 */
static lv_draw_buf_t *morph_snapshot_get(clock_widget_t *w, lv_color_t color)
{
    const char *text = lv_label_get_text(w->lbl_time);
    int slot = -1;
    for (int i = 0; i < SNAP_SLOTS && slot < 0; i++) {
        const snap_slot_t *c = &s_snap_cache[i];
        if (c->buf && lv_color_eq(c->color, color) && strcmp(text, c->text) == 0) slot = i;
    }
    if (slot < 0) {
        int64_t t0 = esp_timer_get_time();
        slot = (s_snap_last + 1) % SNAP_SLOTS;
        bool ok = snap_render(w, color, &s_snap_cache[slot]) != NULL;
        ESP_LOGI(TAG, "morph snapshot rendered for '%s' (#%06lx) in %lld us (cache miss)%s",
                 text, (unsigned long)lv_color_to_u32(color) & 0xFFFFFF,
                 (long long)(esp_timer_get_time() - t0), ok ? "" : ": FAILED");
        if (!ok) return NULL;
    }
    s_snap_last = slot;
    s_morph_time_rel = s_snap_cache[slot].time_rel;
    s_morph_origin   = s_snap_cache[slot].origin;
    return s_snap_cache[slot].buf;
}

/**
 * @brief lv_anim start callback at the morph's midpoint: move AM/PM and the date (faded out
 *        by now) to the target layout and colour, so they fade in there while the time
 *        snapshot finishes its move.
 * @version 1
 * @callback
 */
static void morph_midpoint_cb(lv_anim_t *a)
{
    LV_UNUSED(a);
    clock_widget_t *w = s_morph_w;
    if (!w) return;
    snap_to(w, s_morph_target);
    apply_layout_for_mode(w, s_morph_target);
    s_morph_from = s_morph_target;   // the layout now on screen (clock_widget_update aligns to it)
    apply_effective_color(w);
    shadows_sync(w);
}

/**
 * @brief @p w at @p mode's layout and colour at once (no morph).
 * @version 1
 * @utility
 */
static void snap_layout(clock_widget_t *w, clock_mode_t mode)
{
    snap_to(w, mode);
    apply_layout_for_mode(w, mode);
    apply_effective_color(w);
    shadows_sync(w);
}

/**
 * @brief Switch to @p mode at once, without the morph (a still picture of the clock).
 * @version 1
 * @req REQ-CLOCK-002
 */
void clock_widget_snap_mode(clock_widget_t *w, clock_mode_t mode)
{
    if (!w) return;
    finalize_morph(false);
    kill_fade();
    if (mode == CLOCK_MODE_MINIMIZED) w->min_color = w->min_base;
    w->mode = mode;
    lv_obj_remove_flag(w->lbl_ampm, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(w->lbl_date, LV_OBJ_FLAG_HIDDEN);
    snap_layout(w, mode);
}

/**
 * @brief Switch clock mode; animates a 500 ms snapshot morph (time and its shadow scaled, then crossfaded to the real clock; AM/PM and date crossfaded), or snaps instantly if the snapshot cannot be taken.
 * v2: keeps the drop shadows out of the morph snapshot and in sync around it.
 * v3: reuses the cached time snapshot (morph_snapshot_get) instead of re-rendering it every morph.
 * v4: arms the morph refresh probe (frame rate measured per morph, see morph_report).
 * v5: snapshot rendered in the source colour (no per-frame recolour, so PPA scales it); the
 *     colour change to the target happens in the end crossfade.
 * v6: drops a pending settled callback: it belonged to the mode being left.
 * v7: places the cropped snapshot by its crop origin.
 * v8: CLOCK_MORPH_MS duration; AM/PM + date fade out over half of it; records the source mode.
 * v9: AM/PM + date fade back in at the target layout over the second half (they were gone
 *     until the time landed, then appeared: a two-step look).
 * v10: finishes a time roll first (labels at rest for the snapshot).
 * v11: morph source colour per mode (top bar included).
 * v12: the instant fallback is snap_layout (shared with clock_widget_snap_mode).
 * @version 12
 * @req REQ-CLOCK-002
 * @req REQ-DISP-004
 */
void clock_widget_set_mode(clock_widget_t *w, clock_mode_t mode)
{
    if (!w) return;
    s_settled_cb = NULL;     // a pending callback belonged to the mode being left
    text_roll_finish();      // labels at rest before the morph snapshots them
    finalize_morph(false);   // settle any in-flight morph instantly — morphs are serial
    kill_fade();             // and drop any still-dissolving previous snapshot
    if (w->mode == mode) {
        // No geometry change, but on a minimized→minimized state switch the caller
        // has re-derived min_base for the NEW background — apply it. Without this the
        // clock keeps the PREVIOUS state's colour (e.g. radar's white bleeding into
        // weather/astronomy, which should be dark over their bright photo).
        if (mode == CLOCK_MODE_MINIMIZED) {
            w->min_color = w->min_base;
            apply_effective_color(w);
        }
        return;
    }

    clock_mode_t old_mode = w->mode;

    // Source colour of the morph (the snapshot is rendered in it; the end crossfade
    // reveals the target colour). Minimized defaults to min_base; states whose
    // own content covers the bg (radar/photos) set min_base before minimizing.
    if (mode == CLOCK_MODE_MINIMIZED) w->min_color = w->min_base;
    s_mc0 = mode_color(w, old_mode);

    // FULL-layout time snapshot in the source colour: cached per time text and colour.
    s_morph_snap = morph_snapshot_get(w, s_mc0);

    lv_obj_remove_flag(w->lbl_ampm, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(w->lbl_date, LV_OBJ_FLAG_HIDDEN);
    w->mode = mode;
    if (!s_morph_snap) {
        // Snapshot failed (OOM) — fall back to an instant snap, no animation.
        snap_layout(w, mode);
        ESP_LOGW(TAG, "morph: snapshot failed, snapping to mode %d", mode);
        return;
    }

    // Instead of hiding the whole clock, keep it at the SOURCE layout and hide only
    // its TIME — the scaling snapshot stands in for that. The real AM/PM + date stay
    // visible at their source positions and fade OUT in place here (no slide, no
    // ghost); finalize_morph moves them to the target layout and fades them back IN.
    snap_to(w, old_mode);
    apply_layout_for_mode(w, old_mode);
    lv_obj_set_style_text_color(w->lbl_ampm, s_mc0, 0);
    lv_obj_set_style_text_color(w->lbl_date, s_mc0, 0);
    lv_obj_add_flag(w->lbl_time, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_opa(w->lbl_ampm, LV_OPA_COVER, 0);
    lv_obj_set_style_opa(w->lbl_date, LV_OPA_COVER, 0);
    lv_anim_t lf;
    lv_anim_init(&lf);
    lv_anim_set_duration(&lf, CLOCK_MORPH_MS / 2);
    lv_anim_set_values(&lf, LV_OPA_COVER, LV_OPA_TRANSP);
    lv_anim_set_exec_cb(&lf, fade_opa_cb);
    lv_anim_set_var(&lf, w->lbl_ampm); lv_anim_start(&lf);
    lv_anim_set_var(&lf, w->lbl_date); lv_anim_start(&lf);
    // ...and fade back in at the target layout over the second half, converging with the
    // time. Not early-applied: that would cancel the fade-out above at once.
    lv_anim_t li = lf;
    lv_anim_set_delay(&li, CLOCK_MORPH_MS / 2);
    lv_anim_set_values(&li, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_set_early_apply(&li, false);
    lv_anim_set_var(&li, w->lbl_date); lv_anim_start(&li);
    lv_anim_set_start_cb(&li, morph_midpoint_cb);
    lv_anim_set_var(&li, w->lbl_ampm); lv_anim_start(&li);
    shadows_sync(w);   // source layout; the hidden time's shadow stays hidden

    s_morph_img = lv_image_create(lv_obj_get_parent(w->container));
    lv_image_set_src(s_morph_img, s_morph_snap);
    lv_image_set_pivot(s_morph_img, 0, 0);            // scale about top-left
    lv_obj_remove_flag(s_morph_img, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    // Endpoints aligned so the TIME text matches the real clock at each mode.
    morph_appearance(old_mode, s_morph_origin.x, s_morph_origin.y, &s_mx0, &s_my0, &s_msc0);
    morph_appearance(mode,     s_morph_origin.x, s_morph_origin.y, &s_mx1, &s_my1, &s_msc1);
    s_morph_w      = w;
    s_morph_target = mode;
    s_morph_from   = old_mode;
    morph_exec_cb(s_morph_img, 0);   // source appearance immediately (no flash)

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_morph_img);
    lv_anim_set_duration(&a, CLOCK_MORPH_MS);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_set_values(&a, 0, 256);
    lv_anim_set_exec_cb(&a, morph_exec_cb);
    lv_anim_set_completed_cb(&a, morph_done_cb);
    lv_anim_start(&a);
    if (!s_morph_probe_attached) {
        refresh_probe_attach(&s_morph_probe);
        s_morph_probe_attached = true;
    }
    refresh_probe_reset(&s_morph_probe, true);
    s_morph_t0_us = esp_timer_get_time();

    static const char *mode_names[] = {"FULL", "MINIMIZED", "TOPBAR"};
    ESP_LOGI(TAG, "ClockWidget mode → %s (snapshot morph)", mode_names[mode]);
}

/**
 * @brief Set the full-mode text colour and apply it if the clock is in full mode.
 * @version 1
 * @req REQ-BG-002
 */
void clock_widget_set_color(clock_widget_t *w, lv_color_t color)
{
    if (!w) return;
    w->color = color;
    apply_effective_color(w);   // honours current mode (only FULL uses it)
}

/**
 * @brief Set the top-bar text colour and apply it if the clock is in the top bar.
 * @version 1
 * @req REQ-BG-002
 * @req REQ-CLOCK-002
 */
void clock_widget_set_bar_color(clock_widget_t *w, lv_color_t color)
{
    if (!w) return;
    w->bar_color = color;
    apply_effective_color(w);
}

/**
 * @brief Write the screen area of the top-bar clock into @p out (no-op if NULL).
 * @version 1
 * @req REQ-CLOCK-002
 * @req REQ-BG-002
 */
void clock_widget_bar_area(lv_area_t *out)
{
    if (!out) return;
    out->x1 = BAR_X;
    out->y1 = BAR_Y;
    out->x2 = BAR_X + BAR_W - 1;
    out->y2 = BAR_Y + BAR_H - 1;
}

/**
 * @brief Set the current minimized text colour and apply it if the clock is minimized.
 * @version 1
 * @req REQ-BG-002
 */
void clock_widget_set_minimized_color(clock_widget_t *w, lv_color_t color)
{
    if (!w) return;
    w->min_color = color;
    apply_effective_color(w);   // applies immediately if currently minimized
}

/**
 * @brief Set the default minimized colour, applied on the next minimize.
 * @version 1
 * @req REQ-BG-002
 */
void clock_widget_set_minimized_base_color(clock_widget_t *w, lv_color_t color)
{
    if (w) w->min_base = color;   // default applied on the next minimize
}

/**
 * @brief Store the user's configured text colour, the base hue for the derived legible colours.
 * @version 1
 * @req REQ-BG-002
 */
void clock_widget_set_user_color(clock_widget_t *w, lv_color_t color)
{
    if (w) w->user_color = color;   // OKLCH base hue; effective colours derived from it
}

/**
 * @brief Get the user's configured text colour.
 * v2: colours from the theme roles (ui_theme), no literals.
 * @version 2
 * @req REQ-BG-002
 * @return Stored user colour, or white if @p w is NULL.
 */
lv_color_t clock_widget_user_color(const clock_widget_t *w)
{
    return w ? w->user_color : ui_theme_color(UI_ROLE_TEXT);
}

/**
 * @brief Write the screen area occupied by the minimized clock into @p out (no-op if NULL).
 * @version 1
 * @req REQ-CLOCK-002
 * @req REQ-BG-002
 */
void clock_widget_min_area(lv_area_t *out)
{
    if (!out) return;
    out->x1 = MIN_X;
    out->y1 = MIN_Y;
    out->x2 = MIN_X + MIN_W - 1;
    out->y2 = MIN_Y + MIN_H - 1;
}

/**
 * @brief Get the clock's current mode.
 * @version 1
 * @req REQ-CLOCK-002
 * @return Current mode, or CLOCK_MODE_FULL if @p w is NULL.
 */
clock_mode_t clock_widget_get_mode(const clock_widget_t *w)
{
    return w ? w->mode : CLOCK_MODE_FULL;
}

/**
 * @brief Offset the clock container by (@p dx, @p dy), each clamped to +/-4 px, for burn-in mitigation.
 * @version 1
 * @req REQ-CLOCK-003
 */
void clock_widget_set_jitter(clock_widget_t *w, int8_t dx, int8_t dy)
{
    if (!w || !w->container) return;
    if (dx >  4) dx =  4;
    if (dx < -4) dx = -4;
    if (dy >  4) dy =  4;
    if (dy < -4) dy = -4;
    lv_obj_set_style_translate_x(w->container, (int32_t)dx, 0);
    lv_obj_set_style_translate_y(w->container, (int32_t)dy, 0);
}

/**
 * @brief Run @p cb once the clock is at rest: now if no morph is running, else when the morph
 *        finishes. A later mode change drops it. Lets the clock screen start its animation
 *        after the restore morph instead of competing with it for every frame.
 * @version 1
 * @req REQ-DISP-004
 */
void clock_widget_when_settled(clock_widget_t *w, void (*cb)(void))
{
    if (!w || !cb) return;
    if (s_morph_w) s_settled_cb = cb;
    else cb();
}
