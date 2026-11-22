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
#include "ui_contrast.h"
#include "refresh_probe.h"
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
    lv_color_t   min_base;    // default MINIMIZED colour (legible over the bg image,
                              // used by states that show the clock bg behind the clock)
};

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

// All non-full modes reserve ALERT_BANNER_HEIGHT (50px) at the top so the alert
// banner never overlaps minimized/topbar content.  Full mode keeps y=8 because the
// 256pt time text is tall enough that a 50px banner overlap is < 7% and acceptable.
//
// Full mode:      1008×350 at (8, 8)
// Minimized:      420×180  at (588, 58)  — 8px below alert zone
// Topbar:         1008×56  at (8, 54)    — 4px below alert zone
#define FULL_X  8
#define FULL_Y  8
#define FULL_W  1008
#define FULL_H  350
#define MIN_W   420
#define MIN_X   (1024 - MIN_W - 16)  // right-aligned with 16px margin
#define MIN_Y   (ALERT_BANNER_HEIGHT + 8)   // 58 — below alert banner zone
#define MIN_H   180
#define BAR_X   8
#define BAR_Y   (ALERT_BANNER_HEIGHT + 4)   // 54 — below alert banner zone
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

// Apply fonts and inner label alignment for full mode.
// Does NOT touch container position/size — that's handled by animation or snap_to().
/**
 * @brief Full-mode layout: 256pt time, 48pt AM/PM baseline-aligned to it, 48pt date at the bottom; does not move the container.
 * v2: time uses the 8 bpp static-bitmap digit font (same metrics).
 * v3: AM/PM and date use the 8 bpp static-bitmap 48 pt font (same metrics).
 * @version 3
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
    lv_obj_align_to(w->lbl_ampm, w->lbl_time, LV_ALIGN_OUT_RIGHT_BOTTOM, 8, -43);

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
 * @version 3
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
    lv_obj_align_to(w->lbl_ampm, w->lbl_time, LV_ALIGN_OUT_RIGHT_BOTTOM, 8, -17);

    // Date: 24pt, below time
    lv_obj_set_style_text_font(w->lbl_date, &lv_font_montserrat_24, 0);
    lv_obj_align(w->lbl_date, LV_ALIGN_BOTTOM_MID, 0, -4);

    lv_obj_clear_flag(w->container, LV_OBJ_FLAG_HIDDEN);
}

// Apply fonts and inner label alignment for topbar mode (thin strip, settings use).
// Does NOT touch container position/size — that's handled by animation or snap_to().
/**
 * @brief Topbar-mode layout: 48pt time left, 24pt AM/PM beside it, 24pt date right-aligned; does not move the container.
 * @version 1
 * @req REQ-CLOCK-002
 */
