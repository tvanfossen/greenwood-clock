/**
 * @file settings_mock_library.c
 * @version 1
 * @brief The sample backend's Library (REQ-LIB-001): sample backgrounds, a 14-photo album
 *        (two pages), the sample themes and animations; thumbnails painted from the theme's
 *        roles (no card to read them from); delete and rename change the sample lists and
 *        keep the device's rules (in use: refused, naming who).
 */
#include "settings_mock.h"
#include "library_thumb.h"

#include <stdio.h>
#include <string.h>

#define ALBUM_N 14

static lib_item_t s_items[LIB_KINDS][LIB_ALBUM_MAX];
static int        s_n[LIB_KINDS];

/**
 * @brief Add sample @p name (@p label) to @p kind.
 * @version 1
 * @utility
 */
static void add(lib_kind_t kind, const char *name, const char *label, bool thumb)
{
    lib_item_t *it = &s_items[kind][s_n[kind]++];
    snprintf(it->name, sizeof(it->name), "%s", name);
    snprintf(it->label, sizeof(it->label), "%s", label);
    it->size = thumb ? 900000 + 37000 * (uint32_t)s_n[kind] : 2400;
    it->mtime = 1795000000u + 86400u * (uint32_t)s_n[kind];   // Nov 2026
    it->thumb = thumb;
}

/**
 * @brief The sample lists, once.
 * @version 1
 * @utility
 */
static void samples(void)
{
    static const char *const BG[] = { "campfire", "night_winter", "spring-flower", "winter_afternoon", "winter_morning" };
    static const char *const ANIM[] = { "Hummingbird", "Snowfall", "Leaves", "None" };
    if (s_n[LIB_BACKGROUNDS]) return;
    for (int i = 0; i < 5; i++) add(LIB_BACKGROUNDS, BG[i], BG[i], true);
    for (int i = 0; i < ALBUM_N; i++) {
        char name[16];
        snprintf(name, sizeof(name), "photo_%02d", i + 1);
        add(LIB_ALBUM, name, name, true);
    }
    add(LIB_THEMES, "default", "Default", false);
    add(LIB_THEMES, "sassafras", "Sassafras", false);
    for (int i = 0; i < 4; i++) add(LIB_ANIMS, ANIM[i], ANIM[i], false);
}

/**
 * @brief The sample kind.
 * @version 1
 * @callback
 */
static void mock_list(lib_kind_t kind, settings_library_cb_t done, void *ctx)
{
    samples();
    done(true, "", s_items[kind], s_n[kind], 30436, 27911, ctx);
}

/**
 * @brief A sample picture: two theme roles (picked by the name) blended down, a horizon.
 * @version 1
 * @callback
 */
static void mock_thumb(lib_kind_t kind, const char *name, settings_thumb_cb_t done, void *ctx)
{
    static const ui_role_t SKY[] = { UI_ROLE_ACCENT, UI_ROLE_ACCENT_ALT, UI_ROLE_SUN, UI_ROLE_AURORA, UI_ROLE_MOON };
    unsigned h = (unsigned)kind * 7u;
    for (const char *p = name; *p; p++) h = h * 31u + (unsigned char)*p;
    lv_color_t a = ui_theme_color(SKY[h % 5]), b = ui_theme_color(UI_ROLE_SURFACE_RAISED);
    lv_color_t ground = ui_theme_color(UI_ROLE_SURFACE_INSET);
    lv_image_dsc_t *d = library_thumb_new(LIB_THUMB_W, LIB_THUMB_H);
    int horizon = LIB_THUMB_H * (55 + (int)(h % 25)) / 100;
    for (int y = 0; d && y < LIB_THUMB_H; y++) {
        lv_color_t c = y < horizon ? lv_color_mix(b, a, (uint8_t)(255 * y / horizon)) : ground;
        uint8_t *row = (uint8_t *)d->data + (size_t)y * d->header.stride;
        for (int x = 0; x < LIB_THUMB_W; x++) {
            uint8_t *q = row + x * 4;
            q[0] = c.blue, q[1] = c.green, q[2] = c.red, q[3] = 255;
        }
    }
    done(d, ctx);
}

/**
 * @brief Where @p name is in @p kind.
 * @version 1
 * @utility
 * @return Its index, or -1.
 */
static int find(lib_kind_t kind, const char *name)
{
    int i = s_n[kind] - 1;
    while (i >= 0 && strcmp(s_items[kind][i].name, name) != 0) i--;
    return i;
}

/**
 * @brief Delete a sample item (refused while a sample profile uses it).
 * @version 1
 * @callback
 */
static void mock_delete(lib_kind_t kind, const char *name, settings_done_cb_t done, void *ctx)
{
    static sched_rule_t r[SCHED_MAX_PROFILES];
    const char *who[1];
    char ref[SCHED_IMAGE_LEN], why[96] = "";
    lib_ref(kind, name, ref, sizeof(ref));
    int n = settings_mock_backend()->schedule_get(r, SCHED_MAX_PROFILES);
    int i = find(kind, name);
    if (lib_used_by(r, n, kind, ref, who, 1)) snprintf(why, sizeof(why), "Used by %s", who[0]);
    if (!why[0] && i >= 0) {
        memmove(&s_items[kind][i], &s_items[kind][i + 1], sizeof(lib_item_t) * (size_t)(s_n[kind] - i - 1));
        s_n[kind]--;
    }
    done(!why[0], why, ctx);
}

/**
 * @brief Rename a sample item; the sample profiles follow.
 * @version 1
 * @callback
 */
static void mock_rename(lib_kind_t kind, const char *from, const char *to, settings_done_cb_t done,
                        void *ctx)
{
    static sched_rule_t r[SCHED_MAX_PROFILES];
    int i = find(kind, from);
    bool ok = i >= 0 && find(kind, to) < 0;
    if (ok) {
        char a[SCHED_IMAGE_LEN], b[SCHED_IMAGE_LEN];
        snprintf(s_items[kind][i].name, sizeof(s_items[kind][i].name), "%s", to);
        snprintf(s_items[kind][i].label, sizeof(s_items[kind][i].label), "%s", to);
        lib_ref(kind, from, a, sizeof(a));
        lib_ref(kind, to, b, sizeof(b));
        const settings_backend_t *be = settings_mock_backend();
        int n = be->schedule_get(r, SCHED_MAX_PROFILES);
        if (lib_rename_refs(r, n, kind, a, b)) be->schedule_set(r, n, done, ctx);
        else done(true, "", ctx);
        return;
    }
    done(false, "That name is taken", ctx);
}

/**
 * @brief The sample theme: the one showing, named after the file.
 * @version 1
 * @callback
 */
static void mock_theme_load(const char *stem, settings_theme_cb_t done, void *ctx)
{
    static ui_theme_t t;
    t = *ui_theme_get();
    int i = find(LIB_THEMES, stem);
    if (i >= 0) snprintf(t.name, sizeof(t.name), "%s", s_items[LIB_THEMES][i].label);
    done(i >= 0, &t, ctx);
}

/**
 * @brief Fill the sample backend's Library calls.
 * @version 1
 * @req REQ-LIB-001
 */
void settings_mock_library_fill(settings_backend_t *be)
{
    be->library_list = mock_list;
    be->library_thumb = mock_thumb;
    be->library_delete = mock_delete;
    be->library_rename = mock_rename;
    be->theme_load = mock_theme_load;
}
