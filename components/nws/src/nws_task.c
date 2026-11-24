/**
 * @file nws_task.c
 * @version 1
 * @brief FreeRTOS task: staggered polling of NWS endpoints, delivering events to
 *        the display FSM.  Single task, priority 3, 12 KB stack.
 */

#include "nws_internal.h"

static const char *TAG = "nws_task";

// Event delivery callback — set by nws_init(), NULL-safe
static nws_event_cb_t s_event_cb;

/**
 * @brief Deliver @p evt through the registered event callback, if one is set.
 * @version 1
 * @utility
 */
static void send_event(const display_event_t *evt)
{
    if (s_event_cb) {
        s_event_cb(evt);
    }
}

// Poll intervals (seconds)
#define CONDITIONS_INTERVAL_S   (15 * 60)   // 15 minutes
#define FORECAST_INTERVAL_S     (60 * 60)   // 1 hour
#define ALERTS_INTERVAL_S       (5 * 60)    // 5 minutes
#define RADAR_INTERVAL_S        (10 * 60)   // 10 minutes
#define KP_INTERVAL_S           (3 * 3600)  // 3 hours

// Stagger initial fetches — a longer initial delay lets WiFi + the ESP32-C6 SDIO
// coprocessor stabilize, and pre-seeding (see nws_task_fn) spreads each source's
// FIRST fetch across separate poll cycles. The boot-time burst of back-to-back TLS
// fetches is what trips the known C6 SDIO panic; one light fetch per cycle avoids it.
#define INITIAL_DELAY_S         30
#define STAGGER_S               10

// Aurora visibility threshold for latitude ~42°N
#define AURORA_KP_THRESHOLD     6.0f

// Backoff when all fetches fail (network down / DRAM exhaustion)
#define BACKOFF_BASE_S          300     // 5 minutes
#define BACKOFF_MAX_S           1800    // 30 minutes
#define BACKOFF_HEAP_MIN        4000    // skip fetches if internal DRAM below 4KB

// ---------------------------------------------------------------------------
// Cached data (thread-safe: written only by nws_task, read by accessors)
// ---------------------------------------------------------------------------

static nws_conditions_t s_conditions;
static nws_forecast_t   s_forecast;
static nws_alerts_t     s_alerts;
static nws_location_t   s_location;
static float            s_kp_index;

// Cached radar PNG (SPIRAM-allocated, swapped on each successful fetch)
static uint8_t         *s_radar_png     = NULL;
static size_t           s_radar_png_len = 0;
static time_t           s_radar_ts      = 0;      // valid time of s_radar_png

// Previous alert state for change detection
static int              s_prev_alert_count;
static char             s_prev_alert_event[64];

/**
 * @brief Last fetched current conditions (written only by nws_task).
 * @version 1
 * @req REQ-WX-002
 * @return Pointer to the cached conditions; check its valid flag.
 */
const nws_conditions_t *nws_get_conditions(void) { return &s_conditions; }
/**
 * @brief Last fetched forecast (written only by nws_task).
 * @version 1
 * @req REQ-WX-003
 * @return Pointer to the cached forecast; check its valid flag.
 */
const nws_forecast_t   *nws_get_forecast(void)   { return &s_forecast; }
/**
 * @brief Last fetched active alerts (written only by nws_task).
 * @version 1
 * @req REQ-ALERT-001
 * @return Pointer to the cached alerts; check its valid flag.
 */
const nws_alerts_t     *nws_get_alerts(void)     { return &s_alerts; }
/**
 * @brief Last fetched planetary Kp index.
 * @version 1
 * @req REQ-ASTRO-001
 * @return Kp value, 0.0 until the first successful fetch.
 */
float                   nws_get_kp_index(void)   { return s_kp_index; }

/**
 * @brief Cached radar PNG from the last successful fetch.
 * @version 1
 * @req REQ-RADAR-001
 * @return SPIRAM PNG buffer (valid until the next radar fetch; caller must not
 *         free), or NULL if none cached. @p len_out receives its length if non-NULL.
 */
