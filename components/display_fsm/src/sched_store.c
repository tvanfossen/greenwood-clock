/**
 * @file sched_store.c
 * v2: sched_store_sun_table; sched_store_today public.
 * v3: safe before sched_store_init (the HTTP server runs during the launch window, before
 *     the display task loads the schedule): empty answers, refused saves.
 * @version 3
 * @brief The device's schedule: file, RAM copy, overlap-checked saves, the active profile and
 *        the device's sun (see sched_store.h).
 */
#include "sched_store.h"
#include "sky_data.h"
#include "settings.h"
#include "bg_schedule.h"
#include "clock_anim_pref.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "sched_store";

#define FILE_MAX 16384
#define CHECK_DAYS 366

static SemaphoreHandle_t s_lock;            // guards the profiles and the sun cache
static float             s_lat, s_lon;      // the location, read at init and on each save
static sched_rule_t      s_rules[SCHED_MAX_PROFILES];
static int               s_count;

// ---------------------------------------------------------------------------
// The device's sun
// ---------------------------------------------------------------------------

/**
 * @brief Re-read the location from the settings.
 * @version 1
 * @utility
 */
static void read_location(void)
{
    clock_settings_t cfg;
    settings_load(&cfg);
    s_lat = cfg.latitude;
    s_lon = cfg.longitude;
}

/**
 * @brief Sunrise and sunset on local date @p d at the configured location (cached per date;
 *        6:00/18:00 where the sun does not rise or set). Called with s_lock held.
 * @version 1
 * @callback
 * @return The sun times.
 */
static sched_sun_t device_sun(sched_date_t d, void *ctx)
{
    (void)ctx;
    static sched_date_t last;
    static sched_sun_t  cached;
    if (memcmp(&last, &d, sizeof(d)) != 0) {
        struct tm t = { .tm_year = d.year - 1900, .tm_mon = d.month - 1, .tm_mday = d.day,
                        .tm_hour = 12, .tm_isdst = -1 };
        sky_data_t sky;
        sky_data_compute(mktime(&t), s_lat, s_lon, 0.0f, &sky);
        cached = sky.sunrise_min >= 0 ? (sched_sun_t){ sky.sunrise_min, sky.sunset_min }
                                      : (sched_sun_t){ 6 * 60, 18 * 60 };
        last = d;
    }
    return cached;
}

static const sched_sun_src_t SUN = { device_sun, NULL };   // for callers holding s_lock

/**
 * @brief device_sun for callers outside the store: takes s_lock around the cache (6:00/18:00
 *        before init).
 * v2: 6:00/18:00 before init.
 * @version 2
 * @callback
 * @return The sun times.
 */
static sched_sun_t locked_sun(sched_date_t d, void *ctx)
{
    sched_sun_t s = { 6 * 60, 18 * 60 };
    if (s_lock) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        s = device_sun(d, ctx);
        xSemaphoreGive(s_lock);
    }
    return s;
}

static const sched_sun_src_t LOCKED_SUN = { locked_sun, NULL };

/**
 * @brief The device's sun source (safe from any task).
 * v2: takes the store's lock (the settings screens call it from the LVGL task).
 * @version 2
 * @req REQ-BG-004
 * @return It.
 */
const sched_sun_src_t *sched_store_sun(void)
{
    return &LOCKED_SUN;
}

// ---------------------------------------------------------------------------
// File
// ---------------------------------------------------------------------------

/**
 * @brief Read SCHED_STORE_PATH whole.
 * @version 1
 * @utility
 * @return The text (free it), or NULL when missing, empty or too large.
 */
static char *read_file(void)
{
    FILE *f = fopen(SCHED_STORE_PATH, "rb");
    char *buf = f ? calloc(1, FILE_MAX + 1) : NULL;
    size_t n = buf ? fread(buf, 1, FILE_MAX, f) : 0;
    if (f) fclose(f);
    if (buf && (n == 0 || n == FILE_MAX)) {
        ESP_LOGW(TAG, "%s: %s", SCHED_STORE_PATH, n ? "larger than 16 KB" : "empty");
        free(buf);
        buf = NULL;
    }
    return buf;
}

