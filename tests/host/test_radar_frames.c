/**
 * @file test_radar_frames.c
 * @version 1
 * @brief Host tests for the radar loop's frame choice (newest first, spaced, recent, capped,
 *        oldest-first output).
 */
#include "nws_radar_frames.h"

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

#define MIN_S   60
#define GAP_S   (9 * 60)
#define AGE_S   (70 * 60)
#define NOW_S   1795477000LL

/**
 * @brief Fill @p r with images every @p step_s seconds, the newest (open) starting at
 *        @p newest_s, @p n in all, newest last.
 * @version 1
 * @utility
 */
static void rasters(nws_radar_raster_t *r, int n, int64_t newest_s, int step_s)
{
    for (int i = 0; i < n; i++) {
        int64_t start = newest_s - (int64_t)(n - 1 - i) * step_s;
        r[i].id = i + 1;
        r[i].start_ms = start * 1000;
        r[i].end_ms = (i == n - 1) ? start * 1000 : (start + step_s) * 1000;
    }
}

/**
 * @brief Images every 7 min: the newest is kept and the rest spaced >= 9 min, oldest first.
 * @version 1
 * @utility
 */
static void test_spacing(void)
{
    nws_radar_raster_t r[16], out[6];
    rasters(r, 16, NOW_S - 5 * MIN_S, 7 * MIN_S);
    int n = nws_radar_pick(r, 16, NOW_S, GAP_S, AGE_S, out, 6);
    CHECK(n == 5, "70 min at >= 9 min spacing from 7 min steps: 5 frames, got %d", n);
    CHECK(out[n - 1].start_ms == r[15].start_ms, "newest last");
    for (int i = 1; i < n; i++) {
        int64_t gap = (out[i].start_ms - out[i - 1].start_ms) / 1000;
        CHECK(gap >= GAP_S, "gap %d: %lld s", i, (long long)gap);
    }
    CHECK((NOW_S * 1000 - out[0].start_ms) / 1000 <= AGE_S, "oldest within max age");
}

/**
 * @brief The cap keeps the newest frames.
 * @version 1
 * @utility
 */
static void test_cap(void)
{
    nws_radar_raster_t r[16], out[3];
    rasters(r, 16, NOW_S - MIN_S, 10 * MIN_S);
    int n = nws_radar_pick(r, 16, NOW_S, GAP_S, AGE_S, out, 3);
    CHECK(n == 3, "capped at 3, got %d", n);
    CHECK(out[2].start_ms == r[15].start_ms && out[0].start_ms == r[13].start_ms,
          "the newest three");
}

/**
 * @brief Input order does not matter; nothing older than the max age.
 * @version 1
 * @utility
 */
static void test_unsorted_and_stale(void)
{
    nws_radar_raster_t r[4] = {
        { 1, (NOW_S - 20 * MIN_S) * 1000, (NOW_S - 10 * MIN_S) * 1000 },
        { 2, (NOW_S - 90 * MIN_S) * 1000, (NOW_S - 80 * MIN_S) * 1000 },   // too old
        { 3, (NOW_S - 1 * MIN_S) * 1000, (NOW_S - 1 * MIN_S) * 1000 },     // newest, open
        { 4, (NOW_S - 10 * MIN_S) * 1000, (NOW_S - 1 * MIN_S) * 1000 },
    };
    nws_radar_raster_t out[6];
    int n = nws_radar_pick(r, 4, NOW_S, GAP_S, AGE_S, out, 6);
    CHECK(n == 3, "three recent, got %d", n);
    CHECK(out[0].start_ms == r[0].start_ms && out[2].start_ms == r[2].start_ms, "oldest first");
}

/**
 * @brief Run the radar frame tests; exit status is the failure count.
 * @version 1
 * @utility
 * @return 0 when every check passed.
 */
int main(void)
{
    test_spacing();
    test_cap();
    test_unsorted_and_stale();
    printf("radar_frames: %d/%d checks passed\n", s_checks - s_failures, s_checks);
    return s_failures;
}