const uint8_t *nws_get_radar_png(size_t *len_out)
{
    if (len_out) *len_out = s_radar_png_len;
    return s_radar_png;
}

/**
 * @brief When the cached radar image is valid from.
 * @version 1
 * @req REQ-RADAR-001
 * @return Epoch seconds, or 0 if none cached.
 */
time_t nws_get_radar_ts(void)
{
    return s_radar_ts;
}

// Precipitation detection via PNG size heuristic.
// An all-transparent 1024×600 PNG32 compresses to ~5-15KB.
// Meaningful reflectivity data pushes well past 20KB.
#define RADAR_PRECIP_SIZE_THRESHOLD 20000

/**
 * @brief Whether the cached radar image indicates precipitation (PNG > 20000 B).
 * @version 1
 * @req REQ-RADAR-002
 * @return true if a radar PNG is cached and exceeds the size threshold.
 */
bool nws_radar_has_precipitation(void)
{
    return (s_radar_png != NULL && s_radar_png_len > RADAR_PRECIP_SIZE_THRESHOLD);
}

// ---------------------------------------------------------------------------
// Alert change detection
// ---------------------------------------------------------------------------

/**
 * @brief Compare @p new_alerts with the last snapshot (count and first event name).
 * @version 1
 * @req REQ-ALERT-001
 * @return true if the alert count or the first alert's event differs.
 */
static bool alerts_changed(const nws_alerts_t *new_alerts)
{
    if (new_alerts->alert_count != s_prev_alert_count) return true;
    if (new_alerts->alert_count > 0 &&
        strcmp(new_alerts->alerts[0].event, s_prev_alert_event) != 0) return true;
    return false;
}

/**
 * @brief Record the alert count and first event name for later change detection.
 * @version 1
 * @utility
 */
static void alerts_snapshot(const nws_alerts_t *alerts)
{
    s_prev_alert_count = alerts->alert_count;
    if (alerts->alert_count > 0) {
        strncpy(s_prev_alert_event, alerts->alerts[0].event,
                sizeof(s_prev_alert_event) - 1);
        s_prev_alert_event[sizeof(s_prev_alert_event) - 1] = '\0';
    } else {
        s_prev_alert_event[0] = '\0';
    }
}

// ---------------------------------------------------------------------------
// Task
// ---------------------------------------------------------------------------

// Per-source last-fetch ticks and per-cycle attempt/fail counters.
typedef struct {
    TickType_t conditions, forecast, alerts, radar, kp;
} poll_ticks_t;

typedef struct {
    int attempted, failed;
} poll_stats_t;

// True if a source is due (never fetched, or its interval has elapsed).
/**
 * @brief Decide whether a source is due for a fetch.
 * @version 1
 * @req REQ-WX-004
 * @return true if never fetched (@p last == 0) or @p interval_s has elapsed.
 */
static bool poll_due(TickType_t now, TickType_t last, uint32_t interval_s)
{
    return last == 0 || (now - last) >= pdMS_TO_TICKS(interval_s * 1000);
}

// Fabricate a "last fetch" tick so a source first becomes due `due_in_s` seconds
// after `t0`. Used to spread each source's first fetch across separate poll cycles
// at boot (unsigned tick wraparound makes now-last resolve to the true elapsed).
/**
 * @brief Compute a synthetic last-fetch tick so a source first falls due
 *        @p due_in_s seconds after @p t0.
 * @version 1
 * @req REQ-WX-004
 * @return The seeded tick, never 0 (0 means "never fetched").
 */
static TickType_t seed_due_in(TickType_t t0, uint32_t interval_s, uint32_t due_in_s)
{
    TickType_t last = t0 - pdMS_TO_TICKS((interval_s - due_in_s) * 1000);
    return last == 0 ? 1 : last;   // 0 means "never fetched" (immediately due)
}

/**
 * @brief Send a payload-less display event of @p type.
 * @version 1
 * @utility
 */
static void send_simple_event(display_event_type_t type)
{
    display_event_t evt = {};
    evt.type = type;
    send_event(&evt);
}

