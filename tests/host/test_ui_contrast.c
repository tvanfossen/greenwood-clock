/**
 * @file test_ui_contrast.c
 * @version 1
 * @brief Host tests for ui_legible: a colour that already meets the contrast target passes
 *        through unchanged; one that does not has its OKLCH lightness moved only as far as
 *        the target needs, keeping its hue.
 */
#include "ui_contrast.h"

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
 * @brief Run the ui_contrast tests; exit status is the failure count.
 * @version 1
 * @utility
 * @return 0 when every check passed.
 */
int main(void)
{
    test_pass_through();
    test_black_over_dark();
    test_white_over_light();
    test_hue_kept();
    printf("ui_contrast: %d/%d checks passed\n", s_checks - s_failures, s_checks);
    return s_failures;
}
