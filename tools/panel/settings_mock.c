/**
 * @file settings_mock.c
 * v2: a pretend upload (it says what would happen).
 * v3: the sun alone.
 * v4: no global settings; every sample field set.
 * @version 4
 * @brief An in-memory settings backend with sample data, for rendering and clicking through
 *        the settings screens off the device (host preview, the panel before it talks to a
 *        clock). The sun is the real calculation for a public sample place (Chicago); "now" is
 *        a fixed sample moment so renders are repeatable. Saves keep the device's rule: a
 *        schedule where two profiles of a kind overlap is refused with the reason.
 */
#include "settings_mock.h"
#include "sky_data.h"

#include <stdio.h>
#include <string.h>

#define SAMPLE_LAT 41.88f    // Chicago: a public sample place, not the device's location
#define SAMPLE_LON -87.63f

static sched_rule_t      s_rules[SCHED_MAX_PROFILES];
static int               s_n;

static const char *const BACKGROUNDS[] = { "campfire", "night_winter", "spring-flower",
                                           "winter_afternoon", "winter_morning" };
static const char *const THEMES[][2] = { { "default", "Default" }, { "sassafras", "Sassafras" } };
static const char *const ANIMS[] = { "Hummingbird", "Snowfall", "Leaves", "None" };

/**
 * @brief Sunrise and sunset at the sample place on @p d.
 * v2: the sun alone (sky_sun_local).
 * @version 2
 * @callback
 * @return The sun times.
 */
static sched_sun_t mock_sun_fn(sched_date_t d, void *ctx)
{
    (void)ctx;
    struct tm t = { .tm_year = d.year - 1900, .tm_mon = d.month - 1, .tm_mday = d.day,
                    .tm_hour = 12, .tm_isdst = -1 };
    sched_sun_t sun;
    sky_sun_local(mktime(&t), SAMPLE_LAT, SAMPLE_LON, &sun.rise_min, &sun.set_min);
    return sun;
}

static const sched_sun_src_t SUN = { mock_sun_fn, NULL };

/**
 * @brief The sample lists.
 * @version 1
 * @callback
 * @return How many.
 */
static int mock_list(settings_list_t which, settings_choice_t *out, int max)
{
    int n = 0;
    if (which == SETTINGS_LIST_BACKGROUNDS) {
        for (; n < 5 && n < max; n++) {
            snprintf(out[n].value, sizeof(out[n].value), "A:/backgrounds/%s.png", BACKGROUNDS[n]);
            snprintf(out[n].label, sizeof(out[n].label), "%s", BACKGROUNDS[n]);
        }
    } else if (which == SETTINGS_LIST_THEMES) {
        for (; n < 2 && n < max; n++) {
            snprintf(out[n].value, sizeof(out[n].value), "%s", THEMES[n][0]);
            snprintf(out[n].label, sizeof(out[n].label), "%s", THEMES[n][1]);
        }
    } else {
        for (; n < 4 && n < max; n++) {
            snprintf(out[n].value, sizeof(out[n].value), "%s", ANIMS[n]);
            snprintf(out[n].label, sizeof(out[n].label), "%s", ANIMS[n]);
        }
    }
    return n;
}

/**
 * @brief The schedule.
 * @version 1
 * @callback
 * @return How many.
 */
static int mock_schedule_get(sched_rule_t *out, int max)
{
    int n = s_n < max ? s_n : max;
    memcpy(out, s_rules, sizeof(sched_rule_t) * (size_t)n);
    return n;
}

/**
 * @brief Now: the sample moment, Tuesday Nov 24 2026, 12:09.
 * @version 1
 * @callback
 */
static void mock_now(sched_date_t *day, int *min)
{
    *day = (sched_date_t){ 2026, 11, 24 };
    *min = 12 * 60 + 9;
}

/**
 * @brief Save the schedule, refusing overlaps like the device.
 * @version 1
 * @callback
 */
static void mock_schedule_set(const sched_rule_t *r, int n, settings_done_cb_t done, void *ctx)
{
    char why[128] = "";
    sched_date_t day;
    int min;
    mock_now(&day, &min);
    sched_overlap_t o;
    bool ok = true;
    for (int i = 0; i < n && ok; i++) {
        ok = !sched_find_overlap(r, n, i, day, 366, &SUN, &o);
        if (!ok) sched_fmt_overlap(r, &o, why, sizeof(why));
    }
    if (ok) {
        memcpy(s_rules, r, sizeof(sched_rule_t) * (size_t)n);
        s_n = n;
    }
    done(ok, why, ctx);
}

/**
 * @brief The sample sun.
 * @version 1
 * @callback
 * @return It.
 */
