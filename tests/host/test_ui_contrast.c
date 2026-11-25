/**
 * @file test_ui_contrast.c
 * v2: APCA contrast and the background-polarity legible colour (ui_legible_apca).
 * @version 2
 * @brief Host tests for ui_legible: a colour that already meets the contrast target passes
 *        through unchanged; one that does not has its OKLCH lightness moved only as far as
 *        the target needs, keeping its hue. APCA: reference values, and text whose
 *        light/dark side follows the background rather than the ink.
 */
#include "ui_contrast.h"

#include <math.h>
#include <stdio.h>

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

/** Margin above the target a "minimal" shift may land (one 8-bit step can overshoot). */
#define MINIMAL_SLACK 0.6f

/**
 * @brief True if @p a and @p b are the same colour.
 * @version 1
 * @utility
 * @return Whether every channel matches.
 */
static bool same(lv_color_t a, lv_color_t b)
{
    return a.red == b.red && a.green == b.green && a.blue == b.blue;
}

/**
 * @brief Colours that already meet the target are returned exactly.
 * @version 1
 * @utility
 */
static void test_pass_through(void)
{
    lv_color_t white = lv_color_white(), black = lv_color_black();
    CHECK(same(ui_legible(white, lv_color_hex(0x101820)), white), "white over dark map");
    CHECK(same(ui_legible(black, lv_color_hex(0xe0d8c0)), black), "black over bright photo");
    lv_color_t gold = lv_color_hex(0xE9C46A);
    CHECK(same(ui_legible(gold, lv_color_hex(0x0a0a1e)), gold), "gold over navy panel");
}

/**
 * @brief Black over the dark radar map becomes light enough, and no lighter than needed.
 * @version 1
 * @utility
 */
static void test_black_over_dark(void)
{
    lv_color_t bg = lv_color_hex(0x161a1c);
    lv_color_t out = ui_legible(lv_color_black(), bg);
    float r = ui_contrast_ratio(out, bg);
    CHECK(r >= UI_CONTRAST_AA, "reaches AA: %.2f", (double)r);
    CHECK(r < UI_CONTRAST_AA + MINIMAL_SLACK, "minimal shift: %.2f", (double)r);
}

/**
 * @brief White over a bright background darkens only as far as AA needs (not to black).
 * @version 1
 * @utility
 */
static void test_white_over_light(void)
{
    lv_color_t bg = lv_color_hex(0xd8d4cc);
    lv_color_t out = ui_legible(lv_color_white(), bg);
    float r = ui_contrast_ratio(out, bg);
    CHECK(r >= UI_CONTRAST_AA, "reaches AA: %.2f", (double)r);
    CHECK(r < UI_CONTRAST_AA + MINIMAL_SLACK, "minimal shift: %.2f", (double)r);
    CHECK(!same(out, lv_color_black()), "not pushed to black");
}

/**
 * @brief A dark red over a dark grey lightens but stays red (hue kept).
 * @version 1
 * @utility
 */
static void test_hue_kept(void)
{
    lv_color_t bg = lv_color_hex(0x303030);
    lv_color_t out = ui_legible(lv_color_hex(0x800000), bg);
    float r = ui_contrast_ratio(out, bg);
    CHECK(r >= UI_CONTRAST_AA, "reaches AA: %.2f", (double)r);
    CHECK(out.red > out.green && out.red > out.blue, "still red: %02x%02x%02x",
          out.red, out.green, out.blue);
}

/**
 * @brief APCA Lc matches the reference implementation (apca-w3 0.0.98G-4g) for known pairs.
 * @version 1
 * @utility
 */
static void test_apca_reference(void)
{
    static const struct { uint32_t txt, bg; float lc; } REF[] = {
        { 0x000000, 0xffffff, 106.04f },
        { 0xffffff, 0x000000, -107.88f },
        { 0x888888, 0xffffff, 63.06f },
        { 0xffffff, 0x888888, -68.54f },
    };
    for (size_t i = 0; i < sizeof(REF) / sizeof(REF[0]); i++) {
        float lc = ui_apca_lc(lv_color_hex(REF[i].txt), lv_color_hex(REF[i].bg));
        CHECK(fabsf(lc - REF[i].lc) < 0.5f, "%06x on %06x: Lc %.2f, want %.2f",
              (unsigned)REF[i].txt, (unsigned)REF[i].bg, (double)lc, (double)REF[i].lc);
    }
}

/**
 * @brief The light/dark side comes from the background, not the ink: over the mid amber
 *        of a sunrise photo (where WCAG 2 picks black by a hair) white stays white and even
 *        black ink comes out light; over a pale sky white ink goes dark.
 * @version 1
 * @utility
 */
static void test_apca_polarity_from_background(void)
{
    lv_color_t amber = lv_color_hex(0x98744e), pale = lv_color_hex(0xc6c3ad);
    lv_color_t w = ui_legible_apca(lv_color_white(), amber, UI_APCA_LARGE);
    lv_color_t b = ui_legible_apca(lv_color_black(), amber, UI_APCA_LARGE);
    CHECK(ui_color_to_oklch(w).l > 0.9f, "white over amber stays light: L %.2f",
          (double)ui_color_to_oklch(w).l);
    CHECK(ui_color_to_oklch(b).l > 0.9f, "black ink over amber comes out light: L %.2f",
          (double)ui_color_to_oklch(b).l);
    lv_color_t d = ui_legible_apca(lv_color_white(), pale, UI_APCA_LARGE);
    CHECK(ui_color_to_oklch(d).l < 0.4f, "white over a pale sky goes dark: L %.2f",
          (double)ui_color_to_oklch(d).l);
    CHECK(fabsf(ui_apca_lc(d, pale)) >= UI_APCA_LARGE, "and reaches the target: Lc %.1f",
          (double)ui_apca_lc(d, pale));
}

/**
 * @brief A coloured ink keeps its hue when its lightness is moved, and an ink already past
 *        the target on the background's side passes through unchanged.
 * @version 1
 * @utility
 */
static void test_apca_hue_and_pass_through(void)
{
    lv_color_t gold = lv_color_hex(0xe9c46a), pale = lv_color_hex(0xd8d8d8);
    lv_color_t out = ui_legible_apca(gold, pale, UI_APCA_LARGE);
    float gap = fabsf(ui_color_to_oklch(out).h - ui_color_to_oklch(gold).h);
    CHECK(ui_color_to_oklch(out).l < ui_color_to_oklch(gold).l, "gold darkens over pale");
    CHECK(gap < 8.0f || ui_color_to_oklch(out).c < 0.02f, "hue kept: gap %.1f", (double)gap);
    lv_color_t night = lv_color_hex(0x061f43);
    CHECK(same(ui_legible_apca(gold, night, UI_APCA_LARGE), gold), "legible gold unchanged");
}

/**
 * @brief Run the ui_contrast tests; exit status is the failure count.
 * v2: the APCA tests.
 * @version 2
 * @utility
 * @return 0 when every check passed.
 */
int main(void)
{
    test_pass_through();
    test_black_over_dark();
    test_white_over_light();
    test_hue_kept();
    test_apca_reference();
    test_apca_polarity_from_background();
    test_apca_hue_and_pass_through();
    printf("ui_contrast: %d/%d checks passed\n", s_checks - s_failures, s_checks);
    return s_failures;
}
