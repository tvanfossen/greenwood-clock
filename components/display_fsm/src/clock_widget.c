// components/display_fsm/src/clock_widget.c
//
// Layer 1: ClockWidget — reusable time/date display with full/minimized modes.
// Extracted from components/ui/ui.c clock label management.
//
// Caller must hold LVGL lock for all functions.

#include "display_widgets.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "clock_widget";

LV_FONT_DECLARE(nunito_48);
LV_FONT_DECLARE(nunito_128);
LV_FONT_DECLARE(nunito_256);

// ---------------------------------------------------------------------------
// Internal struct
// ---------------------------------------------------------------------------

struct clock_widget_t {
    lv_obj_t    *container;     // invisible container for grouping
    lv_obj_t    *lbl_time;      // HH:MM
    lv_obj_t    *lbl_ampm;      // AM/PM
    lv_obj_t    *lbl_date;      // Day, Month DD
    clock_mode_t mode;
    lv_color_t   user_color;  // raw NVS text colour — the OKLCH base for all modes
    lv_color_t   color;       // FULL-mode colour (legible over the bg image)
    lv_color_t   min_color;   // MINIMIZED-mode colour (current; states may override)
    lv_color_t   min_base;    // default MINIMIZED colour (legible over the bg image,
                              // used by states that show the clock bg behind the clock)
};

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

static void format_time(const struct tm *ti, char *buf_time, size_t time_sz,
                        char *buf_ampm, size_t ampm_sz,
                        char *buf_date, size_t date_sz)
{
    int h12 = ti->tm_hour % 12;
    if (!h12) h12 = 12;
    snprintf(buf_time, time_sz, "%02d:%02d", h12, ti->tm_min);
    snprintf(buf_ampm, ampm_sz, "%s", ti->tm_hour < 12 ? "AM" : "PM");
    strftime(buf_date, date_sz, "%A, %B, %d", ti);
}

// Apply fonts and inner label alignment for full mode.
// Does NOT touch container position/size — that's handled by animation or snap_to().
static void apply_full_layout(clock_widget_t *w)
{
    // Time: 256pt, centered in container
    lv_obj_set_style_text_font(w->lbl_time, &nunito_256, 0);
    lv_obj_align(w->lbl_time, LV_ALIGN_TOP_MID, 0, 24);

    // AM/PM: 48pt, visual text-bottom aligned with time.
    // y_ofs = -(nunito_256.base_line - nunito_48.base_line) = -(53 - 10) = -43
    // Verified: LV_ALIGN_OUT_RIGHT_BOTTOM sets y = h(base)-h(obj)+y_ofs = 265-50-43 = 172
    //   time baseline  = 265 - 53 = 212
    //   ampm baseline  = 172 + 50 - 10 = 212  (same pixel row)
    lv_obj_set_style_text_font(w->lbl_ampm, &nunito_48, 0);
    lv_obj_align_to(w->lbl_ampm, w->lbl_time, LV_ALIGN_OUT_RIGHT_BOTTOM, 8, -43);

    // Date: 48pt, bottom center
    lv_obj_set_style_text_font(w->lbl_date, &nunito_48, 0);
    lv_obj_align(w->lbl_date, LV_ALIGN_BOTTOM_MID, 0, -16);

    lv_obj_clear_flag(w->container, LV_OBJ_FLAG_HIDDEN);
}

