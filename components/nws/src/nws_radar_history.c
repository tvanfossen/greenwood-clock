/**
 * @file nws_radar_history.c
 * @version 2
 * @brief Radar history on the SD card: the loop's frames as /sdcard/radar/<valid epoch>.png,
 *        the newest NWS_RADAR_HISTORY_MAX within NWS_RADAR_HISTORY_MAX_AGE_S kept. The
 *        radar screen loops them. Runs on the NWS task (save) and the FSM task (list), never
 *        under the LVGL lock.
 */
#include "nws.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include "esp_log.h"

static const char *TAG = "nws_radar_hist";

#define RADAR_HIST_DIR     "/sdcard/radar"
#define RADAR_HIST_SCAN    32           /* files considered per scan (more are pruned next time) */
#define TIME_VALID_EPOCH   1700000000   /* before this the clock has no SNTP time yet */

/**
 * @brief Build the history file path for frame time @p ts.
 * @version 1
 * @req REQ-RADAR-003
 */
void nws_radar_history_path(time_t ts, char *out, size_t out_sz)
{
    snprintf(out, out_sz, RADAR_HIST_DIR "/%lld.png", (long long)ts);
}

/**
 * @brief Parse a history file name ("<epoch>.png") into its frame time.
 * @version 1
 * @dg_internal
 * @return The frame time, or 0 if the name is not a history frame.
 */
static time_t parse_frame_name(const char *name)
{
    char *end = NULL;
    long long v = strtoll(name, &end, 10);
    bool ok = end && end != name && strcmp(end, ".png") == 0 && v > TIME_VALID_EPOCH;
    return ok ? (time_t)v : 0;
}

/**
 * @brief qsort comparator: ascending frame time.
 * @version 1
 * @dg_internal
 * @return <0, 0 or >0.
 */
static int cmp_time(const void *a, const void *b)
{
    time_t x = *(const time_t *)a, y = *(const time_t *)b;
    return (x > y) - (x < y);
}

/**
 * @brief Read up to RADAR_HIST_SCAN frame times from the history directory, ascending.
 * @version 1
 * @dg_internal
 * @return Number of frame times written to @p ts (0 if the directory is missing).
 */
static int scan_frames(time_t *ts)
{
    DIR *d = opendir(RADAR_HIST_DIR);
    if (!d) return 0;
    int n = 0;
    struct dirent *e;
    while (n < RADAR_HIST_SCAN && (e = readdir(d)) != NULL) {
        time_t t = parse_frame_name(e->d_name);
        if (t) ts[n++] = t;
    }
    closedir(d);
    qsort(ts, (size_t)n, sizeof(time_t), cmp_time);
    return n;
}

/**
 * @brief Delete frames beyond the newest NWS_RADAR_HISTORY_MAX or older than the max age.
 * @version 1
 * @dg_internal
 */
static void prune_frames(time_t now)
{
    time_t ts[RADAR_HIST_SCAN];
    int n = scan_frames(ts);
    for (int i = 0; i < n; i++) {
        bool too_many = i < n - NWS_RADAR_HISTORY_MAX;
        bool too_old  = now - ts[i] > NWS_RADAR_HISTORY_MAX_AGE_S;
        if (!too_many && !too_old) continue;
        char path[48];
        nws_radar_history_path(ts[i], path, sizeof(path));
        if (remove(path) != 0) ESP_LOGW(TAG, "prune %s failed: errno %d", path, errno);
    }
}

/**
 * @brief Write one fetched radar PNG as the history frame valid at @p ts, then prune. Skipped
 *        (logged) before SNTP time is set or if the SD card is unavailable.
 * v2: stamped with the image's valid time (was the fetch time, ~10 min late and the same
 *     image twice when the server had nothing newer).
 * @version 2
 * @req REQ-RADAR-003
 */
void nws_radar_history_save(const uint8_t *png, size_t len, time_t ts)
{
    time_t now = time(NULL);
    if (!png || len == 0 || now < TIME_VALID_EPOCH || ts < TIME_VALID_EPOCH) {
        ESP_LOGW(TAG, "not saved: %s", now < TIME_VALID_EPOCH ? "no wall-clock time yet" : "empty PNG");
        return;
    }
    if (mkdir(RADAR_HIST_DIR, 0775) != 0 && errno != EEXIST) {
        ESP_LOGW(TAG, "not saved: mkdir %s failed (errno %d, SD card missing?)", RADAR_HIST_DIR, errno);
        return;
    }
    char path[48];
    nws_radar_history_path(ts, path, sizeof(path));
    FILE *f = fopen(path, "wb");
    size_t wrote = f ? fwrite(png, 1, len, f) : 0;
    if (f) fclose(f);
    if (wrote != len) {
        ESP_LOGE(TAG, "write %s failed (%zu of %zu B)", path, wrote, len);
        remove(path);
        return;
    }
    prune_frames(now);
    ESP_LOGI(TAG, "saved %s (%zu B)", path, len);
}

/**
 * @brief Whether the history holds the frame valid at @p ts.
 * @version 1
 * @req REQ-RADAR-003
 * @return true if its file exists.
 */
bool nws_radar_history_has(time_t ts)
{
    char path[48];
    struct stat st;
    nws_radar_history_path(ts, path, sizeof(path));
    return stat(path, &st) == 0;
}

/**
 * @brief True if @p t is one of the @p n times in @p set.
 * @version 1
 * @dg_internal
 * @return Membership.
 */
static bool in_set(time_t t, const time_t *set, int n)
{
    for (int i = 0; i < n; i++) {
        if (set[i] == t) return true;
    }
    return false;
}

/**
 * @brief Delete every history frame whose time is not among the @p n in @p keep (frames the
 *        loop no longer uses, and old fetch-time-stamped ones).
 * @version 1
 * @req REQ-RADAR-003
 */
void nws_radar_history_keep(const time_t *keep, int n)
{
    time_t ts[RADAR_HIST_SCAN];
    int count = scan_frames(ts);
    for (int i = 0; i < count; i++) {
        if (in_set(ts[i], keep, n)) continue;
        char path[48];
        nws_radar_history_path(ts[i], path, sizeof(path));
        if (remove(path) != 0) ESP_LOGW(TAG, "drop %s failed: errno %d", path, errno);
        else ESP_LOGI(TAG, "dropped %s (not a loop frame)", path);
    }
}

/**
 * @brief List the history frames still within the max age, ascending, at most @p max.
 * @version 1
 * @req REQ-RADAR-003
 * @return Number of frame times written to @p ts_out.
 */
int nws_radar_history_list(time_t *ts_out, int max)
{
    time_t ts[RADAR_HIST_SCAN];
    int n = scan_frames(ts);
    time_t now = time(NULL);
    int first = n > max ? n - max : 0;
    int out = 0;
    for (int i = first; i < n; i++) {
        if (now - ts[i] <= NWS_RADAR_HISTORY_MAX_AGE_S) ts_out[out++] = ts[i];
    }
    return out;
}
