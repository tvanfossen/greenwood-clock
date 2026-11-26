/**
 * @file settings_library.c
 * @version 1
 * @brief Settings > Library (see settings_library.h): kinds, the kind's grid or list, an
 *        item's detail, Use in a profile. Pictures come a page at a time (PAGE thumbnails),
 *        loaded one by one so the screen stays responsive.
 */
#include "settings_library.h"
#include "settings_app.h"
#include "settings_frame.h"
#include "settings_theme.h"
#include "library_thumb.h"
#include "sched_rule.h"
#include "sched_json.h"
#include "src/misc/cache/instance/lv_image_cache.h"
#include "ui_theme_bind.h"

#include <stdio.h>
#include <string.h>
#include <time.h>

#define PAGE        12    // thumbnails a page
#define THUMB_MS    30    // between thumbnail requests
#define WHY_LEN     128

/** The kind on show and its items. */
typedef struct {
    lib_kind_t  kind;
    lib_item_t *items;          /**< lv_malloc'd; LIB_ALBUM_MAX of them. */
    int         n;
    bool        loaded;
    bool        asked;          /**< A list request is in flight. */
    char        why[WHY_LEN];   /**< The list failed: why. */
    uint32_t    total_mb, free_mb;
    int         page;
    int         sel;            /**< The item the detail shows. */
    unsigned    list_gen;       /**< Bumped when the kind screen goes: late answers dropped. */
    unsigned    grid_gen;       /**< Likewise for the thumbnails of one grid. */
    lv_obj_t   *imgs[PAGE];     /**< The grid's images, waiting for thumbnails. */
    int         cells;
    int         next;           /**< The next cell to ask a thumbnail for. */
    bool        busy;           /**< A thumbnail request is in flight. */
    lv_timer_t *timer;
    bool        confirm;        /**< Delete was tapped once. */
} lib_view_t;

static lib_view_t s_v;
static sched_rule_t *s_rules;   // the profiles, read as needed (allocated with the items)

static const char *const KIND_TITLES[LIB_KINDS] = { "Backgrounds", "Album", "Themes", "Animations" };

static lv_obj_t *kind_screen(lv_obj_t *parent, void *ctx);
static lv_obj_t *detail_screen(lv_obj_t *parent, void *ctx);

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/**
 * @brief Whether the kind is pictures (a thumbnail grid).
 * @version 1
 * @utility
 * @return Whether.
 */
static bool pictures(lib_kind_t kind)
{
    return kind == LIB_BACKGROUNDS || kind == LIB_ALBUM;
}

/**
 * @brief The item on show in the detail.
 * @version 1
 * @utility
 * @return It.
 */
static const lib_item_t *sel_item(void)
{
    return &s_v.items[s_v.sel];
}

/**
 * @brief Who uses @p it, as "Used by Morning, Night" (or "Not used by a profile").
 * @version 1
 * @utility
 * @return How many use it.
 */
static int used_text(const lib_item_t *it, char *out, size_t n)
{
    char ref[SCHED_IMAGE_LEN];
    const char *who[4];
    lib_ref(s_v.kind, it->name, ref, sizeof(ref));
    int r = settings_app_backend()->schedule_get(s_rules, SCHED_MAX_PROFILES);
    int k = lib_used_by(s_rules, r, s_v.kind, ref, who, 4);
    size_t at = (size_t)snprintf(out, n, k ? "Used by %s" : "Not used by a profile", k ? who[0] : "");
    for (int i = 1; i < k && i < 4 && at < n; i++) at += (size_t)snprintf(out + at, n - at, ", %s", who[i]);
    if (k > 4 && at < n) snprintf(out + at, n - at, " and %d more", k - 4);
    return k;
}

/**
 * @brief Show the kind's list again from the device (after a change), from @p pops screens up.
 * @version 1
 * @utility
 */
static void reload(int pops)
{
    s_v.loaded = false;
    for (int i = 0; i < pops; i++) settings_app_pop();
    settings_app_refresh();
}

/**
 * @brief A change finished: say so and show the kind again, or say why not.
 * @version 1
 * @callback
 */
static void changed_cb(bool ok, const char *why, void *ctx)
{
    settings_app_toast(ok ? (const char *)ctx : (why && why[0] ? why : "That did not work"), false);
    if (ok) reload(1);
}