// Apply fonts and inner label alignment for minimized mode.
// Does NOT touch container position/size — that's handled by animation or snap_to().
static void apply_minimized_layout(clock_widget_t *w)
{
    // Time: 128pt. Shift left so the time + AM/PM *group* is centred in the
    // container — otherwise centring the time alone pushes the 48pt AM/PM past
    // the right screen edge (only "P"/"A" visible).
    lv_obj_set_style_text_font(w->lbl_time, &nunito_128, 0);
    lv_obj_align(w->lbl_time, LV_ALIGN_TOP_MID, -40, 0);

    // AM/PM: 48pt, visual text-bottom aligned with time.
    // y_ofs = -(nunito_128.base_line - nunito_48.base_line) = -(27 - 10) = -17
    // Verified: y = h(base)-h(obj)+y_ofs = 133-50-17 = 66
    //   time baseline  = 133 - 27 = 106
    //   ampm baseline  = 66 + 50 - 10 = 106  (same pixel row)
    lv_obj_set_style_text_font(w->lbl_ampm, &nunito_48, 0);
    lv_obj_align_to(w->lbl_ampm, w->lbl_time, LV_ALIGN_OUT_RIGHT_BOTTOM, 8, -17);

    // Date: 24pt, below time
    lv_obj_set_style_text_font(w->lbl_date, &lv_font_montserrat_24, 0);
    lv_obj_align(w->lbl_date, LV_ALIGN_BOTTOM_MID, 0, -4);

    lv_obj_clear_flag(w->container, LV_OBJ_FLAG_HIDDEN);
}

// Apply fonts and inner label alignment for topbar mode (thin strip, settings use).
// Does NOT touch container position/size — that's handled by animation or snap_to().
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

void clock_widget_full_area(lv_area_t *out)
{
    if (!out) return;
    out->x1 = FULL_X;
    out->y1 = FULL_Y;
    out->x2 = FULL_X + FULL_W - 1;
    out->y2 = FULL_Y + FULL_H - 1;
}

// Snap container to a mode's target position/size immediately (no animation).
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

    // Time label
    w->lbl_time = lv_label_create(w->container);
    lv_obj_set_style_text_color(w->lbl_time, w->color, 0);
    lv_obj_set_style_text_align(w->lbl_time, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(w->lbl_time, "12:00");

    // AM/PM label
    w->lbl_ampm = lv_label_create(w->container);
    lv_obj_set_style_text_color(w->lbl_ampm, w->color, 0);
    lv_label_set_text(w->lbl_ampm, "PM");

    // Date label
    w->lbl_date = lv_label_create(w->container);
    lv_obj_set_style_text_color(w->lbl_date, w->color, 0);
    lv_obj_set_style_text_align(w->lbl_date, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(w->lbl_date, "");

    apply_full_layout(w);
    snap_to(w, CLOCK_MODE_FULL);
    ESP_LOGI(TAG, "ClockWidget created");
    return w;
}

void clock_widget_destroy(clock_widget_t *w)
{
    if (!w) return;
    kill_fade();   // drop any in-flight crossfade snapshot before teardown
    if (w->container) {
        lv_anim_delete(w->container, NULL);
        lv_obj_delete(w->container);
    }
    lv_free(w);
    ESP_LOGI(TAG, "ClockWidget destroyed");
}

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

static uint8_t lerp_u8(uint8_t a, uint8_t b, int32_t prog)
{
    return (uint8_t)((int32_t)a + ((int32_t)b - (int32_t)a) * prog / 256);
}

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

static void fade_opa_cb(void *var, int32_t v)
{
    lv_obj_set_style_opa((lv_obj_t *)var, (lv_opa_t)v, 0);
}

// Tear down any in-flight crossfade immediately (its anim, image, and buffer).
static void kill_fade(void)
{
    if (s_fade_img) {
        lv_anim_delete(s_fade_img, NULL);
        lv_obj_delete(s_fade_img);
        s_fade_img = NULL;
    }
    if (s_fade_snap) { lv_draw_buf_destroy(s_fade_snap); s_fade_snap = NULL; }
}

static void fade_done_cb(lv_anim_t *a)
{
    LV_UNUSED(a);
    kill_fade();
}

// Restore the real clock at the target layout, then dispose of the morph
// snapshot image — by dissolving it out (crossfade=true, natural completion) or
// instantly (crossfade=false, an interrupting morph settling this one). Idempotent.
// Strictly serial: an interrupted morph can never orphan its snapshot on the layer.
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
        if (s_morph_snap) lv_draw_buf_destroy(s_morph_snap);
    }
    s_morph_img  = NULL;
    s_morph_snap = NULL;
    s_morph_w    = NULL;
}

