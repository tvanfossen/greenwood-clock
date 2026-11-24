/**
 * @file test_ui_theme.c
 * v2: slots (which role fills each themed place) — defaults, file overrides, refusals.
 * @version 2
 * @brief Host tests for the OKLCH theme: the base theme reproduces the clock's original
 *        colours; every shipped theme's text roles meet their contrast targets; text over
 *        any backdrop keeps its hue and meets its target; theme files parse (and the base
 *        file matches the compiled fallback); malformed files are refused.
 */
#include "ui_theme.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int s_failures = 0;
static int s_checks   = 0;

#define CHECK(cond, ...)                                              \
    do {                                                              \
        s_checks++;                                                   \
        if (!(cond)) {                                                \
            s_failures++;                                             \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);               \
            printf(__VA_ARGS__);                                      \
            printf("\n");                                             \
        }                                                             \
    } while (0)

/** Largest per-channel difference accepted when a derived role reproduces an original. */
#define CHANNEL_SLACK 6

/** Largest hue drift (degrees) accepted when text is made legible. */
#define HUE_SLACK 6.0f

static const char *const THEME_FILES[] = {
    "sdcard/themes/greenwood.json",
    "sdcard/themes/greenwood-day.json",
    "sdcard/themes/greenwood-night.json",
    "sdcard/themes/sassafras.json",
};
#define THEME_FILE_COUNT (sizeof(THEME_FILES) / sizeof(THEME_FILES[0]))

/**
 * @brief True if every channel of @p a is within CHANNEL_SLACK of @p hex.
 * @version 1
 * @utility
 * @return Whether the colours match.
 */
static bool near_hex(lv_color_t a, uint32_t hex)
{
    lv_color_t b = lv_color_hex(hex);
    return abs(a.red - b.red) <= CHANNEL_SLACK && abs(a.green - b.green) <= CHANNEL_SLACK &&
           abs(a.blue - b.blue) <= CHANNEL_SLACK;
}

/**
 * @brief Smallest angle between two hues, in degrees.
 * @version 1
 * @utility
 * @return 0..180.
 */
static float hue_gap(float a, float b)
{
    float d = fabsf(fmodf(a - b + 540.0f, 360.0f) - 180.0f);
    return d;
}

/**
 * @brief Read a whole file into a NUL-terminated heap buffer.
 * @version 1
 * @utility
 * @return The text (free it), or NULL.
 */
static char *read_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    char *buf = NULL;
    if (f) {
        fseek(f, 0, SEEK_END);
        long n = ftell(f);
        fseek(f, 0, SEEK_SET);
        buf = calloc(1, (size_t)n + 1);
        if (buf && fread(buf, 1, (size_t)n, f) != (size_t)n) { free(buf); buf = NULL; }
        fclose(f);
    }
    return buf;
}

/**
 * @brief Load and parse a shipped theme file.
 * @version 1
 * @utility
 * @return Whether it parsed.
 */
static bool load_theme(const char *path, ui_theme_t *out)
{
    char *json = read_file(path);
    bool ok = json && ui_theme_parse(json, out);
    free(json);
    return ok;
}

/**
 * @brief The base theme's roles land on the clock's original hand-picked colours.
 * @version 1
 * @utility
 */
static void test_base_reproduces_original(void)
{
    lv_color_t r[UI_ROLE_COUNT];
    ui_theme_derive(&UI_THEME_BASE, r);
    CHECK(near_hex(r[UI_ROLE_SURFACE], 0x0a0a1e), "surface = card navy");
    CHECK(near_hex(r[UI_ROLE_SURFACE_RAISED], 0x1a1a2e), "raised = strip/dialog navy");
    CHECK(near_hex(r[UI_ROLE_SURFACE_INSET], 0x2a2a44), "inset = button navy");
    CHECK(near_hex(r[UI_ROLE_ACCENT], 0xE9C46A), "accent = gold");
    CHECK(near_hex(r[UI_ROLE_ACCENT_ALT], 0xa490c2), "accent_alt = aurora violet");
    CHECK(near_hex(r[UI_ROLE_TEXT], 0xffffff), "text = white");
    CHECK(near_hex(r[UI_ROLE_SCREEN], 0x000000) || r[UI_ROLE_SCREEN].red < 0x10,
          "screen near black");
}

/**
 * @brief Every text role of every shipped theme meets its target against SURFACE.
 * @version 1
 * @utility
 */
static void test_text_targets(void)
{
    static const ui_role_t TEXT_ROLES[] = { UI_ROLE_TEXT, UI_ROLE_TEXT_MUTED, UI_ROLE_TEXT_FAINT };
    for (size_t f = 0; f < THEME_FILE_COUNT; f++) {
        ui_theme_t t;
        CHECK(load_theme(THEME_FILES[f], &t), "%s parses", THEME_FILES[f]);
        ui_theme_set(&t);
        lv_color_t surface = ui_theme_color(UI_ROLE_SURFACE);
        for (size_t i = 0; i < 3; i++) {
            float ratio = ui_contrast_ratio(ui_theme_color(TEXT_ROLES[i]), surface);
            CHECK(ratio >= ui_theme_target(TEXT_ROLES[i]) - 0.01f, "%s %s: %.2f < %.2f",
                  THEME_FILES[f], ui_theme_role_name(TEXT_ROLES[i]), (double)ratio,
                  (double)ui_theme_target(TEXT_ROLES[i]));
        }
    }
    ui_theme_set(NULL);
}