// ---------------------------------------------------------------------------
// The kind's list
// ---------------------------------------------------------------------------

/**
 * @brief Show the kind's list now that it is here (if its screen is still the one asking).
 * @version 1
 * @callback
 */
static void show_listed(void *ctx)
{
    if ((unsigned)(uintptr_t)ctx == s_v.list_gen) settings_app_refresh();
}

/**
 * @brief The device answered the list: keep it and show it (if the screen is still there);
 *        the screen is built again on the next cycle, as the answer may come while it is
 *        still being built (a device that answers at once).
 * @version 1
 * @callback
 */
static void listed_cb(bool ok, const char *why, const lib_item_t *items, int n,
                      uint32_t total_mb, uint32_t free_mb, void *ctx)
{
    if ((unsigned)(uintptr_t)ctx != s_v.list_gen) return;   // the screen went
    s_v.asked = false;
    s_v.loaded = true;
    s_v.n = ok && s_v.items ? LV_MIN(n, LIB_ALBUM_MAX) : 0;
    if (s_v.n) memcpy(s_v.items, items, sizeof(lib_item_t) * (size_t)s_v.n);
    snprintf(s_v.why, sizeof(s_v.why), "%s", ok ? "" : (why ? why : "The clock did not answer"));
    s_v.total_mb = total_mb;
    s_v.free_mb = free_mb;
    s_v.page = LV_MIN(s_v.page, s_v.n ? (s_v.n - 1) / PAGE : 0);
    lv_async_call(show_listed, ctx);
}

/**
 * @brief The kind screen went: late answers are dropped, the thumbnails stop.
 * @version 1
 * @callback
 */
static void kind_deleted_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    s_v.list_gen++;
    s_v.asked = false;
}

/**
 * @brief A grid went: its thumbnails stop.
 * @version 1
 * @callback
 */
static void grid_deleted_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    s_v.grid_gen++;
    s_v.cells = 0;
    s_v.busy = false;
    if (s_v.timer) lv_timer_delete(s_v.timer);
    s_v.timer = NULL;
}

/**
 * @brief An image went: free its thumbnail.
 * @version 1
 * @callback
 */
static void img_deleted_cb(lv_event_t *e)
{
    lv_image_dsc_t *d = lv_event_get_user_data(e);
    lv_image_cache_drop(d);
    library_thumb_free(d);
}

/**
 * @brief Show thumbnail @p d in @p img (which frees it when it goes).
 * @version 1
 * @utility
 */
static void set_thumb(lv_obj_t *img, lv_image_dsc_t *d)
{
    lv_image_set_src(img, d);
    lv_obj_add_event_cb(img, img_deleted_cb, LV_EVENT_DELETE, d);
}

/**
 * @brief A grid thumbnail arrived: show it in its cell (the grid may have gone).
 * @version 1
 * @callback
 */
static void grid_thumb_cb(lv_image_dsc_t *thumb, void *ctx)
{
    uintptr_t c = (uintptr_t)ctx;
    bool live = (c >> 8) == (s_v.grid_gen & 0xFFFFFF) && (int)(c & 0xFF) < s_v.cells;
    if (live && thumb) set_thumb(s_v.imgs[c & 0xFF], thumb);
    else library_thumb_free(thumb);
    if (live) s_v.busy = false;
}

/**
 * @brief Ask for the next cell's thumbnail (one at a time).
 * @version 1
 * @callback
 */
static void thumb_timer_cb(lv_timer_t *t)
{
    if (s_v.busy) return;
    if (s_v.next >= s_v.cells) {
        lv_timer_pause(t);
        return;
    }
    int cell = s_v.next++;
    const lib_item_t *it = &s_v.items[s_v.page * PAGE + cell];
    if (!it->thumb) return;
    s_v.busy = true;
    uintptr_t c = ((uintptr_t)(s_v.grid_gen & 0xFFFFFF) << 8) | (uintptr_t)cell;
    settings_app_backend()->library_thumb(s_v.kind, it->name, grid_thumb_cb, (void *)c);
}

/**
 * @brief An item tapped: its detail.
 * @version 1
 * @callback
 */
