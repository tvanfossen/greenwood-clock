/**
 * @file test_particle_index.c
 * @version 1
 * @brief Host tests for the particle band index: every query equals a full scan (same
 *        particles, same order), over random boxes and redraw areas, off-screen boxes and the
 *        too-many fallback.
 */
#include "particle_index.h"

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
 * @brief Whether two boxes intersect (edges inclusive, as lv_area_intersect).
 * @version 1
 * @utility
 * @return Whether.
 */
static bool hit(const lv_area_t *a, const lv_area_t *b)
{
    return a->x1 <= b->x2 && b->x1 <= a->x2 && a->y1 <= b->y2 && b->y1 <= a->y2;
}

/**
 * @brief A random box: mostly small flakes, some big leaves, some partly off screen.
 * @version 1
 * @utility
 * @return It.
 */
static lv_area_t rnd_box(void)
{
    int s = rand() % 10 < 8 ? 2 + rand() % 10 : 20 + rand() % 60;
    int x = rand() % 1100 - 40, y = rand() % 700 - 60;
    lv_area_t a = { x, y, x + s, y + s };
    return a;
}

/**
 * @brief Queries match a full scan.
 * @version 1
 * @utility
 */
static void test_matches_scan(int n, int rounds)
{
    static particle_index_t x;
    lv_area_t boxes[PARTICLE_INDEX_MAX];
    int bad = 0;
    for (int r = 0; r < rounds; r++) {
        for (int i = 0; i < n; i++) boxes[i] = rnd_box();
        particle_index_build(&x, boxes, n);
        for (int q = 0; q < 40; q++) {
            lv_area_t clip = rnd_box();
            uint8_t got[PARTICLE_INDEX_MAX];
            int k = particle_index_query(&x, boxes, &clip, got), m = 0;
            for (int i = 0; i < n; i++) {
                if (!hit(&boxes[i], &clip)) continue;
                bad += m >= k || got[m] != i;
                m++;
            }
            bad += m != k;
        }
    }
    CHECK(bad == 0, "%d particles: %d mismatches against a full scan", n, bad);
}

/**
 * @brief Run the tests; exit status is the failure count.
 * @version 1
 * @utility
 * @return 0 when every check passed.
 */
int main(void)
{
    srand(7);
    test_matches_scan(0, 3);
    test_matches_scan(1, 50);
    test_matches_scan(140, 200);
    test_matches_scan(PARTICLE_INDEX_MAX, 100);
    static particle_index_t x;   // every box screen-tall: too many memberships, falls back to a scan
    lv_area_t tall[PARTICLE_INDEX_MAX];
    for (int i = 0; i < PARTICLE_INDEX_MAX; i++) tall[i] = (lv_area_t){ i, -10, i + 3, 650 };
    particle_index_build(&x, tall, PARTICLE_INDEX_MAX);
    lv_area_t clip = { 10, 300, 12, 302 };
    uint8_t got[PARTICLE_INDEX_MAX];
    int k = particle_index_query(&x, tall, &clip, got);
    CHECK(x.n == -1 && k == 6 && got[0] == 7 && got[5] == 12, "fallback scan: %d hits from %d", k, got[0]);
    printf("particle_index: %d/%d checks passed\n", s_checks - s_failures, s_checks);
    return s_failures;
}
