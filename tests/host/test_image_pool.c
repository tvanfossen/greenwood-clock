/**
 * @file test_image_pool.c
 * @version 1
 * @brief Host tests for the full-screen slots and the in-place pixel conversions.
 */
#include "image_pool.h"

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

/**
 * @brief Slots: taken until none is left, too-large refused, given back, foreign refused.
 * @version 1
 * @utility
 */
static void test_slots(void)
{
    static uint8_t mem[IMAGE_POOL_SLOTS][64];
    uint8_t *bufs[IMAGE_POOL_SLOTS];
    for (int i = 0; i < IMAGE_POOL_SLOTS; i++) bufs[i] = mem[i];
    image_pool_t p;
    image_pool_setup(&p, bufs, IMAGE_POOL_SLOTS, 64);
    CHECK(image_pool_free_count(&p) == IMAGE_POOL_SLOTS, "all free");
    CHECK(image_pool_take(&p, 65) == NULL, "larger than a slot");
    uint8_t *got[IMAGE_POOL_SLOTS];
    for (int i = 0; i < IMAGE_POOL_SLOTS; i++) got[i] = image_pool_take(&p, 64);
    CHECK(got[0] && got[1] && got[0] != got[1] && got[IMAGE_POOL_SLOTS - 1], "distinct slots");
    CHECK(image_pool_take(&p, 8) == NULL && image_pool_free_count(&p) == 0, "none left");
    uint8_t heap[8];
    CHECK(!image_pool_give(&p, heap), "a heap pointer is not a slot");
    CHECK(image_pool_give(&p, got[1]) && image_pool_free_count(&p) == 1, "given back");
    CHECK(!image_pool_give(&p, got[1]), "not twice");
    CHECK(image_pool_take(&p, 10) == got[1], "taken again");
    image_pool_t none;
    image_pool_setup(&none, NULL, 0, 0);
    CHECK(image_pool_take(&none, 1) == NULL, "no slots: heap");
}

/**
 * @brief RGBA to BGRA in place.
 * @version 1
 * @utility
 */
static void test_swap(void)
{
    uint8_t px[8] = { 1, 2, 3, 4, 5, 6, 7, 8 };
    image_px_swap_rb(px, 2);
    CHECK(px[0] == 3 && px[1] == 2 && px[2] == 1 && px[3] == 4 && px[4] == 7 && px[6] == 5,
          "swapped %d %d %d", px[0], px[1], px[2]);
}

/**
 * @brief RGB565 packed in the same buffer matches packing into a separate one, with a padded
 *        stride (1025 px wide, as a background can be).
 * @version 1
 * @utility
 */
static void test_rgb565_inplace(void)
{
    enum { W = 1025, H = 7 };
    uint32_t stride = ((W * 2 + 63) / 64) * 64;
    uint8_t *buf = malloc((size_t)W * H * 4), *src = malloc((size_t)W * H * 4);
    for (size_t i = 0; i < (size_t)W * H * 4; i++) src[i] = (uint8_t)(i * 37 + i / 7);
    memcpy(buf, src, (size_t)W * H * 4);
    image_px_rgba_to_rgb565_inplace(buf, W, H, stride);
    int bad = 0;
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            const uint8_t *s = src + ((size_t)y * W + x) * 4;
            uint16_t want = (uint16_t)(((s[0] & 0xF8) << 8) | ((s[1] & 0xFC) << 3) | (s[2] >> 3));
            uint16_t got;
            memcpy(&got, buf + (size_t)y * stride + (size_t)x * 2, 2);
            bad += got != want;
        }
    }
    CHECK(bad == 0, "%d pixels differ", bad);
    free(buf);
    free(src);
}

/**
 * @brief Run the tests; exit status is the failure count.
 * @version 1
 * @utility
 * @return 0 when every check passed.
 */
int main(void)
{
    test_slots();
    test_swap();
    test_rgb565_inplace();
    printf("image_pool: %d/%d checks passed\n", s_checks - s_failures, s_checks);
    return s_failures;
}