static void morph_done_cb(lv_anim_t *a)
{
    LV_UNUSED(a);
    finalize_morph(true);   // natural completion → dissolve the snapshot out
}

// Get position/size for a mode (avoids repeating constants in animation logic).
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
static void apply_effective_color(clock_widget_t *w)
{
    lv_color_t c = (w->mode == CLOCK_MODE_FULL) ? w->color : w->min_color;
    lv_obj_set_style_text_color(w->lbl_time, c, 0);
    lv_obj_set_style_text_color(w->lbl_ampm, c, 0);
    lv_obj_set_style_text_color(w->lbl_date, c, 0);
}

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

    // Render the real clock at FULL (256pt) in WHITE and snapshot it once. The
    // morph scales that static bitmap (no per-frame re-raster) and recolours it,
    // so one white snapshot serves every colour (white→recolour keeps glyph AA).
    snap_to(w, CLOCK_MODE_FULL);
    apply_full_layout(w);
    lv_color_t white = lv_color_white();
    lv_obj_set_style_text_color(w->lbl_time, white, 0);
    lv_obj_set_style_text_color(w->lbl_ampm, white, 0);
    lv_obj_set_style_text_color(w->lbl_date, white, 0);
    // Keep AM/PM + date OUT of the morph snapshot. They reflow between layouts, so
    // scaling them uniformly with the time lands them in the wrong place and they
    // ghost. Snapshot the time alone; the real AM/PM + date crossfade separately.
    lv_obj_add_flag(w->lbl_ampm, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(w->lbl_date, LV_OBJ_FLAG_HIDDEN);
    lv_obj_update_layout(w->container);

    s_morph_snap = lv_snapshot_take(w->container, LV_COLOR_FORMAT_ARGB8888);

    lv_obj_remove_flag(w->lbl_ampm, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(w->lbl_date, LV_OBJ_FLAG_HIDDEN);
    w->mode = mode;
    if (!s_morph_snap) {
        // Snapshot failed (OOM) — fall back to an instant snap, no animation.
        snap_to(w, mode);
        apply_layout_for_mode(w, mode);
        apply_effective_color(w);
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

    static const char *mode_names[] = {"FULL", "MINIMIZED", "TOPBAR"};
    ESP_LOGI(TAG, "ClockWidget mode → %s (snapshot morph)", mode_names[mode]);
}

void clock_widget_set_color(clock_widget_t *w, lv_color_t color)
{
    if (!w) return;
    w->color = color;
    apply_effective_color(w);   // honours current mode (only FULL uses it)
}

void clock_widget_set_minimized_color(clock_widget_t *w, lv_color_t color)
{
    if (!w) return;
    w->min_color = color;
    apply_effective_color(w);   // applies immediately if currently minimized
}

void clock_widget_set_minimized_base_color(clock_widget_t *w, lv_color_t color)
{
    if (w) w->min_base = color;   // default applied on the next minimize
}

void clock_widget_set_user_color(clock_widget_t *w, lv_color_t color)
{
    if (w) w->user_color = color;   // OKLCH base hue; effective colours derived from it
}

lv_color_t clock_widget_user_color(const clock_widget_t *w)
{
    return w ? w->user_color : lv_color_white();
}

void clock_widget_min_area(lv_area_t *out)
{
    if (!out) return;
    out->x1 = MIN_X;
    out->y1 = MIN_Y;
    out->x2 = MIN_X + MIN_W - 1;
    out->y2 = MIN_Y + MIN_H - 1;
}

clock_mode_t clock_widget_get_mode(const clock_widget_t *w)
{
    return w ? w->mode : CLOCK_MODE_FULL;
}

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