static void item_cb(lv_event_t *e)
{
    s_v.sel = (int)(intptr_t)lv_event_get_user_data(e);
    s_v.confirm = false;
    settings_app_push(detail_screen, NULL);
}

/**
 * @brief Prev / Next page.
 * @version 1
 * @callback
 */
static void page_cb(lv_event_t *e)
{
    int pages = (s_v.n + PAGE - 1) / PAGE;
    s_v.page = (s_v.page + (int)(intptr_t)lv_event_get_user_data(e) + pages) % pages;
    settings_app_refresh();
}

/**
 * @brief An image @p w wide in the clock's shape, waiting for its thumbnail.
 * @version 1
 * @utility
 * @return It.
 */
static lv_obj_t *picture(lv_obj_t *parent, int32_t w)
{
    lv_obj_t *img = lv_image_create(parent);
    lv_obj_set_size(img, w, w * LIB_THUMB_H / LIB_THUMB_W);
    lv_image_set_inner_align(img, LV_IMAGE_ALIGN_STRETCH);
    lv_obj_set_style_radius(img, 6, 0);
    lv_obj_set_style_clip_corner(img, true, 0);
    ui_bind_fill_role(img, UI_ROLE_SURFACE_INSET);   // until it arrives
    lv_obj_set_style_bg_opa(img, LV_OPA_COVER, 0);
    return img;
}

/**
 * @brief One cell of the grid: the thumbnail and the name, @p w wide.
 * @version 1
 * @utility
 */