/**
 * @brief Text over arbitrary backdrops meets its target and keeps the ink's hue.
 * @version 1
 * @utility
 */
static void test_text_on_backdrops(void)
{
    static const uint32_t BACKDROPS[] = { 0x0a0a1e, 0x3a3a44, 0x808080, 0xc0c8d8, 0xffffff };
    ui_theme_t t;
    CHECK(load_theme("sdcard/themes/sassafras.json", &t), "sassafras parses");
    ui_theme_set(&t);
    for (size_t i = 0; i < sizeof(BACKDROPS) / sizeof(BACKDROPS[0]); i++) {
        lv_color_t bg = lv_color_hex(BACKDROPS[i]);
        lv_color_t c = ui_theme_text_on(UI_ROLE_TEXT, bg);
        float ratio = ui_contrast_ratio(c, bg);
        float best = fmaxf(ui_contrast_ratio(lv_color_black(), bg),
                           ui_contrast_ratio(lv_color_white(), bg));
        float want = fminf(t.contrast, best) - 0.3f;   // the extremes keep a little chroma
        CHECK(ratio >= want, "text on %06x: %.2f < %.2f", (unsigned)BACKDROPS[i],
              (double)ratio, (double)want);
        ui_oklch_t o = ui_color_to_oklch(c);
        CHECK(o.c < 0.01f || hue_gap(o.h, t.ink.h) <= HUE_SLACK, "hue kept on %06x: %.1f",
              (unsigned)BACKDROPS[i], (double)o.h);
    }
    ui_theme_set(NULL);
}

/**
 * @brief A grey ink's muted and faint tiers take the surface's hue, never a stray one (an
 *        achromatic ink has no hue of its own to keep).
 * @version 1
 * @utility
 */
static void test_grey_ink_tiers(void)
{
    lv_color_t r[UI_ROLE_COUNT];
    ui_theme_derive(&UI_THEME_BASE, r);
    static const ui_role_t TIERS[] = { UI_ROLE_TEXT_MUTED, UI_ROLE_TEXT_FAINT };
    for (size_t i = 0; i < 2; i++) {
        ui_oklch_t o = ui_color_to_oklch(r[TIERS[i]]);
        CHECK(o.c < 0.012f || hue_gap(o.h, UI_THEME_BASE.surface.h) <= 20.0f,
              "%s hue %.1f (chroma %.3f) is not the surface's", ui_theme_role_name(TIERS[i]),
              (double)o.h, (double)o.c);
    }
}

/**
 * @brief A light theme steps its tints darker; a dark one lighter.
 * @version 1
 * @utility
 */
static void test_polarity(void)
{
    ui_theme_t t;
    CHECK(load_theme("sdcard/themes/sassafras.json", &t), "sassafras parses");
    lv_color_t r[UI_ROLE_COUNT];
    ui_theme_derive(&t, r);
    ui_oklch_t s = ui_color_to_oklch(r[UI_ROLE_SURFACE]);
    ui_oklch_t raised = ui_color_to_oklch(r[UI_ROLE_SURFACE_RAISED]);
    CHECK(raised.l < s.l, "light theme: raised darker (%.3f vs %.3f)", (double)raised.l,
          (double)s.l);
    ui_theme_derive(&UI_THEME_BASE, r);
    s = ui_color_to_oklch(r[UI_ROLE_SURFACE]);
    raised = ui_color_to_oklch(r[UI_ROLE_SURFACE_RAISED]);
    CHECK(raised.l > s.l, "dark theme: raised lighter");
}

/**
 * @brief The base theme file equals the compiled fallback (one look, two copies).
 * v2: slots compared too.
 * @version 2
 * @utility
 */
static void test_base_file_matches(void)
{
    ui_theme_t t;
    CHECK(load_theme("sdcard/themes/greenwood.json", &t), "greenwood parses");
    CHECK(strcmp(t.name, UI_THEME_BASE.name) == 0, "name");
    CHECK(memcmp(&t.ink, &UI_THEME_BASE.ink, sizeof(t.ink)) == 0, "ink");
    CHECK(memcmp(&t.surface, &UI_THEME_BASE.surface, sizeof(t.surface)) == 0, "surface");
    CHECK(memcmp(&t.accent, &UI_THEME_BASE.accent, sizeof(t.accent)) == 0, "accent");
    CHECK(memcmp(t.slots, UI_THEME_BASE.slots, sizeof(t.slots)) == 0, "slots");
    CHECK(t.contrast == UI_THEME_BASE.contrast, "contrast");
}

/**
 * @brief Malformed or out-of-range theme files are refused and leave the output alone.
 * @version 1
 * @utility
 */
