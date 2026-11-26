/**
 * @file settings_location.c
 * @version 1
 * @brief Settings > Location (see settings_location.h): the place and zone shown; an address
 *        looked up and confirmed; the zone picked by region and place.
 */
#include "settings_location.h"
#include "settings_app.h"
#include "settings_frame.h"
#include "ui_theme_bind.h"
#include "tz.h"

#include <stdio.h>
#include <string.h>

#define REGIONS_MAX 16
#define PLACES_MAX  200
#define ADDRESS_MAX 95   // a full one-line street address

static settings_location_t s_found;   // the place an address matched, awaiting "Use"

// ---------------------------------------------------------------------------
// Saving
// ---------------------------------------------------------------------------

/**
 * @brief The place or zone was kept (or not): back to the Location screen, rebuilt.
 * @version 1
 * @callback
 */
static void saved_cb(bool ok, const char *why, void *ctx)
{
    bool pop = ctx != NULL;   // from the confirm or zone screen: close it
    if (!ok) {
        settings_app_toast(why && why[0] ? why : "Could not save", false);
        return;
    }
    if (pop) settings_app_pop();
    settings_app_refresh();
    settings_app_toast("Saved", false);
}

// ---------------------------------------------------------------------------
// The place from an address
// ---------------------------------------------------------------------------

/**
 * @brief "Use this place": keep it with the zone the clock has.
 * @version 1
 * @callback
 */
static void use_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    settings_location_t now;
    settings_app_backend()->location_get(&now);
    snprintf(s_found.tz, sizeof(s_found.tz), "%s", now.tz);
    settings_app_backend()->location_set(&s_found, saved_cb, (void *)1);
}

/**
 * @brief The place found, to confirm before it is kept.
 * @version 1
 * @utility
 * @return The frame.
 */
static lv_obj_t *confirm_screen(lv_obj_t *parent, void *ctx)
{
    LV_UNUSED(ctx);
    lv_obj_t *frame = settings_frame_create(parent, "Use this place?", settings_app_back_cb);
    settings_title_button(frame, LV_SYMBOL_OK " Use", use_cb, NULL);
    lv_obj_t *col = settings_column(frame);
    char where[96];
    snprintf(where, sizeof(where), "%s\n%.3f, %.3f", s_found.label, (double)s_found.lat,
             (double)s_found.lon);
    settings_text(col, where, &lv_font_montserrat_24, UI_ROLE_TEXT);
    settings_text(col, "Only the city and the coordinates are kept, not the address.",
                  &lv_font_montserrat_16, UI_ROLE_TEXT_FAINT);
    return frame;
}

/**
 * @brief The address was looked up: confirm the place, or say why not.
 * @version 1
 * @callback
 */
static void found_cb(bool ok, const char *why, const settings_location_t *found, void *ctx)
{
    LV_UNUSED(ctx);
    if (!ok || !found) {
        settings_app_toast(why && why[0] ? why : "No match for that address", false);
        return;
    }
    s_found = *found;
    settings_app_push(confirm_screen, NULL);
}

/**
 * @brief An address typed: look it up.
 * @version 1
 * @callback
 */
static void address_cb(const char *value, void *ctx)
{
    LV_UNUSED(ctx);
    settings_app_toast("Looking up the address...", false);
    settings_app_backend()->location_lookup(value, found_cb, NULL);
}

/**
 * @brief "Place": type an address.
 * @version 1
 * @callback
 */
static void place_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    settings_app_enter_text_max("Street address", "", ADDRESS_MAX, address_cb, NULL);
}

// ---------------------------------------------------------------------------
// The zone, by region and place
// ---------------------------------------------------------------------------

static struct {
    char      regions[REGIONS_MAX][TZ_REGION_LEN];
    int       region_n;
    int       zones[PLACES_MAX];
    int       zone_n;
    char      opts[PLACES_MAX * 24];   // the place wheel's options
    lv_obj_t *region, *place;
} s_tz;

/**
 * @brief Fill the place wheel for the region selected, at @p want (a zone name) if there.
 * @version 1
 * @utility
 */
static void fill_places(const char *want)
{
    const char *region = s_tz.regions[lv_roller_get_selected(s_tz.region)];
    s_tz.zone_n = tz_zones_in(region, s_tz.zones, PLACES_MAX);
    size_t at = 0;
    int sel = 0;
    s_tz.opts[0] = '\0';
    for (int i = 0; i < s_tz.zone_n && at < sizeof(s_tz.opts) - 1; i++) {
        const char *name = TZ_ZONES[s_tz.zones[i]].name;
        size_t from = at;
        at += (size_t)snprintf(s_tz.opts + at, sizeof(s_tz.opts) - at, "%s%s", i ? "\n" : "",
                               tz_place(name));
        for (size_t k = from; k < at && k < sizeof(s_tz.opts); k++) {
            if (s_tz.opts[k] == '_') s_tz.opts[k] = ' ';   // "Ciudad_Juarez" -> "Ciudad Juarez"
        }
        if (want && strcmp(name, want) == 0) sel = i;
    }
    lv_roller_set_options(s_tz.place, s_tz.opts, LV_ROLLER_MODE_NORMAL);
    lv_roller_set_selected(s_tz.place, (uint32_t)sel, LV_ANIM_OFF);
}