// Stagger between fetches in a cycle, then refresh `now`.
/**
 * @brief Sleep STAGGER_S (10 s) between fetches in a cycle, then refresh @p now.
 * @version 1
 * @req REQ-WX-004
 */
static void poll_stagger(TickType_t *now)
{
    vTaskDelay(pdMS_TO_TICKS(STAGGER_S * 1000));
    *now = xTaskGetTickCount();
}

/**
 * @brief If due (15 min), fetch current conditions, emit DISPLAY_EVT_WEATHER_UPDATE
 *        on success, count the attempt/failure and stagger.
 * @version 1
 * @req REQ-WX-002
 * @req REQ-WX-004
 */
static void poll_conditions(TickType_t *now, TickType_t *last, poll_stats_t *st)
{
    if (!poll_due(*now, *last, CONDITIONS_INTERVAL_S)) return;
    st->attempted++;
    esp_err_t err = nws_fetch_conditions(&s_location, &s_conditions);
    if (err == ESP_OK) {
        send_simple_event(DISPLAY_EVT_WEATHER_UPDATE);
    } else {
        st->failed++;
        ESP_LOGW(TAG, "conditions fetch failed: %s", esp_err_to_name(err));
    }
    *last = xTaskGetTickCount();
    poll_stagger(now);
}

/**
 * @brief If due (1 h), fetch the forecast, emit DISPLAY_EVT_FORECAST_UPDATE on
 *        success, count the attempt/failure and stagger.
 * @version 1
 * @req REQ-WX-003
 * @req REQ-WX-004
 */
static void poll_forecast(TickType_t *now, TickType_t *last, poll_stats_t *st)
{
    if (!poll_due(*now, *last, FORECAST_INTERVAL_S)) return;
    st->attempted++;
    esp_err_t err = nws_fetch_forecast(&s_location, &s_forecast);
    if (err == ESP_OK) {
        send_simple_event(DISPLAY_EVT_FORECAST_UPDATE);
    } else {
        st->failed++;
        ESP_LOGW(TAG, "forecast fetch failed: %s", esp_err_to_name(err));
    }
    *last = xTaskGetTickCount();
    poll_stagger(now);
}

// Adopt freshly-fetched alerts, emitting an event only when they changed.
/**
 * @brief Replace the cached alerts with @p new_alerts; on change, snapshot them
 *        and emit DISPLAY_EVT_ALERT_RECEIVED.
 * @version 1
 * @req REQ-ALERT-001
 */
static void process_alerts(const nws_alerts_t *new_alerts)
{
    bool changed = alerts_changed(new_alerts);
    memcpy(&s_alerts, new_alerts, sizeof(s_alerts));
    if (changed) {
        alerts_snapshot(&s_alerts);
        send_simple_event(DISPLAY_EVT_ALERT_RECEIVED);
        ESP_LOGI(TAG, "alert change: %d active", s_alerts.alert_count);
    }
}

/**
 * @brief If due (5 min), fetch alerts into a heap buffer (~17 KB), adopt them via
 *        process_alerts(), count the attempt/failure and stagger.
 * @version 1
 * @req REQ-ALERT-001
 * @req REQ-WX-004
 */
static void poll_alerts(float lat, float lon, TickType_t *now,
                        TickType_t *last, poll_stats_t *st)
{
    if (!poll_due(*now, *last, ALERTS_INTERVAL_S)) return;
    st->attempted++;
    // Heap-allocated: nws_alerts_t is ~17KB (too large for stack).
    nws_alerts_t *new_alerts = (nws_alerts_t *)malloc(sizeof(nws_alerts_t));
    if (!new_alerts) {
        st->failed++;
        ESP_LOGE(TAG, "alerts: malloc failed (%zu B)", sizeof(nws_alerts_t));
    } else {
        esp_err_t err = nws_fetch_alerts(lat, lon, new_alerts);
        if (err == ESP_OK) {
            process_alerts(new_alerts);
        } else {
            st->failed++;
            ESP_LOGW(TAG, "alerts fetch failed: %s", esp_err_to_name(err));
        }
        free(new_alerts);
    }
    *last = xTaskGetTickCount();
    poll_stagger(now);
}

