/**
 * @file test_library.c
 * @version 1
 * @brief Host tests for the Library's rules: who uses an item, safe names, renames carried
 *        into the profiles, thumbnails averaged from the picture.
 */
#include "library.h"

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
 * @brief A profile @p name using @p image, @p theme and @p anim.
 * @version 1
 * @utility
 * @return It.
 */
static sched_rule_t prof(const char *name, const char *image, const char *theme, const char *anim)
{
    sched_rule_t r;
    memset(&r, 0, sizeof(r));
    snprintf(r.name, sizeof(r.name), "%s", name);
    snprintf(r.image, sizeof(r.image), "%s", image);
    snprintf(r.theme, sizeof(r.theme), "%s", theme);
    snprintf(r.anim, sizeof(r.anim), "%s", anim);
    return r;
}

/**
 * @brief Who uses what; renames follow.
 * @version 1
 * @utility
 */
static void test_used_and_rename(void)
{
    sched_rule_t r[3] = {
        prof("Morning", "A:/backgrounds/winter.png", "default", "Snowfall"),
        prof("Night", "A:/backgrounds/night.png", "default", "None"),
        prof("Christmas", "A:/backgrounds/winter.png", "sassafras", "Snowfall"),
    };
    const char *names[4];
    int n = lib_used_by(r, 3, LIB_BACKGROUNDS, "A:/backgrounds/winter.png", names, 4);
    CHECK(n == 2 && strcmp(names[0], "Morning") == 0 && strcmp(names[1], "Christmas") == 0,
          "winter used by Morning and Christmas (%d)", n);
    CHECK(lib_used_by(r, 3, LIB_THEMES, "default", names, 1) == 2, "default counted past max");
    CHECK(lib_used_by(r, 3, LIB_ANIMS, "Leaves", names, 4) == 0, "Leaves unused");
    CHECK(lib_used_by(r, 3, LIB_ALBUM, "A:/photos/a.png", names, 4) == 0, "photos: no profile");
    CHECK(lib_rename_refs(r, 3, LIB_BACKGROUNDS, "A:/backgrounds/winter.png", "A:/backgrounds/snow.png") == 2,
          "two profiles renamed");
    CHECK(strcmp(r[0].image, "A:/backgrounds/snow.png") == 0 && strcmp(r[2].image, "A:/backgrounds/snow.png") == 0 &&
          strcmp(r[1].image, "A:/backgrounds/night.png") == 0, "only theirs changed");
    CHECK(lib_rename_refs(r, 3, LIB_THEMES, "sassafras", "rose") == 1 && strcmp(r[2].theme, "rose") == 0,
          "theme rename");
    CHECK(lib_use_in(&r[1], LIB_BACKGROUNDS, "snow") && strcmp(r[1].image, "A:/backgrounds/snow.png") == 0,
          "use a background: %s", r[1].image);
    CHECK(lib_use_in(&r[1], LIB_ANIMS, "Leaves") && strcmp(r[1].anim, "Leaves") == 0, "use an animation");
    CHECK(!lib_use_in(&r[1], LIB_ALBUM, "a"), "the album is no profile's");
}

/**
 * @brief Safe names.
 * @version 1
 * @utility
 */
static void test_names(void)
{
    CHECK(lib_name_ok("winter_morning-2"), "letters, digits, - and _");
    CHECK(!lib_name_ok(""), "empty");
    CHECK(!lib_name_ok("../etc"), "no path");
    CHECK(!lib_name_ok("a b"), "no spaces");
    CHECK(!lib_name_ok("x.png"), "no dots");
    char long_name[LIB_NAME_MAX + 2];
    memset(long_name, 'a', sizeof(long_name) - 1);
    long_name[sizeof(long_name) - 1] = '\0';
    CHECK(!lib_name_ok(long_name), "too long");
    char ref[64];
    lib_ref(LIB_BACKGROUNDS, "winter", ref, sizeof(ref));
    CHECK(strcmp(ref, "A:/backgrounds/winter.png") == 0, "background ref %s", ref);
    lib_ref(LIB_THEMES, "rose", ref, sizeof(ref));
    CHECK(strcmp(ref, "rose") == 0, "theme ref %s", ref);
}

/**
 * @brief Thumbnails: the average of each area, BGRA to RGBA, opaque.
 * @version 1
 * @utility
 */
static void test_thumb(void)
{
    enum { W = 345, H = 200 };   // not 2:1, so a thumbnail pixel straddles the middle
    static uint8_t src[W * H * 4];
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            uint8_t *p = &src[(y * W + x) * 4];
            bool left = x < W / 2;
            p[0] = left ? 200 : 10;   // B
            p[1] = 100;               // G
            p[2] = left ? 0 : 250;    // R
            p[3] = 255;
        }
    }
    static uint8_t out[LIB_THUMB_W * LIB_THUMB_H * 4];
    lib_thumb_scale(src, W, H, W * 4, out, LIB_THUMB_W, LIB_THUMB_H);
    const uint8_t *l = &out[(50 * LIB_THUMB_W + 10) * 4], *r = &out[(50 * LIB_THUMB_W + 160) * 4];
    CHECK(l[0] == 0 && l[2] == 200 && l[1] == 100 && l[3] == 255, "left: RGBA %d %d %d %d", l[0], l[1], l[2], l[3]);
    CHECK(r[0] == 250 && r[2] == 10, "right: R %d B %d", r[0], r[2]);
    const uint8_t *mid = &out[(50 * LIB_THUMB_W + 84) * 4];
    CHECK(mid[0] < 250 && mid[2] < 200, "the edge averages: R %d B %d", mid[0], mid[2]);
}

/**
 * @brief Run the Library tests; exit status is the failure count.
 * @version 1
 * @utility
 * @return 0 when every check passed.
 */
int main(void)
{
    test_used_and_rename();
    test_names();
    test_thumb();
    printf("library: %d/%d checks passed\n", s_checks - s_failures, s_checks);
    return s_failures;
}