/**
 * @brief Write @p n profiles to SCHED_STORE_PATH (via a temporary file, renamed over it).
 * @version 1
 * @utility
 * @return ESP_OK, or ESP_FAIL (logged).
 */
static esp_err_t write_file(const sched_rule_t *r, int n)
{
    char *json = sched_to_json(r, n);
    FILE *f = json ? fopen(SCHED_STORE_PATH ".tmp", "wb") : NULL;
    bool ok = f && fwrite(json, 1, strlen(json), f) == strlen(json);
    if (f) ok = fclose(f) == 0 && ok;
    if (ok) {
        remove(SCHED_STORE_PATH);   // FAT's rename does not replace an existing file
        ok = rename(SCHED_STORE_PATH ".tmp", SCHED_STORE_PATH) == 0;
    }
    if (!ok) ESP_LOGE(TAG, "write %s failed", SCHED_STORE_PATH);
    free(json);
    return ok ? ESP_OK : ESP_FAIL;
}

// ---------------------------------------------------------------------------
// Converting the old weekly schedule
// ---------------------------------------------------------------------------

/**
 * @brief One old profile's look as a daily profile from @p start to @p end (minutes).
 * @version 1
 * @utility
 * @return The profile.
 */
static sched_rule_t from_old(const bg_profile_t *p, int start, int end, bool all_day)
{
    sched_rule_t r;
    memset(&r, 0, sizeof(r));
    strlcpy(r.name, p->name, sizeof(r.name));
    r.repeat = SCHED_EVERY_DAY;
    r.all_day = all_day;
    r.start = (sched_time_t){ SCHED_AT_CLOCK, (int16_t)start };
    r.end = (sched_time_t){ SCHED_AT_CLOCK, (int16_t)(end % (24 * 60)) };
    strlcpy(r.image, p->image, sizeof(r.image));
    strlcpy(r.theme, p->theme, sizeof(r.theme));
    if (p->anim != BG_INHERIT) strlcpy(r.anim, clock_anim_name((clock_anim_t)p->anim), sizeof(r.anim));
    r.brightness = p->brightness == BG_INHERIT ? SCHED_INHERIT : p->brightness;
    return r;
}

/**
 * @brief Convert the old weekly schedule (its first weekday's slots) into daily profiles.
 * @version 1
 * @utility
 * @return How many profiles were made.
 */
static int convert_old(sched_rule_t *out)
{
    bg_schedule_t old;
    bg_schedule_get(&old);
    int n = 0, slots = old.slot_count;
    for (int d = 1; d < BG_SCHED_DAYS; d++) {
        if (memcmp(old.grid[d], old.grid[0], sizeof(old.grid[0])) != 0) {
            ESP_LOGW(TAG, "old schedule differs by weekday: kept day 0's slots only");
            break;
        }
    }
    for (int i = 0; i < slots && n < SCHED_MAX_PROFILES; i++) {
        int next = old.slot_start[(i + 1) % slots] + (i + 1 == slots ? 24 * 60 : 0);
        out[n++] = from_old(&old.profiles[old.grid[0][i]], old.slot_start[i], next, slots == 1);
    }
    return n;
}

// ---------------------------------------------------------------------------
// API
// ---------------------------------------------------------------------------

/**
 * @brief Load the schedule, or convert the old one.
 * @version 1
 * @req REQ-BG-004
 * @return ESP_OK, or ESP_ERR_NO_MEM.
 */
esp_err_t sched_store_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    read_location();
    char *json = s_lock ? read_file() : NULL;
    int n = json ? sched_from_json(json, s_rules, SCHED_MAX_PROFILES) : -1;
    free(json);
    if (n < 0) {
        n = convert_old(s_rules);
        ESP_LOGI(TAG, "no valid %s: converted the weekly schedule into %d daily profiles",
                 SCHED_STORE_PATH, n);
        write_file(s_rules, n);
    }
    s_count = n;
    ESP_LOGI(TAG, "schedule: %d profiles", s_count);
    return s_lock ? ESP_OK : ESP_ERR_NO_MEM;
}