static void apply_topbar_layout(clock_widget_t *w)
{
    // Time: 48pt, left-aligned
    lv_obj_set_style_text_font(w->lbl_time, &nunito_48, 0);
    lv_obj_align(w->lbl_time, LV_ALIGN_LEFT_MID, 8, 0);

    // AM/PM: 24pt, right of time
    lv_obj_set_style_text_font(w->lbl_ampm, &lv_font_montserrat_24, 0);
    lv_obj_align_to(w->lbl_ampm, w->lbl_time, LV_ALIGN_OUT_RIGHT_MID, 6, 0);

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
 * @brief Hide all three shadows (the morph snapshot is recoloured wholesale, so it must not contain them).
 * @version 1
 * @dg_internal
 */
static void shadows_hide(clock_widget_t *w)
{
    lv_obj_add_flag(w->sh_time, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(w->sh_ampm, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(w->sh_date, LV_OBJ_FLAG_HIDDEN);
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
 * @dg_internal
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
 * @version 2
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
    w->user_color = lv_color_white();
    w->color      = lv_color_white();
    w->min_color  = lv_color_white();   // legible default until sampled
    w->min_base   = lv_color_white();
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
 * @version 2
 * @req REQ-CLOCK-001
 */
void clock_widget_destroy(clock_widget_t *w)
{
    if (!w) return;
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
 * @version 2
 * @req REQ-CLOCK-001
 */
void clock_widget_update(clock_widget_t *w, const struct tm *ti)
{
    if (!w || !ti) return;

    char buf_time[6], buf_ampm[3], buf_date[30];
    format_time(ti, buf_time, sizeof(buf_time),
                buf_ampm, sizeof(buf_ampm),
                buf_date, sizeof(buf_date));

    lv_label_set_text(w->lbl_time, buf_time);
    lv_label_set_text(w->lbl_ampm, buf_ampm);
    lv_label_set_text(w->lbl_date, buf_date);

    // Re-align AM/PM after time text change (width varies by digit glyphs)
    switch (w->mode) {
        case CLOCK_MODE_FULL:
            lv_obj_align_to(w->lbl_ampm, w->lbl_time, LV_ALIGN_OUT_RIGHT_BOTTOM, 8, -43);
            break;
        case CLOCK_MODE_MINIMIZED:
            lv_obj_align_to(w->lbl_ampm, w->lbl_time, LV_ALIGN_OUT_RIGHT_BOTTOM, 8, -17);
            break;
        case CLOCK_MODE_TOPBAR:
            lv_obj_align_to(w->lbl_ampm, w->lbl_time, LV_ALIGN_OUT_RIGHT_MID, 6, 0);
            break;
    }
    shadows_sync(w);
}

// Snapshot morph. LVGL re-rasterizes a transform layer every frame, so scaling
// the live 256pt clock was jagged. Instead we rasterize the full clock to an
// image ONCE, then animate a scaled lv_image of that static bitmap — the source
// pixels never change, so each frame is just a (PPA-accelerated) scale-blit, no
// re-raster. Crisp (high-res snapshot, only ever downscaled) AND smooth.
static lv_obj_t      *s_morph_img;     // temporary scaled image of the snapshot
static lv_draw_buf_t *s_morph_snap;    // the snapshot buffer (freed at completion)
static clock_widget_t *s_morph_w;
static clock_mode_t    s_morph_target;
static int32_t s_mx0, s_my0, s_msc0;   // source pos + scale (256 = 1.0x)
static int32_t s_mx1, s_my1, s_msc1;   // target pos + scale
static lv_color_t s_mc0, s_mc1;        // source/target text colour (recolours snapshot)
// Snapshot cache: the white FULL-layout time snapshot depends only on the time text, so it
// is reused across morphs until the minute changes (rendering it is the bulk of a morph's
// LVGL-lock time). The cache OWNS the buffer; morph/crossfade free a snapshot only once it
// is no longer the cache.
static lv_draw_buf_t *s_snap_cache;
static char           s_snap_text[8];
static lv_area_t      s_snap_time_rel;   // time label's area inside the cached snapshot
// Morph measurement: refresh rate/gaps during the morph and the handoff geometry error.
static refresh_probe_t s_morph_probe;
static bool            s_morph_probe_attached;
static int64_t         s_morph_t0_us;

/**
 * @brief Linearly interpolate between two bytes at @p prog in 0..256.
 * @version 1
 * @dg_internal
 * @return Interpolated value.
 */
static uint8_t lerp_u8(uint8_t a, uint8_t b, int32_t prog)
{
    return (uint8_t)((int32_t)a + ((int32_t)b - (int32_t)a) * prog / 256);
}

/**
 * @brief lv_anim exec callback: place, scale and recolour the morph snapshot at progress @p prog (0 source .. 256 target).
 * @version 1
 * @callback
 */
static void morph_exec_cb(void *var, int32_t prog)   // prog: 0 (source) .. 256 (target)
{
    lv_obj_t *img = (lv_obj_t *)var;
    int32_t x  = s_mx0  + (int32_t)((int64_t)(s_mx1  - s_mx0)  * prog / 256);
    int32_t y  = s_my0  + (int32_t)((int64_t)(s_my1  - s_my0)  * prog / 256);
    int32_t sc = s_msc0 + (int32_t)((int64_t)(s_msc1 - s_msc0) * prog / 256);
    if(sc < 1) sc = 1;
    lv_obj_set_pos(img, x, y);
    lv_image_set_scale(img, (uint32_t)sc);
    // Recolour the white snapshot from the old mode's colour to the new — the
    // OKLCH text colour transitions smoothly instead of popping at the handoff.
    lv_color_t c = lv_color_make(lerp_u8(s_mc0.red,   s_mc1.red,   prog),
                                 lerp_u8(s_mc0.green, s_mc1.green, prog),
                                 lerp_u8(s_mc0.blue,  s_mc1.blue,  prog));
    lv_obj_set_style_image_recolor(img, c, 0);
}

// Where to place + how far to scale the (256pt, centre-rendered) FULL-clock
// snapshot so its TIME text lands exactly on the real clock of `mode` — same
// glyph size, centre-x and top-y. This makes the snapshot→real handoff seamless:
// the dominant time text doesn't jump in size or position when the morph ends.
// (Date/AM-PM reflow between layouts and only roughly match — the end crossfade
// hides that peripheral difference.)
/**
 * @brief Compute where to place and how far to scale the full-clock snapshot so its time text lands on the real clock of @p mode.
 * @version 1
 * @req REQ-DISP-004
 */
static void morph_appearance(clock_mode_t mode, int32_t *x, int32_t *y, int32_t *scale)
{
    const int32_t pad = 8;
    const int32_t snap_tcx  = pad + (FULL_W - 2 * pad) / 2;  // time centre-x in snapshot
    const int32_t snap_ttop = pad + 24;                      // time top-y in snapshot

    if (mode == CLOCK_MODE_MINIMIZED) {
        const int32_t tcx  = MIN_X + pad + (MIN_W - 2 * pad) / 2 - 40;  // -40: layout shift
        const int32_t ttop = MIN_Y + pad;
        *scale = 128;                                        // 128pt / 256pt
        *x = tcx  - snap_tcx  * *scale / 256;
        *y = ttop - snap_ttop * *scale / 256;
    } else if (mode == CLOCK_MODE_TOPBAR) {
        int32_t gx, gy, gw, gh;
        mode_geometry(CLOCK_MODE_TOPBAR, &gx, &gy, &gw, &gh);
        *x = gx; *y = gy; *scale = (FULL_W > 0) ? 256 * gw / FULL_W : 256;
    } else {  // FULL — snapshot's native geometry
        *x = FULL_X; *y = FULL_Y; *scale = 256;
    }
}

/**
 * @brief Apply the inner label layout for @p mode (minimized for unknown modes).
 * @version 1
 * @dg_internal
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
 * @version 2
 * @dg_internal
 */
static void kill_fade(void)
{
    if (s_fade_img) {
        lv_anim_delete(s_fade_img, NULL);
        lv_obj_delete(s_fade_img);
        s_fade_img = NULL;
    }
    if (s_fade_snap && s_fade_snap != s_snap_cache) lv_draw_buf_destroy(s_fade_snap);
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
 * @version 1
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
    int32_t px1 = s_mx1 + s_snap_time_rel.x1 * sc / 256, py1 = s_my1 + s_snap_time_rel.y1 * sc / 256;
    int32_t px2 = s_mx1 + (s_snap_time_rel.x2 + 1) * sc / 256 - 1;
    int32_t py2 = s_my1 + (s_snap_time_rel.y2 + 1) * sc / 256 - 1;
    ESP_LOGI(TAG, "[MORPH] -> %d %s in %lld ms: %lu refreshes (%.1f Hz), render avg %lld us, "
             "refresh avg %lld us max %lld us, max gap %lld us; handoff snapshot "
             "(%ld,%ld)-(%ld,%ld) real (%ld,%ld)-(%ld,%ld)",
             (int)s_morph_target, natural ? "done" : "interrupted", (long long)(dur / 1000),
             (unsigned long)p->refrs, dur > 0 ? (double)p->refrs * 1e6 / (double)dur : 0.0,
             p->render_us / refrs, p->refr_us / refrs, p->refr_max_us, p->gap_max_us,
             (long)px1, (long)py1, (long)px2, (long)py2,
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
 * @version 3
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

    if (crossfade && s_morph_img && s_morph_snap) {
        // Fade AM/PM + date back in at the target layout as the time snapshot
        // dissolves out — a clean crossfade instead of a slide or a pop.
        lv_obj_set_style_opa(w->lbl_ampm, LV_OPA_TRANSP, 0);
        lv_obj_set_style_opa(w->lbl_date, LV_OPA_TRANSP, 0);
        lv_anim_t li;
        lv_anim_init(&li);
        lv_anim_set_duration(&li, 160);
        lv_anim_set_values(&li, LV_OPA_TRANSP, LV_OPA_COVER);
        lv_anim_set_exec_cb(&li, fade_opa_cb);
        lv_anim_set_var(&li, w->lbl_ampm); lv_anim_start(&li);
        lv_anim_set_var(&li, w->lbl_date); lv_anim_start(&li);

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
        // Instant settle (an interrupting morph): labels fully opaque, no fade.
        lv_obj_set_style_opa(w->lbl_ampm, LV_OPA_COVER, 0);
        lv_obj_set_style_opa(w->lbl_date, LV_OPA_COVER, 0);
        if (s_morph_img) { lv_anim_delete(s_morph_img, NULL); lv_obj_delete(s_morph_img); }
        if (s_morph_snap && s_morph_snap != s_snap_cache) lv_draw_buf_destroy(s_morph_snap);
    }
    s_morph_img  = NULL;
    s_morph_snap = NULL;
    s_morph_w    = NULL;
    shadows_sync(w);
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
 * @dg_internal
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

// Effective label colour for the current mode: the user's NVS text_color only
// on the FULL clock screen (sized to its background image); a fixed, legible
// white once minimized over the dark state screens, where the user's chosen
// colour — which may be dark — is not guaranteed to contrast.
/**
 * @brief Apply the mode's effective text colour (full colour in FULL mode, minimized colour otherwise) to all three labels.
 * v2: also colours the drop shadows (contrasting black/white).
 * @version 2
 * @req REQ-BG-002
 */
static void apply_effective_color(clock_widget_t *w)
{
    lv_color_t c = (w->mode == CLOCK_MODE_FULL) ? w->color : w->min_color;
    lv_obj_set_style_text_color(w->lbl_time, c, 0);
    lv_obj_set_style_text_color(w->lbl_ampm, c, 0);
    lv_obj_set_style_text_color(w->lbl_date, c, 0);
    lv_color_t sc = ui_text_on(c);   // black under light text, white under dark text
    lv_obj_set_style_text_color(w->sh_time, sc, 0);
    lv_obj_set_style_text_color(w->sh_ampm, sc, 0);
    lv_obj_set_style_text_color(w->sh_date, sc, 0);
}

/**
 * @brief Drop the snapshot cache entry, destroying its buffer unless a morph or crossfade still
 *        shows it (that owner then frees it, since it is no longer the cache).
 * @version 1
 * @dg_internal
 */
static void snap_cache_release(void)
{
    lv_draw_buf_t *old = s_snap_cache;
    s_snap_cache = NULL;
    s_snap_text[0] = '\0';
    if (old && old != s_morph_snap && old != s_fade_snap) lv_draw_buf_destroy(old);
}

/**
 * @brief Return the white FULL-layout time snapshot for the current time text: the cached one
 *        when the text is unchanged, else render (and cache) a new one. Leaves the labels in
 *        FULL layout, white, with AM/PM, date and shadows hidden on a render.
 * v2: records where the time label sits inside the snapshot (handoff measurement).
 * @version 2
 * @req REQ-DISP-004
 * @return Snapshot buffer (owned by the cache), or NULL if rendering failed (OOM).
 */
static lv_draw_buf_t *morph_snapshot_get(clock_widget_t *w)
{
    const char *text = lv_label_get_text(w->lbl_time);
    if (s_snap_cache && strcmp(text, s_snap_text) == 0) return s_snap_cache;

    // Render the real clock at FULL (256pt) in WHITE and snapshot it once. The morph
    // scales that static bitmap (no per-frame re-raster) and recolours it, so one white
    // snapshot serves every colour (white -> recolour keeps glyph AA). AM/PM + date stay
    // OUT of it: they reflow between layouts and crossfade separately.
    int64_t t0 = esp_timer_get_time();
    snap_to(w, CLOCK_MODE_FULL);
    apply_full_layout(w);
    lv_color_t white = lv_color_white();
    lv_obj_set_style_text_color(w->lbl_time, white, 0);
    lv_obj_set_style_text_color(w->lbl_ampm, white, 0);
    lv_obj_set_style_text_color(w->lbl_date, white, 0);
    lv_obj_add_flag(w->lbl_ampm, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(w->lbl_date, LV_OBJ_FLAG_HIDDEN);
    shadows_hide(w);   // the snapshot is recoloured wholesale: no shadows in it
    lv_obj_update_layout(w->container);
    lv_draw_buf_t *snap = lv_snapshot_take(w->container, LV_COLOR_FORMAT_ARGB8888);
    lv_area_t tc, cc;
    lv_obj_get_coords(w->lbl_time, &tc);
    lv_obj_get_coords(w->container, &cc);
    lv_area_set(&s_snap_time_rel, tc.x1 - cc.x1, tc.y1 - cc.y1, tc.x2 - cc.x1, tc.y2 - cc.y1);
    ESP_LOGI(TAG, "morph snapshot rendered for '%s' in %lld us (cache miss)", text,
             (long long)(esp_timer_get_time() - t0));
    if (!snap) return NULL;
    snap_cache_release();
    s_snap_cache = snap;
    strlcpy(s_snap_text, text, sizeof(s_snap_text));
    return snap;
}

/**
 * @brief Switch clock mode; animates a 500 ms snapshot morph (time scaled and recoloured, AM/PM and date crossfaded), or snaps instantly if the snapshot cannot be taken.
 * v2: keeps the drop shadows out of the morph snapshot and in sync around it.
 * v3: reuses the cached time snapshot (morph_snapshot_get) instead of re-rendering it every morph.
 * v4: arms the morph refresh probe (frame rate measured per morph, see morph_report).
 * @version 4
 * @req REQ-CLOCK-002
 * @req REQ-DISP-004
 */
void clock_widget_set_mode(clock_widget_t *w, clock_mode_t mode)
{
    if (!w) return;
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

    // Effective colours for each end of the morph (computed BEFORE the snapshot so
    // the recolour animates old→new). Minimized defaults to min_base; states whose
    // own content covers the bg (radar/photos) set min_base before minimizing.
    if (mode == CLOCK_MODE_MINIMIZED) w->min_color = w->min_base;
    s_mc0 = (old_mode == CLOCK_MODE_FULL) ? w->color : w->min_color;
    s_mc1 = (mode     == CLOCK_MODE_FULL) ? w->color : w->min_color;

    // White FULL-layout time snapshot: cached per time text (re-rendered once a minute).
    s_morph_snap = morph_snapshot_get(w);

    lv_obj_remove_flag(w->lbl_ampm, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(w->lbl_date, LV_OBJ_FLAG_HIDDEN);
    w->mode = mode;
    if (!s_morph_snap) {
        // Snapshot failed (OOM) — fall back to an instant snap, no animation.
        snap_to(w, mode);
        apply_layout_for_mode(w, mode);
        apply_effective_color(w);
        shadows_sync(w);
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
    lv_anim_set_duration(&lf, 180);
    lv_anim_set_values(&lf, LV_OPA_COVER, LV_OPA_TRANSP);
    lv_anim_set_exec_cb(&lf, fade_opa_cb);
    lv_anim_set_var(&lf, w->lbl_ampm); lv_anim_start(&lf);
    lv_anim_set_var(&lf, w->lbl_date); lv_anim_start(&lf);
    shadows_sync(w);   // source layout; the hidden time's shadow stays hidden

    s_morph_img = lv_image_create(lv_obj_get_parent(w->container));
    lv_image_set_src(s_morph_img, s_morph_snap);
    lv_image_set_pivot(s_morph_img, 0, 0);            // scale about top-left
    lv_obj_set_style_image_recolor_opa(s_morph_img, LV_OPA_COVER, 0);
    lv_obj_remove_flag(s_morph_img, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    // Endpoints aligned so the TIME text matches the real clock at each mode.
    morph_appearance(old_mode, &s_mx0, &s_my0, &s_msc0);
    morph_appearance(mode,     &s_mx1, &s_my1, &s_msc1);
    s_morph_w      = w;
    s_morph_target = mode;
    morph_exec_cb(s_morph_img, 0);   // source appearance immediately (no flash)

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_morph_img);
    lv_anim_set_duration(&a, 500);
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
 * @version 1
 * @req REQ-BG-002
 * @return Stored user colour, or white if @p w is NULL.
 */
lv_color_t clock_widget_user_color(const clock_widget_t *w)
{
    return w ? w->user_color : lv_color_white();
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