#define RADAR_RASTERS_MAX   32         /* images listed per query (the server holds ~16) */
#define RADAR_GRID_S        (10 * 60)  /* loop frames at hh:00, hh:10, ...: each fixed once its
                                          mark is past, so 1-2 fetches per poll */

/**
 * @brief Fetch loop frame @p f unless the SD history (for the @p newest: the cache) already
 *        has it; save it to the history under its shown time (the newest overwrites its file,
 *        repairing one saved by an older build) and, for the newest, make it the cached image.
 * v2: takes a frame; stored and cached under its shown time (a grid mark for past frames).
 * @version 2
 * @utility
 * @return ESP_OK (fetched or nothing to do) or the fetch error. @p new_latest is set when the
 *         cached image changed.
 */
static esp_err_t fetch_frame(float lat, float lon, const nws_radar_frame_t *f, bool newest,
                             bool *new_latest)
{
    const nws_radar_raster_t *r = &f->raster;
    time_t ts = (time_t)f->show_s;
    bool on_sd = nws_radar_history_has(ts);
    bool needed = newest ? (s_radar_ts != ts || !s_radar_png) : !on_sd;
    if (!needed) return ESP_OK;
    uint8_t *png = NULL;
    size_t len = 0;
    esp_err_t err = nws_fetch_radar(lat, lon, r->id, &png, &len);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "radar frame %lld: %s", (long long)ts, esp_err_to_name(err));
        return err;
    }
    if (!on_sd || newest) nws_radar_history_save(png, len, ts);   // the newest overwrites
    if (!newest) {
        free(png);
        return ESP_OK;
    }
    free(s_radar_png);              // swap cached radar buffer
    s_radar_png     = png;
    s_radar_png_len = len;
    s_radar_ts      = ts;
    *new_latest = true;
    ESP_LOGI(TAG, "radar cached: %zu bytes, image %lld valid from %lld", len, (long long)r->id,
             (long long)ts);
    return ESP_OK;
}

/**
 * @brief If due (10 min): list the server's radar images, pick the loop's frames (the
 *        newest, then the image current at each 10-minute mark over the last 70 min), fetch the
 *        ones not yet on the SD history (all of them after boot), drop the rest from it,
 *        and emit DISPLAY_EVT_RADAR_READY when the newest image changed. Counts the
 *        attempt/failure and staggers.
 * v2: also writes each fetched image to the SD radar history (the radar loop); the fetch
 *     rate is unchanged.
 * v3: frames by their valid time, each fetched by image id (the default view lagged and
 *     returned the same image twice), and the past hour backfilled.
 * v4: past frames on the 10-minute grid (the image current at each mark), the newest at its
 *     own time.
 * @version 4
 * @req REQ-RADAR-001
 * @req REQ-RADAR-003
 * @req REQ-WX-004
 */
static void poll_radar(float lat, float lon, TickType_t *now,
                       TickType_t *last, poll_stats_t *st)
{
    if (!poll_due(*now, *last, RADAR_INTERVAL_S)) return;
    st->attempted++;
    nws_radar_raster_t all[RADAR_RASTERS_MAX];
    nws_radar_frame_t pick[NWS_RADAR_HISTORY_MAX];
    int n = nws_radar_rasters(lat, lon, all, RADAR_RASTERS_MAX);
    int k = n > 0 ? nws_radar_pick(all, n, (int64_t)time(NULL), RADAR_GRID_S,
                                   NWS_RADAR_HISTORY_MAX_AGE_S, pick, NWS_RADAR_HISTORY_MAX)
                  : 0;
    bool failed = (k == 0);
    bool new_latest = false;
    // Drop the frames the loop no longer uses first: saving prunes to the newest
    // NWS_RADAR_HISTORY_MAX, which would otherwise delete the backfilled older frames.
    time_t keep[NWS_RADAR_HISTORY_MAX];
    for (int i = 0; i < k; i++) keep[i] = (time_t)pick[i].show_s;
    if (k > 0) nws_radar_history_keep(keep, k);
    for (int i = k - 1; i >= 0; i--) {   // newest first: the screen gets it before the backfill
        if (fetch_frame(lat, lon, &pick[i], i == k - 1, &new_latest) != ESP_OK) failed = true;
    }
    if (new_latest) send_simple_event(DISPLAY_EVT_RADAR_READY);
    if (failed) {
        st->failed++;
        ESP_LOGW(TAG, "radar: %d of %d image(s) picked, some fetch failed", k, n);
    }
    *last = xTaskGetTickCount();
    poll_stagger(now);
}