/**
 * @brief Another region: its places.
 * @version 1
 * @callback
 */
static void region_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    fill_places(NULL);
}

/**
 * @brief Done: keep the zone with the place the clock has.
 * @version 1
 * @callback
 */
static void zone_done_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    settings_location_t loc;
    settings_app_backend()->location_get(&loc);
    int i = (int)lv_roller_get_selected(s_tz.place);
    if (i < s_tz.zone_n) snprintf(loc.tz, sizeof(loc.tz), "%s", TZ_ZONES[s_tz.zones[i]].name);
    settings_app_backend()->location_set(&loc, saved_cb, (void *)1);
}

/**
 * @brief A wheel in @p row, @p w wide.
 * @version 1
 * @utility
 * @return It.
 */
static lv_obj_t *wheel(lv_obj_t *row, int32_t w)
{
    lv_obj_t *r = lv_roller_create(row);
    lv_obj_set_style_text_font(r, &lv_font_montserrat_20, 0);   // before sizing: rows fit it
    lv_roller_set_visible_row_count(r, 5);
    lv_obj_set_width(r, w);
    return r;
}

/**
 * @brief The region of zone @p name in the region list (0 when none).
 * @version 1
 * @utility
 * @return Its index.
 */
static int region_of(const char *name)
{
    int sel = 0;
    for (int i = 0; i < s_tz.region_n; i++) {
        size_t len = strlen(s_tz.regions[i]);
        if (strncmp(name, s_tz.regions[i], len) == 0 && (name[len] == '/' || !name[len])) sel = i;
    }
    return sel;
}

/**
 * @brief The zone picker: region and place wheels at the clock's zone.
 * @version 1
 * @utility
 * @return The frame.
 */
static lv_obj_t *zone_screen(lv_obj_t *parent, void *ctx)
{
    LV_UNUSED(ctx);
    settings_location_t loc;
    settings_app_backend()->location_get(&loc);
    lv_obj_t *frame = settings_frame_create(parent, "Timezone", settings_app_back_cb);
    settings_title_button(frame, LV_SYMBOL_OK " Done", zone_done_cb, NULL);
    lv_obj_t *col = settings_column(frame);
    lv_obj_t *row = lv_obj_create(col);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, settings_content_w() - 12, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 12, 0);
    lv_obj_set_style_pad_row(row, 8, 0);
    s_tz.region_n = tz_regions(s_tz.regions, REGIONS_MAX);
    static char region_opts[REGIONS_MAX * TZ_REGION_LEN];
    size_t at = 0;
    for (int i = 0; i < s_tz.region_n; i++) {
        at += (size_t)snprintf(region_opts + at, sizeof(region_opts) - at, "%s%s", i ? "\n" : "",
                               s_tz.regions[i]);
    }
    s_tz.region = wheel(row, 180);
    lv_roller_set_options(s_tz.region, region_opts, LV_ROLLER_MODE_NORMAL);
    lv_roller_set_selected(s_tz.region, (uint32_t)region_of(loc.tz), LV_ANIM_OFF);
    lv_obj_add_event_cb(s_tz.region, region_cb, LV_EVENT_VALUE_CHANGED, NULL);
    s_tz.place = wheel(row, LV_MIN(320, settings_content_w() - 40));
    fill_places(loc.tz);
    return frame;
}

/**
 * @brief "Timezone": pick one.
 * @version 1
 * @callback
 */
static void zone_cb(lv_event_t *e)
{
    LV_UNUSED(e);
    settings_app_push(zone_screen, NULL);
}

// ---------------------------------------------------------------------------
// The Location screen
// ---------------------------------------------------------------------------

/**
 * @brief The Location screen.
 * @version 1
 * @req REQ-LOC-001
 * @return The frame.
 */
lv_obj_t *settings_location_screen(lv_obj_t *parent, void *ctx)
{
    LV_UNUSED(ctx);
    settings_location_t loc;
    settings_app_backend()->location_get(&loc);
    lv_obj_t *frame = settings_frame_create(parent, "Location", settings_app_back_cb);
    lv_obj_t *col = settings_column(frame);
    char coords[48];
    snprintf(coords, sizeof(coords), "%.3f, %.3f", (double)loc.lat, (double)loc.lon);
    settings_value_button(settings_field(col, "Place"), loc.label[0] ? loc.label : "Set by address",
                          place_cb, NULL);
    settings_text(settings_field(col, "Coordinates"), coords, &lv_font_montserrat_20, UI_ROLE_TEXT);
    settings_value_button(settings_field(col, "Timezone"), loc.tz[0] ? loc.tz : "Choose",
                          zone_cb, NULL);
    lv_obj_t *note = settings_text(col, "The weather, the sun and the moon follow the place. The "
                                        "address is looked up once and not kept.",
                                   &lv_font_montserrat_16, UI_ROLE_TEXT_FAINT);
    lv_obj_set_width(note, settings_content_w() - 12);
    lv_label_set_long_mode(note, LV_LABEL_LONG_MODE_WRAP);
    return frame;
}