static void grid_cell(lv_obj_t *grid, int i, int32_t w)
{
    lv_obj_t *b = lv_obj_create(grid);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, w, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(b, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(b, 4, 0);
    lv_obj_add_flag(b, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(b, item_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    s_v.imgs[s_v.cells++] = picture(b, w);
    lv_obj_remove_flag(s_v.imgs[s_v.cells - 1], LV_OBJ_FLAG_CLICKABLE);
    lv_obj_t *l = settings_text(b, s_v.items[i].label, &lv_font_montserrat_16, UI_ROLE_TEXT_MUTED);
    settings_one_line(l, w);
}

/**
 * @brief The page's thumbnails in @p col, and Prev / Next when there are more pages.
 * @version 1
 * @utility
 */
static void picture_grid(lv_obj_t *col)
{
    int cols = settings_narrow() ? 2 : 4;
    int32_t gap = 12, w = (settings_content_w() - 16 - gap * (cols - 1)) / cols;
    lv_obj_t *grid = lv_obj_create(col);
    lv_obj_remove_style_all(grid);
    lv_obj_set_size(grid, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_gap(grid, gap, 0);
    lv_obj_add_event_cb(grid, grid_deleted_cb, LV_EVENT_DELETE, NULL);
    s_v.cells = 0;
    s_v.next = 0;
    s_v.busy = false;
    for (int i = s_v.page * PAGE; i < s_v.n && i < (s_v.page + 1) * PAGE; i++) grid_cell(grid, i, w);
    s_v.timer = lv_timer_create(thumb_timer_cb, THUMB_MS, NULL);
    if (s_v.n <= PAGE) return;
    char at[32];
    lv_snprintf(at, sizeof(at), "Page %d of %d", s_v.page + 1, (s_v.n + PAGE - 1) / PAGE);
    lv_obj_t *v = settings_field(col, at);
    settings_button(v, LV_SYMBOL_LEFT, page_cb, (void *)(intptr_t)-1);
    settings_button(v, LV_SYMBOL_RIGHT, page_cb, (void *)(intptr_t)1);
}

/**
 * @brief How many profiles use @p it, as a short note ("2 profiles", "Unused").
 * @version 1
 * @utility
 */
static void used_count(const lib_item_t *it, char *out, size_t n)
{
    char ref[SCHED_IMAGE_LEN];
    const char *who[1];
    lib_ref(s_v.kind, it->name, ref, sizeof(ref));
    int r = settings_app_backend()->schedule_get(s_rules, SCHED_MAX_PROFILES);
    int k = lib_used_by(s_rules, r, s_v.kind, ref, who, 1);
    if (k) lv_snprintf(out, n, "%d profile%s", k, k == 1 ? "" : "s");
    else lv_snprintf(out, n, "Unused");
}

/**
 * @brief Themes and animations as a list: the name, and how many profiles use it.
 * @version 1
 * @utility
 */
static void name_list(lv_obj_t *col)
{
    lv_obj_t *list = lv_list_create(col);
    lv_obj_set_size(list, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_style_text_font(list, &lv_font_montserrat_20, 0);
    const char *icon = s_v.kind == LIB_THEMES ? LV_SYMBOL_TINT : LV_SYMBOL_PLAY;
    for (int i = 0; i < s_v.n; i++) {
        char used[32];
        used_count(&s_v.items[i], used, sizeof(used));
        lv_obj_t *b = lv_list_add_button(list, icon, s_v.items[i].label);
        lv_obj_add_event_cb(b, item_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        lv_obj_t *note = settings_text(b, used, &lv_font_montserrat_16, UI_ROLE_TEXT_MUTED);
        lv_obj_add_flag(note, LV_OBJ_FLAG_IGNORE_LAYOUT);
        lv_obj_align(note, LV_ALIGN_RIGHT_MID, 0, 0);
    }
}

/**
 * @brief How full the kind and the card are: "37 of 200 photos", the free space, a meter.
 * @version 1
 * @utility
 */
static void summary(lv_obj_t *col)
{
    char s[96];
    const char *what = KIND_TITLES[s_v.kind];
    if (s_v.kind == LIB_ALBUM) lv_snprintf(s, sizeof(s), "%d of %d photos", s_v.n, LIB_ALBUM_MAX);
    else lv_snprintf(s, sizeof(s), "%d %s", s_v.n, what);
    lv_obj_t *v = settings_field(col, s);
    if (!s_v.total_mb || s_v.kind == LIB_ANIMS) return;   // built in: no space of their own
    char free_s[48];
    lv_snprintf(free_s, sizeof(free_s), "%lu.%lu GB free", (unsigned long)(s_v.free_mb / 1024),
                (unsigned long)(s_v.free_mb % 1024 * 10 / 1024));
    lv_obj_t *bar = lv_bar_create(v);
    lv_obj_set_size(bar, settings_narrow() ? 90 : 180, 12);
    lv_bar_set_range(bar, 0, 1000);
    lv_bar_set_value(bar, (int32_t)((uint64_t)(s_v.total_mb - s_v.free_mb) * 1000 / s_v.total_mb), LV_ANIM_OFF);
    ui_bind_fill_role(bar, UI_ROLE_BORDER);
    lv_obj_set_style_bg_color(bar, ui_theme_color(UI_ROLE_ACCENT), LV_PART_INDICATOR);
    settings_text(v, free_s, &lv_font_montserrat_16, UI_ROLE_TEXT_MUTED);
}

/**
 * @brief An upload finished: show the kind again (its thumbnail comes as the clock makes it).
 * v2: says how it went ("Sent: x.png"), now that the Library is the only way to add.
 * @version 2
 * @callback
 */
static void uploaded_cb(bool ok, const char *why, void *ctx)
{
    LV_UNUSED(ctx);
    bool say = why && why[0] && strcmp(why, "Cancelled") != 0;
    if (say) settings_app_toast(why, false);   // "Sent: x.png", or why not
    if (ok) reload(0);
}

/**
 * @brief A new theme was saved: show the themes again.
 * @version 1
 * @callback
 */
static void theme_new_cb(const char *stem, void *ctx)
{
    LV_UNUSED(stem);
    LV_UNUSED(ctx);
    reload(0);
}

/**
 * @brief Upload (pictures) or New (themes), from the kind's title row.
 * @version 1
 * @callback
 */
static void add_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    const settings_backend_t *be = settings_app_backend();
    if (s_v.kind == LIB_THEMES) settings_theme_customize(theme_new_cb, NULL);
    else be->upload(s_v.kind == LIB_ALBUM ? SETTINGS_UPLOAD_PHOTO : SETTINGS_UPLOAD_BACKGROUND, uploaded_cb, NULL);
}

/**
 * @brief The title row's add button for the kind, when the device can add one.
 * @version 1
 * @utility
 */
static void add_button(lv_obj_t *frame)
{
    bool upload = pictures(s_v.kind) && settings_app_backend()->upload;
    if (upload) settings_title_button(frame, LV_SYMBOL_UPLOAD " Add", add_cb, NULL);
    if (s_v.kind == LIB_THEMES) settings_title_button(frame, LV_SYMBOL_PLUS " New", add_cb, NULL);
}

/**
 * @brief The kind's screen: loading, why not, or its summary and items.
 * @version 1
 * @utility
 * @return The frame.
 */
static lv_obj_t *kind_screen(lv_obj_t *parent, void *ctx)
{
    LV_UNUSED(ctx);
    lv_obj_t *frame = settings_frame_create(parent, KIND_TITLES[s_v.kind], settings_app_back_cb);
    lv_obj_add_event_cb(frame, kind_deleted_cb, LV_EVENT_DELETE, NULL);
    lv_obj_t *col = settings_column(frame);
    if (!s_v.loaded) {
        settings_text(col, "Looking...", &lv_font_montserrat_20, UI_ROLE_TEXT_MUTED);
        s_v.asked = true;
        unsigned gen = ++s_v.list_gen;   // this screen's (its delete bumps it again)
        settings_app_backend()->library_list(s_v.kind, listed_cb, (void *)(uintptr_t)gen);
        return frame;
    }
    add_button(frame);
    if (s_v.why[0]) settings_banner(col, s_v.why, UI_SLOT_ALERT_SEVERE);
    summary(col);
    if (pictures(s_v.kind)) picture_grid(col);
    else name_list(col);
    if (!s_v.n && !s_v.why[0]) settings_text(col, "Nothing here yet", &lv_font_montserrat_20, UI_ROLE_TEXT_MUTED);
    return frame;
}

// ---------------------------------------------------------------------------
// An item's detail
// ---------------------------------------------------------------------------

/**
 * @brief The detail's thumbnail arrived.
 * @version 1
 * @callback
 */
static void detail_thumb_cb(lv_image_dsc_t *thumb, void *ctx)
{
    lv_obj_t *img = ctx;
    bool live = lv_obj_is_valid(img) && thumb;
    if (live) set_thumb(img, thumb);
    else library_thumb_free(thumb);
}

/**
 * @brief Delete: a first tap asks again, the second deletes.
 * @version 1
 * @callback
 */
static void delete_cb(lv_event_t *e)
{
    if (!s_v.confirm) {
        s_v.confirm = true;
        lv_label_set_text(lv_obj_get_child(lv_event_get_target_obj(e), 0), LV_SYMBOL_TRASH "  Tap again to delete");
        return;
    }
    settings_app_backend()->library_delete(s_v.kind, sel_item()->name, changed_cb, (void *)"Deleted");
}

/**
 * @brief A new name typed: rename.
 * @version 1
 * @callback
 */
static void renamed_cb(const char *value, void *ctx)
{
    LV_UNUSED(ctx);
    if (!lib_name_ok(value)) {
        settings_app_toast("Letters, digits, - and _ only", false);
        return;
    }
    settings_app_backend()->library_rename(s_v.kind, sel_item()->name, value, changed_cb, (void *)"Renamed");
}

/**
 * @brief Rename: ask the new name.
 * @version 1
 * @callback
 */
static void rename_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    settings_app_enter_text_max("New name", sel_item()->name, LIB_NAME_MAX, renamed_cb, NULL);
}

/**
 * @brief A theme was saved from the editor or as a copy: show the themes again.
 * @version 1
 * @callback
 */
static void theme_kept_cb(const char *stem, void *ctx)
{
    LV_UNUSED(stem);
    LV_UNUSED(ctx);
    reload(1);
}

/**
 * @brief A copy's name typed: keep the theme under it.
 * @version 1
 * @callback
 */
static void copy_named_cb(const char *value, void *ctx)
{
    ui_theme_t *t = ctx;
    char stem[SCHED_THEME_LEN];
    if (!sched_theme_stem(value, stem, sizeof(stem))) {
        settings_app_toast("Name it with letters or digits (not \"Default\")", false);
        return;
    }
    snprintf(t->name, sizeof(t->name), "%s", value);
    settings_app_backend()->save_theme(t, stem, changed_cb, (void *)"Theme copied");
}

/**
 * @brief The theme file arrived: edit it, or name its copy (@p ctx: 1 duplicates).
 * @version 1
 * @callback
 */
static void theme_loaded_cb(bool ok, const ui_theme_t *t, void *ctx)
{
    static ui_theme_t s_copy;
    char name[sizeof(s_copy.name) + 8];
    if (!ok) settings_app_toast("Could not read that theme", false);
    else if (!ctx) settings_theme_edit(t, sel_item()->name, theme_kept_cb, NULL);
    if (!ok || !ctx) return;
    s_copy = *t;
    lv_snprintf(name, sizeof(name), "%s 2", t->name);
    settings_app_enter_text("Name of the copy", name, copy_named_cb, &s_copy);
}

/**
 * @brief Edit or Duplicate a theme (@p user: 1 duplicates).
 * @version 1
 * @callback
 */
static void theme_cb(lv_event_t *e)
{
    settings_app_backend()->theme_load(sel_item()->name, theme_loaded_cb, lv_event_get_user_data(e));
}

/**
 * @brief A profile picked: it uses the item now.
 * @version 1
 * @callback
 */
static void use_cb(lv_event_t *e)
{
    int p = (int)(intptr_t)lv_event_get_user_data(e);
    const settings_backend_t *be = settings_app_backend();
    int n = be->schedule_get(s_rules, SCHED_MAX_PROFILES);
    static char msg[SCHED_NAME_LEN + 16];
    lv_snprintf(msg, sizeof(msg), "%s uses it now", s_rules[p].name);
    lib_use_in(&s_rules[p], s_v.kind, sel_item()->name);
    settings_app_pop();
    be->schedule_set(s_rules, n, changed_cb, msg);
}

/**
 * @brief Use in a profile: the profiles to pick from.
 * @version 1
 * @utility
 * @return The frame.
 */
static lv_obj_t *use_screen(lv_obj_t *parent, void *ctx)
{
    LV_UNUSED(ctx);
    lv_obj_t *frame = settings_frame_create(parent, "Use in a profile", settings_app_back_cb);
    lv_obj_t *list = lv_list_create(frame);
    lv_obj_set_size(list, settings_content_w(), settings_content_h());
    lv_obj_align(list, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_text_font(list, &lv_font_montserrat_20, 0);
    int n = settings_app_backend()->schedule_get(s_rules, SCHED_MAX_PROFILES);
    char ref[SCHED_IMAGE_LEN];
    lib_ref(s_v.kind, sel_item()->name, ref, sizeof(ref));
    for (int i = 0; i < n; i++) {
        const char *who[1];
        bool uses = lib_used_by(&s_rules[i], 1, s_v.kind, ref, who, 1) > 0;   // marked: already
        lv_obj_t *b = lv_list_add_button(list, uses ? LV_SYMBOL_OK : LV_SYMBOL_BULLET, s_rules[i].name);
        if (uses) lv_obj_add_state(b, LV_STATE_CHECKED);
        lv_obj_add_event_cb(b, use_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }
    if (!n) lv_list_add_text(list, "No profiles yet: add one in Schedule");
    return frame;
}

/**
 * @brief Use in a profile tapped.
 * @version 1
 * @callback
 */
static void use_open_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    settings_app_push(use_screen, NULL);
}

/**
 * @brief The item's size and date, "1.2 MB · Nov 24, 2026", into @p out.
 * @version 1
 * @utility
 */
static void info_text(const lib_item_t *it, char *out, size_t n)
{
    char date[24] = "";
    time_t t = (time_t)it->mtime;
    struct tm tm;
    if (it->mtime && localtime_r(&t, &tm)) strftime(date, sizeof(date), "%b %d, %Y", &tm);
    if (it->size >= 1024 * 1024) {
        lv_snprintf(out, n, "%lu.%lu MB   %s", (unsigned long)(it->size >> 20),
                    (unsigned long)((it->size & 0xFFFFF) * 10 >> 20), date);
    } else {
        lv_snprintf(out, n, "%lu KB   %s", (unsigned long)((it->size + 1023) >> 10), date);
    }
}

/**
 * @brief The actions an item has, in @p col.
 * @version 1
 * @utility
 */
static void actions(lv_obj_t *col, bool used)
{
    bool fixed = s_v.kind == LIB_ANIMS || (s_v.kind == LIB_THEMES && strcmp(sel_item()->name, "default") == 0);
    lv_obj_t *v = settings_field(col, "");
    if (s_v.kind != LIB_ALBUM) settings_button(v, LV_SYMBOL_OK "  Use in a profile", use_open_cb, NULL);
    if (s_v.kind == LIB_THEMES && !fixed) settings_button(v, LV_SYMBOL_EDIT "  Edit", theme_cb, NULL);
    if (s_v.kind == LIB_THEMES) settings_button(v, LV_SYMBOL_COPY "  Duplicate", theme_cb, (void *)1);
    if (fixed) return;
    v = settings_field(col, "");
    settings_button(v, LV_SYMBOL_KEYBOARD "  Rename", rename_cb, NULL);
    lv_obj_t *del = settings_button(v, LV_SYMBOL_TRASH "  Delete", delete_cb, NULL);
    ui_bind_fill(del, UI_SLOT_ALERT_SEVERE);
    if (used) lv_obj_add_state(del, LV_STATE_DISABLED);   // a profile uses it: see who above
}

/**
 * @brief An item's detail: the picture, size and date, who uses it, and its actions.
 * @version 1
 * @utility
 * @return The frame.
 */
static lv_obj_t *detail_screen(lv_obj_t *parent, void *ctx)
{
    LV_UNUSED(ctx);
    const lib_item_t *it = sel_item();
    lv_obj_t *frame = settings_frame_create(parent, it->label, settings_app_back_cb);
    lv_obj_t *col = settings_column(frame);
    if (pictures(s_v.kind)) {
        lv_obj_t *img = picture(col, LV_MIN(settings_content_w() - 16, 2 * LIB_THUMB_W));
        if (it->thumb) settings_app_backend()->library_thumb(s_v.kind, it->name, detail_thumb_cb, img);
    }
    char used[WHY_LEN], info[64];
    int k = used_text(it, used, sizeof(used));
    if (s_v.kind != LIB_ANIMS) {
        info_text(it, info, sizeof(info));
        settings_text(col, info, &lv_font_montserrat_16, UI_ROLE_TEXT_MUTED);
    }
    if (s_v.kind != LIB_ALBUM) settings_text(col, used, &lv_font_montserrat_20, UI_ROLE_TEXT);
    actions(col, k > 0);
    return frame;
}

// ---------------------------------------------------------------------------
// The kinds
// ---------------------------------------------------------------------------

/**
 * @brief A kind tapped: its screen.
 * @version 1
 * @callback
 */
static void kind_cb(lv_event_t *e)
{
    s_v.kind = (lib_kind_t)(intptr_t)lv_event_get_user_data(e);
    s_v.loaded = false;
    s_v.page = 0;
    if (!s_v.items) s_v.items = lv_malloc(sizeof(lib_item_t) * LIB_ALBUM_MAX);
    if (!s_rules) s_rules = lv_malloc(sizeof(sched_rule_t) * SCHED_MAX_PROFILES);
    if (s_v.items && s_rules) settings_app_push(kind_screen, NULL);
    else settings_app_toast("Out of memory", false);
}

/**
 * @brief The Library's kinds.
 * @version 1
 * @req REQ-LIB-001
 * @return The frame.
 */
lv_obj_t *settings_library_screen(lv_obj_t *parent, void *ctx)
{
    LV_UNUSED(ctx);
    static const char *const ICONS[LIB_KINDS] = { LV_SYMBOL_IMAGE, LV_SYMBOL_DIRECTORY,
                                                  LV_SYMBOL_TINT, LV_SYMBOL_PLAY };
    settings_menu_item_t items[LIB_KINDS];
    for (int i = 0; i < LIB_KINDS; i++) {
        items[i] = (settings_menu_item_t){ ICONS[i], KIND_TITLES[i], kind_cb, (void *)(intptr_t)i };
    }
    lv_obj_t *frame = settings_frame_create(parent, "Library", settings_app_back_cb);
    settings_menu_grid(frame, items, LIB_KINDS);
    return frame;
}