/**
 * @brief If due (3 h), fetch the Kp index and cache it; emit DISPLAY_EVT_ASTRO_TRIGGER
 *        when Kp >= 6.0 (aurora possible). Last fetch of the cycle, so no stagger.
 * @version 1
 * @req REQ-ASTRO-001
 * @req REQ-WX-004
 */
static void poll_kp(TickType_t *now, TickType_t *last, poll_stats_t *st)
{
    if (!poll_due(*now, *last, KP_INTERVAL_S)) return;
    st->attempted++;
    float kp = 0.0f;
    esp_err_t err = nws_fetch_kp_index(&kp);
    if (err == ESP_OK) {
        s_kp_index = kp;
        if (kp >= AURORA_KP_THRESHOLD) {
            ESP_LOGW(TAG, "Aurora possible! Kp=%.1f (threshold=%.1f)",
                     kp, AURORA_KP_THRESHOLD);
            send_simple_event(DISPLAY_EVT_ASTRO_TRIGGER);
        }
    } else {
        st->failed++;
        ESP_LOGW(TAG, "Kp fetch failed: %s", esp_err_to_name(err));
    }
    *last = xTaskGetTickCount();
    // No stagger after the final fetch of the cycle.
}

// True (and logs/sleeps) if internal DRAM is too low to safely do HTTP/TLS work.
/**
 * @brief Skip a fetch cycle when free internal DRAM is below BACKOFF_HEAP_MIN
 *        (4000 B), sleeping BACKOFF_BASE_S (5 min) before returning.
 * @version 1
 * @req REQ-MEM-002
 * @return true if DRAM was too low and the cycle was skipped.
 */
static bool dram_too_low_backoff(void)
{
    size_t internal_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    if (internal_free >= BACKOFF_HEAP_MIN) return false;
    ESP_LOGW(TAG, "internal DRAM low (%zu B < %d) — skipping fetch cycle",
             internal_free, BACKOFF_HEAP_MIN);
    vTaskDelay(pdMS_TO_TICKS(BACKOFF_BASE_S * 1000));
    return true;
}

// Linear backoff when every attempted fetch failed; normal cadence otherwise.
/**
 * @brief End-of-cycle sleep: when every attempted fetch failed, back off linearly
 *        (5 min x failure streak, capped at 30 min); otherwise reset the streak
 *        and sleep 60 s.
 * @version 1
 * @req REQ-WX-004
 */
static void apply_backoff(const poll_stats_t *st, int *streak)
{
    if (st->attempted > 0 && st->failed == st->attempted) {
        (*streak)++;
        int backoff_s = BACKOFF_BASE_S * (*streak);
        if (backoff_s > BACKOFF_MAX_S) backoff_s = BACKOFF_MAX_S;
        ESP_LOGW(TAG, "all %d fetches failed (streak=%d) — backing off %ds",
                 st->attempted, *streak, backoff_s);
        vTaskDelay(pdMS_TO_TICKS(backoff_s * 1000));
        return;
    }
    if (*streak > 0) {
        ESP_LOGI(TAG, "fetch recovery after %d consecutive failures", *streak);
    }
    *streak = 0;
    vTaskDelay(pdMS_TO_TICKS(60 * 1000));  // normal sleep between poll cycles
}

