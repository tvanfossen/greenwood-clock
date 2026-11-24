/**
 * @file test_radar_frames.c
 * @version 2
 * @brief Host tests for the radar loop's frame choice: past frames on the 10-minute grid (the
 *        image current at each mark, shown at the mark), the newest image at its own time,
 *        recent, capped, oldest-first output.
 * v2: grid frames instead of every image at its own time.
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
#define GRID_S  (10 * 60)
#define AGE_S   (70 * 60)
#define NOW_S   1795477800LL   /* on a 10-minute mark */

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
 * @brief Whether raster @p r was the image current at @p t_s.
 * @version 1
 * @utility
 * @return true when its validity covers the instant.
 */
static int covers(const nws_radar_raster_t *r, int64_t t_s)
{
    return r->start_ms <= t_s * 1000 && (r->end_ms > t_s * 1000 || r->end_ms == r->start_ms);
}

/**
 * @brief Images every 7 min, newest 3 min after a mark: every frame but the newest is shown
 *        at a 10-minute mark with the image current then; the newest at its own start.
 * @version 1
 * @utility
 */
static void test_grid(void)
{
    nws_radar_raster_t r[16];
    nws_radar_frame_t out[8];
    int64_t newest = NOW_S - 7 * MIN_S;   // 3 min past the mark NOW_S - 10 min
    rasters(r, 16, newest, 7 * MIN_S);
    int n = nws_radar_pick(r, 16, NOW_S, GRID_S, AGE_S, out, 8);
    CHECK(n == 8, "7 marks within 70 min + the newest, got %d", n);
    CHECK(out[n - 1].show_s == newest && out[n - 1].raster.id == 16, "newest last, own time");
    for (int i = 0; i < n - 1; i++) {
        CHECK(out[i].show_s % GRID_S == 0, "frame %d at a mark: %lld", i,
              (long long)out[i].show_s);
        CHECK(covers(&out[i].raster, out[i].show_s), "frame %d: the image current at its mark",
              i);
        CHECK(NOW_S - out[i].show_s <= AGE_S, "frame %d recent", i);
        if (i) CHECK(out[i].show_s - out[i - 1].show_s == GRID_S, "frame %d 10 min after", i);
    }
    CHECK(out[n - 2].show_s == NOW_S - 10 * MIN_S, "last mark before the newest");
}

/**
 * @brief A newest image starting exactly on a mark is that mark's frame (no duplicate).
 * @version 1
 * @utility
 */
static void test_newest_on_mark(void)
{
    nws_radar_raster_t r[16];
    nws_radar_frame_t out[8];
    rasters(r, 16, NOW_S, 7 * MIN_S);
    int n = nws_radar_pick(r, 16, NOW_S, GRID_S, AGE_S, out, 8);
    CHECK(out[n - 1].show_s == NOW_S && out[n - 1].raster.id == 16, "newest at its mark");
    CHECK(out[n - 2].show_s == NOW_S - GRID_S, "previous mark next, got %lld",
          (long long)(NOW_S - out[n - 2].show_s));
}

/**
 * @brief The cap keeps the newest frames.
 * @version 2
 * @utility
 */
static void test_cap(void)
{
    nws_radar_raster_t r[16];
    nws_radar_frame_t out[3];
    rasters(r, 16, NOW_S - 2 * MIN_S, 7 * MIN_S);
    int n = nws_radar_pick(r, 16, NOW_S, GRID_S, AGE_S, out, 3);
    CHECK(n == 3, "capped at 3, got %d", n);
    CHECK(out[2].raster.id == 16 && out[1].show_s == NOW_S - GRID_S &&
          out[0].show_s == NOW_S - 2 * GRID_S, "the newest three");
}

/**
 * @brief Input order does not matter; a mark no image covers (a server gap) is skipped.
 * @version 2
 * @utility
 */
static void test_unsorted_and_gap(void)
{
    nws_radar_raster_t r[4] = {
        { 1, (NOW_S - 34 * MIN_S) * 1000, (NOW_S - 27 * MIN_S) * 1000 },   // covers -30
        { 2, (NOW_S - 90 * MIN_S) * 1000, (NOW_S - 80 * MIN_S) * 1000 },   // too old
        { 3, (NOW_S - 1 * MIN_S) * 1000, (NOW_S - 1 * MIN_S) * 1000 },     // newest, open
        { 4, (NOW_S - 12 * MIN_S) * 1000, (NOW_S - 1 * MIN_S) * 1000 },    // covers -10
    };                                                                     // -20: gap
    nws_radar_frame_t out[8];
    int n = nws_radar_pick(r, 4, NOW_S, GRID_S, AGE_S, out, 8);
    CHECK(n == 3, "-30, -10, newest, got %d", n);
    CHECK(out[0].raster.id == 1 && out[0].show_s == NOW_S - 30 * MIN_S, "oldest first");
    CHECK(out[1].raster.id == 4 && out[1].show_s == NOW_S - 10 * MIN_S, "then -10");
    CHECK(out[2].raster.id == 3 && out[2].show_s == NOW_S - 1 * MIN_S, "newest last");
}

/**
 * @brief No rasters: no frames.
 * @version 1
 * @utility
 */
static void test_empty(void)
{
    nws_radar_frame_t out[8];
    CHECK(nws_radar_pick(NULL, 0, NOW_S, GRID_S, AGE_S, out, 8) == 0, "none");
}

/**
 * @brief Run the radar frame tests; exit status is the failure count.
 * @version 2
 * @utility
 * @return 0 when every check passed.
 */
int main(void)
{
    test_grid();
    test_newest_on_mark();
    test_cap();
    test_unsorted_and_gap();
    test_empty();
    printf("radar_frames: %d/%d checks passed\n", s_checks - s_failures, s_checks);
    return s_failures;
}