static const sched_sun_src_t *mock_sun(void)
{
    return &SUN;
}

/**
 * @brief Keep nothing: a sample.
 * v2: takes the stem.
 * @version 2
 * @callback
 */
static void mock_save_theme(const ui_theme_t *t, const char *stem, settings_done_cb_t done,
                            void *ctx)
{
    (void)t;
    (void)stem;
    done(true, "", ctx);
}

/**
 * @brief Sample about text.
 * @version 1
 * @callback
 */
static void mock_about(char *out, size_t n)
{
    snprintf(out, n, "Greenwood Clock\nVersion: 1.0.0\n\n(sample data: not connected)");
}

/**
 * @brief Pretend to upload.
 * @version 1
 * @callback
 */
static void mock_upload(settings_upload_t kind, settings_done_cb_t done, void *ctx)
{
    done(true, kind == SETTINGS_UPLOAD_BACKGROUND ? "Sample: a background would be sent"
                                                  : "Sample: a photo would be sent", ctx);
}

/**
 * @brief Leaving: nothing to go back to.
 * @version 1
 * @callback
 */
static void mock_leave(void)
{
}

/**
 * @brief A sample profile: every field set (60 %, the Default theme).
 * v2: every field set.
 * @version 2
 * @utility
 * @return It.
 */
static sched_rule_t sample(const char *name, sched_repeat_t rep, sched_time_t a, sched_time_t b,
                           const char *image, const char *anim)
{
    sched_rule_t r;
    memset(&r, 0, sizeof(r));
    snprintf(r.name, sizeof(r.name), "%s", name);
    r.repeat = rep;
    r.start = a;
    r.end = b;
    r.all_day = rep != SCHED_EVERY_DAY;
    r.brightness = 60;
    snprintf(r.image, sizeof(r.image), "%s", image);
    snprintf(r.theme, sizeof(r.theme), "default");
    snprintf(r.anim, sizeof(r.anim), "%s", anim);
    return r;
}

#define AT(h, m) ((sched_time_t){ SCHED_AT_CLOCK, (int16_t)((h) * 60 + (m)) })
#define SUNSET   ((sched_time_t){ SCHED_AT_SUNSET, 0 })

/**
 * @brief The sample schedule: four daily profiles and three events, every field set.
 * v2: every field set.
 * @version 2
 * @utility
 */
static void sample_schedule(void)
{
    s_rules[0] = sample("Morning", SCHED_EVERY_DAY, AT(7, 0), AT(12, 0), "A:/backgrounds/winter_morning.png", "Hummingbird");
    s_rules[1] = sample("Afternoon", SCHED_EVERY_DAY, AT(12, 0), SUNSET, "A:/backgrounds/winter_afternoon.png", "Snowfall");
    s_rules[2] = sample("Evening", SCHED_EVERY_DAY, SUNSET, AT(22, 0), "A:/backgrounds/campfire.png", "None");
    s_rules[3] = sample("Night", SCHED_EVERY_DAY, AT(22, 0), AT(7, 0), "A:/backgrounds/night_winter.png", "None");
    s_rules[3].brightness = 12;
    s_rules[4] = sample("Christmas", SCHED_YEARLY, AT(0, 0), AT(0, 0), "A:/backgrounds/winter_morning.png", "Snowfall");
    s_rules[4].from = (sched_date_t){ 0, 12, 24 };
    s_rules[4].to = (sched_date_t){ 0, 12, 25 };
    s_rules[5] = sample("Valentine's Day", SCHED_YEARLY, AT(0, 0), AT(0, 0), "A:/backgrounds/spring-flower.png", "Leaves");
    s_rules[5].from = s_rules[5].to = (sched_date_t){ 0, 2, 14 };
    s_rules[6] = sample("Birthday", SCHED_YEARLY, AT(0, 0), AT(0, 0), "A:/backgrounds/spring-flower.png", "Hummingbird");
    s_rules[6].from = s_rules[6].to = (sched_date_t){ 0, 6, 3 };
    s_n = 7;
}

/**
 * @brief The sample backend.
 * v2: a pretend upload.
 * v3: no global setters.
 * @version 3
 * @utility
 * @return It (static).
 */
const settings_backend_t *settings_mock_backend(void)
{
    static settings_backend_t be;
    if (!be.list) {
        sample_schedule();
        be = (settings_backend_t){
            .list = mock_list, .schedule_get = mock_schedule_get,
            .schedule_set = mock_schedule_set, .sun = mock_sun, .now = mock_now,
            .save_theme = mock_save_theme, .about = mock_about, .upload = mock_upload,
            .leave = mock_leave,
        };
    }
    return &be;
}