/**
 * @brief Copy the profiles.
 * v2: none before init.
 * @version 2
 * @req REQ-BG-004
 * @return How many.
 */
int sched_store_get(sched_rule_t *out, int max)
{
    int n = 0;
    if (s_lock) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        n = s_count < max ? s_count : max;
        memcpy(out, s_rules, sizeof(sched_rule_t) * (size_t)n);
        xSemaphoreGive(s_lock);
    }
    return n;
}

/**
 * @brief Today as a local date.
 * @version 1
 * @req REQ-BG-004
 * @return The date.
 */
sched_date_t sched_store_today(void)
{
    time_t now = time(NULL);
    struct tm t;
    localtime_r(&now, &t);
    return (sched_date_t){ (uint16_t)(t.tm_year + 1900), (uint8_t)(t.tm_mon + 1),
                           (uint8_t)t.tm_mday };
}

/**
 * @brief Check, write and adopt a schedule.
 * v2: sched_store_today.
 * v3: refused before init.
 * @version 3
 * @req REQ-BG-004
 * @return ESP_OK, ESP_ERR_INVALID_ARG, or the write's error.
 */
esp_err_t sched_store_set(const sched_rule_t *r, int n, char *why, size_t why_n)
{
    esp_err_t err = n >= 0 && n <= SCHED_MAX_PROFILES ? ESP_OK : ESP_ERR_INVALID_ARG;
    if (!s_lock) {
        if (why) snprintf(why, why_n, "The schedule is not loaded yet");
        return ESP_ERR_INVALID_STATE;
    }
    sched_overlap_t o;
    read_location();
    xSemaphoreTake(s_lock, portMAX_DELAY);   // the sun cache is shared with sched_store_active
    for (int i = 0; i < n && err == ESP_OK; i++) {
        if (sched_find_overlap(r, n, i, sched_store_today(), CHECK_DAYS, &SUN, &o)) {
            if (why) sched_fmt_overlap(r, &o, why, why_n);
            ESP_LOGW(TAG, "refused: %s overlaps %s", r[i].name, r[o.other].name);
            err = ESP_ERR_INVALID_ARG;
        }
    }
    xSemaphoreGive(s_lock);
    if (err == ESP_OK) err = write_file(r, n);
    if (err == ESP_OK) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        memcpy(s_rules, r, sizeof(sched_rule_t) * (size_t)n);
        s_count = n;
        xSemaphoreGive(s_lock);
        ESP_LOGI(TAG, "schedule saved: %d profiles", n);
    }
    return err;
}

/**
 * @brief The profile showing at a local time.
 * v2: none before init.
 * @version 2
 * @req REQ-BG-004
 * @return Its index, or -1.
 */
int sched_store_active(const struct tm *now, sched_rule_t *out)
{
    sched_date_t d = { (uint16_t)(now->tm_year + 1900), (uint8_t)(now->tm_mon + 1),
                       (uint8_t)now->tm_mday };
    int idx = -1;
    if (s_lock) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
        idx = sched_active(s_rules, s_count, d, now->tm_hour * 60 + now->tm_min, &SUN);
        if (out && idx >= 0) *out = s_rules[idx];
        xSemaphoreGive(s_lock);
    }
    if (out && idx < 0) {
        memset(out, 0, sizeof(*out));
        out->brightness = SCHED_INHERIT;
    }
    return idx;
}

/**
 * @brief The sun for a run of days.
 * v2: through locked_sun (safe before init).
 * @version 2
 * @req REQ-BG-004
 */
void sched_store_sun_table(sched_date_t from, int days, sched_sun_t *out)
{
    sched_date_t d = from;
    for (int i = 0; i < days; i++) {
        out[i] = locked_sun(d, NULL);   // the sun cache is shared
        struct tm t = { .tm_year = d.year - 1900, .tm_mon = d.month - 1, .tm_mday = d.day + 1,
                        .tm_hour = 12, .tm_isdst = -1 };
        mktime(&t);   // normalises the day after
        d = (sched_date_t){ (uint16_t)(t.tm_year + 1900), (uint8_t)(t.tm_mon + 1), (uint8_t)t.tm_mday };
    }
}
