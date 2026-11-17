/**
 * @file tvg_harness.c
 * @brief Render every displayed frame of a Lottie file with whichever ThorVG it is linked to.
 * @version 1
 *
 * Built twice by `inv tvg-compare` (against ThorVG 0.15.3 = device, and 1.x = latest) so
 * the two engines can be compared on identical inputs. Writes, per file:
 *   <out>/<name>.rgba   frames 1..N-1 (lv_lottie skips frame 0), size x size, straight RGBA
 * and prints one line: name, frames, load result, mean/max microseconds per frame.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "thorvg_capi.h"

#if defined(TVG_HARNESS_V1)
#define ENGINE_INIT()      tvg_engine_init(0)
#define ENGINE_TERM()      tvg_engine_term()
#define CANVAS_CREATE()    tvg_swcanvas_create(TVG_ENGINE_OPTION_DEFAULT)
#define CANVAS_ADD(c, p)   tvg_canvas_add((c), (p))
#define CANVAS_DRAW(c)     tvg_canvas_draw((c), false)
typedef Tvg_Canvas  canvas_t;
typedef Tvg_Paint   paint_t;
typedef Tvg_Animation anim_t;
#else
#define ENGINE_INIT()      tvg_engine_init(TVG_ENGINE_SW, 0)
#define ENGINE_TERM()      tvg_engine_term(TVG_ENGINE_SW)
#define CANVAS_CREATE()    tvg_swcanvas_create()
#define CANVAS_ADD(c, p)   tvg_canvas_push((c), (p))
#define CANVAS_DRAW(c)     tvg_canvas_draw((c))
typedef Tvg_Canvas *canvas_t;
typedef Tvg_Paint  *paint_t;
typedef Tvg_Animation *anim_t;
#endif

/**
 * @brief Monotonic time in microseconds.
 * @version 1
 * @dg_internal
 * @return Microseconds.
 */
static double now_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e6 + ts.tv_nsec / 1e3;
}

/**
 * @brief Render one file; write frames to @p out_path (NULL = timing only).
 * @version 1
 * @req REQ-TOOL-003
 * @return 0 on success, nonzero on a ThorVG failure (reported).
 */
static int render_file(const char *path, const char *out_path, uint32_t size)
{
    uint32_t *buf = calloc((size_t)size * size, 4);
    canvas_t canvas = CANVAS_CREATE();
    tvg_swcanvas_set_target(canvas, buf, size, size, size, TVG_COLORSPACE_ABGR8888S);
    anim_t anim = tvg_animation_new();
    paint_t pic = tvg_animation_get_picture(anim);
    Tvg_Result r = tvg_picture_load(pic, path);
    if (r != TVG_RESULT_SUCCESS) {
        printf("%s\tLOAD_FAILED\t%d\n", path, (int)r);
        free(buf);
        return 1;
    }
    tvg_picture_set_size(pic, (float)size, (float)size);
    CANVAS_ADD(canvas, pic);
    float total = 0.0f;
    tvg_animation_get_total_frame(anim, &total);
    FILE *out = out_path ? fopen(out_path, "wb") : NULL;
    double sum = 0.0, worst = 0.0;
    int frames = 0, failures = 0;
    for (int f = (total > 1.0f ? 1 : 0); f < (int)total; f++, frames++) {
        memset(buf, 0, (size_t)size * size * 4);
        double t0 = now_us();
        tvg_animation_set_frame(anim, (float)f);
        failures += tvg_canvas_update(canvas) != TVG_RESULT_SUCCESS;
        failures += CANVAS_DRAW(canvas) != TVG_RESULT_SUCCESS;
        failures += tvg_canvas_sync(canvas) != TVG_RESULT_SUCCESS;
        double dt = now_us() - t0;
        sum += dt;
        if (dt > worst) worst = dt;
        if (out) fwrite(buf, 4, (size_t)size * size, out);
    }
    if (out) fclose(out);
    printf("%s\t%d\t%d\t%.1f\t%.1f\n", path, frames, failures, frames ? sum / frames : 0.0, worst);
    tvg_animation_del(anim);
    tvg_canvas_destroy(canvas);
    free(buf);
    return failures != 0;
}

/**
 * @brief Usage: tvg_harness SIZE OUT_DIR|- FILE...  (OUT_DIR "-" = timing only).
 * @version 1
 * @req REQ-TOOL-003
 * @return Number of files that failed.
 */
int main(int argc, char **argv)
{
    if (argc < 4) {
        fprintf(stderr, "usage: %s SIZE OUT_DIR|- FILE...\n", argv[0]);
        return 2;
    }
    uint32_t size = (uint32_t)atoi(argv[1]);
    const char *out_dir = argv[2];
    ENGINE_INIT();
    int failed = 0;
    for (int i = 3; i < argc; i++) {
        char out_path[1024];
        const char *base = strrchr(argv[i], '/');
        base = base ? base + 1 : argv[i];
        if (strcmp(out_dir, "-") != 0) {
            snprintf(out_path, sizeof(out_path), "%s/%d_%s.rgba", out_dir, i, base);
        }
        failed += render_file(argv[i], strcmp(out_dir, "-") ? out_path : NULL, size);
    }
    ENGINE_TERM();
    return failed;
}