static void test_parse_rejects(void)
{
    ui_theme_t t = UI_THEME_BASE;
    CHECK(!ui_theme_parse("{\"name\":\"x\",\"ink\":[1,0,0],\"surface\":[0,0,0]}", &t),
          "missing accent");
    CHECK(!ui_theme_parse("{\"name\":\"x\",\"ink\":[1,0.9,0],\"surface\":[0,0,0],"
                          "\"accent\":[0.5,0.1,90]}", &t), "chroma out of range");
    CHECK(!ui_theme_parse("{\"name\":\"x\",\"ink\":[1,0,0],\"surface\":[0,0,0],"
                          "\"accent\":[0.5,0.1,90],\"contrast\":30}", &t), "contrast > 21");
    CHECK(!ui_theme_parse("not json", &t), "not JSON");
    CHECK(!ui_theme_parse(NULL, &t), "NULL");
    CHECK(strcmp(t.name, UI_THEME_BASE.name) == 0, "output untouched on failure");
    CHECK(ui_theme_parse("{\"name\":\"x\",\"ink\":[1,0,0],\"surface\":[0,0,0],"
                         "\"accent\":[0.5,0.1,90]}", &t) && t.contrast == 7.0f,
          "contrast defaults to 7");
}

/**
 * @brief Every role name maps back to its role.
 * @version 1
 * @utility
 */
static void test_role_names(void)
{
    for (int i = 0; i < UI_ROLE_COUNT; i++) {
        ui_role_t r;
        CHECK(ui_theme_role_by_name(ui_theme_role_name((ui_role_t)i), &r) && r == (ui_role_t)i,
              "role %d round-trips", i);
    }
    ui_role_t r;
    CHECK(!ui_theme_role_by_name("#ff0000", &r), "a hex string is not a role");
}

/**
 * @brief Slots default to the original look (cards on surface, alerts on error/warn/border)
 *        and a theme file may move them to any role; unknown slots or roles refuse the file.
 * @version 1
 * @utility
 */
static void test_slots(void)
{
    ui_theme_set(NULL);
    CHECK(ui_theme_slot_role(UI_SLOT_CARD) == UI_ROLE_SURFACE, "card on surface");
    CHECK(ui_theme_slot_role(UI_SLOT_STRIP) == UI_ROLE_SURFACE_RAISED, "strip raised");
    CHECK(ui_theme_slot_role(UI_SLOT_ALERT_EXTREME) == UI_ROLE_ERROR_STRONG, "extreme");
    CHECK(ui_theme_slot_role(UI_SLOT_ALERT_SEVERE) == UI_ROLE_ERROR, "severe");
    CHECK(ui_theme_slot_role(UI_SLOT_ALERT_MODERATE) == UI_ROLE_WARN, "moderate");
    CHECK(ui_theme_slot_role(UI_SLOT_ALERT_MINOR) == UI_ROLE_BORDER, "minor");

    ui_theme_t t;
    const char *base = "{\"name\":\"x\",\"ink\":[1,0,0],\"surface\":[0.2,0.04,280],"
                       "\"accent\":[0.8,0.1,90],";
    char json[400];
    snprintf(json, sizeof(json), "%s\"slots\":{\"alert_minor\":\"accent\",\"card\":\"surface_raised\"}}", base);
    CHECK(ui_theme_parse(json, &t), "slot overrides parse");
    ui_theme_set(&t);
    CHECK(ui_theme_slot_role(UI_SLOT_ALERT_MINOR) == UI_ROLE_ACCENT, "minor moved to accent");
    CHECK(ui_theme_slot_role(UI_SLOT_CARD) == UI_ROLE_SURFACE_RAISED, "card moved");
    CHECK(ui_theme_slot_role(UI_SLOT_ALERT_SEVERE) == UI_ROLE_ERROR, "others keep defaults");
    snprintf(json, sizeof(json), "%s\"slots\":{\"nope\":\"accent\"}}", base);
    CHECK(!ui_theme_parse(json, &t), "unknown slot refused");
    snprintf(json, sizeof(json), "%s\"slots\":{\"card\":\"#ff0000\"}}", base);
    CHECK(!ui_theme_parse(json, &t), "a colour instead of a role refused");
    for (int i = 0; i < UI_SLOT_COUNT; i++) {
        ui_slot_t sl;
        CHECK(ui_theme_slot_by_name(ui_theme_slot_name((ui_slot_t)i), &sl) && sl == (ui_slot_t)i,
              "slot %d round-trips", i);
    }
    ui_theme_set(NULL);
}

/**
 * @brief Run the ui_theme tests; exit status is the failure count.
 * v2: slots.
 * @version 2
 * @utility
 * @return 0 when every check passed.
 */
int main(void)
{
    test_base_reproduces_original();
    test_text_targets();
    test_text_on_backdrops();
    test_grey_ink_tiers();
    test_polarity();
    test_base_file_matches();
    test_parse_rejects();
    test_role_names();
    test_slots();
    printf("ui_theme: %d/%d checks passed\n", s_checks - s_failures, s_checks);
    return s_failures;
}