/**
 * @brief NWS polling task entry: waits 30 s, seeds each source's first fetch into
 *        a separate ~60 s cycle, then loops forever polling conditions, forecast,
 *        alerts, radar and Kp, skipping cycles on low DRAM and backing off on
 *        total failure.
 * v2: the forecast's first fetch shares cycle 1 with conditions (sequential, right after
 *     it), so the forecast strip is populated for the first weather screen instead of
 *     showing "---" for ~3 min after boot. Alerts/radar/Kp keep their stagger.
 * v3: no lat/lon in the log (the device location is private).
 * @version 3
 * @callback
 * @req REQ-WX-004
 * @req REQ-WX-003
 */
static void nws_task_fn(void *arg)
{
    (void)arg;
    float lat = s_location.lat;
    float lon = s_location.lon;

    // Never log lat/lon: the device location is private (secrets.h).
    ESP_LOGI(TAG, "task started: office=%s station=%s", s_location.office, s_location.station);

    vTaskDelay(pdMS_TO_TICKS(INITIAL_DELAY_S * 1000));  // let WiFi + C6 SDIO settle

    // Spread the FIRST fetch of each source across separate ~60s poll cycles rather
    // than bursting all five at boot — the burst is what trips the C6 SDIO panic.
    // Order by weight/importance: conditions + forecast first (both feed the first
    // weather screen; fetched sequentially, not concurrently), then alerts, radar
    // (heaviest, deferred most), Kp last.
    TickType_t t0 = xTaskGetTickCount();
    poll_ticks_t last = {
        .conditions = seed_due_in(t0, CONDITIONS_INTERVAL_S,  0),   // ~cycle 1
        .forecast   = seed_due_in(t0, FORECAST_INTERVAL_S,    0),   // ~cycle 1, after conditions
        .alerts     = seed_due_in(t0, ALERTS_INTERVAL_S,     60),   // ~cycle 2
        .radar      = seed_due_in(t0, RADAR_INTERVAL_S,     180),   // ~cycle 4
        .kp         = seed_due_in(t0, KP_INTERVAL_S,        240),   // ~cycle 5
    };
    int streak = 0;

    while (true) {
        // Steady-state internal DRAM is ~15KB (TLS session reuse); only skip if
        // dangerously low (SDMMC/LVGL starvation risk).
        if (dram_too_low_backoff()) continue;

        TickType_t now = xTaskGetTickCount();
        poll_stats_t st = {0, 0};

        poll_conditions(&now, &last.conditions, &st);
        poll_forecast(&now, &last.forecast, &st);
        poll_alerts(lat, lon, &now, &last.alerts, &st);
        poll_radar(lat, lon, &now, &last.radar, &st);
        poll_kp(&now, &last.kp, &st);

        apply_backoff(&st, &streak);
    }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

/**
 * @brief Store the event callback, clear cached data, resolve the NWS location
 *        and start the polling task (12 KB stack, priority 3).
 * v2: no lat/lon in the log (the device location is private).
 * @version 2
 * @req REQ-WX-001
 * @req REQ-WX-004
 * @return ESP_OK when the task started; the nws_resolve_location() error if the
 *         location could not be resolved (weather disabled); ESP_ERR_NO_MEM if
 *         task creation failed.
 */
esp_err_t nws_init(float lat, float lon, nws_event_cb_t event_cb)
{
    ESP_LOGI(TAG, "init: cb=%s", event_cb ? "set" : "NULL");   // no lat/lon: location is private
    s_event_cb = event_cb;

    memset(&s_conditions, 0, sizeof(s_conditions));
    memset(&s_forecast, 0, sizeof(s_forecast));
    memset(&s_alerts, 0, sizeof(s_alerts));
    memset(&s_location, 0, sizeof(s_location));

    // Resolve location (may use NVS cache)
    esp_err_t err = nws_resolve_location(lat, lon, &s_location);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "location resolve failed: %s — weather disabled",
                 esp_err_to_name(err));
        return err;
    }

    // Start polling task
    BaseType_t ret = xTaskCreate(nws_task_fn, "nws_task", 12288, NULL, 3, NULL);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "failed to create nws_task");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "init complete — polling task started");
    return ESP_OK;
}
