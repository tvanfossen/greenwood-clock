// components/nws/src/nws_task.c
//
// FreeRTOS task: staggered polling of NWS endpoints, delivering events to
// the display FSM.  Single task, priority 3, 12 KB stack.

#include "nws_internal.h"

static const char *TAG = "nws_task";

// Event delivery callback — set by nws_init(), NULL-safe
static nws_event_cb_t s_event_cb;

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

// Aurora visibility threshold for latitude ~43°N
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

// Previous alert state for change detection
static int              s_prev_alert_count;
static char             s_prev_alert_event[64];

const nws_conditions_t *nws_get_conditions(void) { return &s_conditions; }
const nws_forecast_t   *nws_get_forecast(void)   { return &s_forecast; }
const nws_alerts_t     *nws_get_alerts(void)     { return &s_alerts; }
float                   nws_get_kp_index(void)   { return s_kp_index; }

const uint8_t *nws_get_radar_png(size_t *len_out)
{
    if (len_out) *len_out = s_radar_png_len;
    return s_radar_png;
}

// Precipitation detection via PNG size heuristic.
// An all-transparent 1024×600 PNG32 compresses to ~5-15KB.
// Meaningful reflectivity data pushes well past 20KB.
#define RADAR_PRECIP_SIZE_THRESHOLD 20000

bool nws_radar_has_precipitation(void)
{
    return (s_radar_png != NULL && s_radar_png_len > RADAR_PRECIP_SIZE_THRESHOLD);
}

// ---------------------------------------------------------------------------
// Alert change detection
// ---------------------------------------------------------------------------

static bool alerts_changed(const nws_alerts_t *new_alerts)
{
    if (new_alerts->alert_count != s_prev_alert_count) return true;
    if (new_alerts->alert_count > 0 &&
        strcmp(new_alerts->alerts[0].event, s_prev_alert_event) != 0) return true;
    return false;
}

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
static bool poll_due(TickType_t now, TickType_t last, uint32_t interval_s)
{
    return last == 0 || (now - last) >= pdMS_TO_TICKS(interval_s * 1000);
}

// Fabricate a "last fetch" tick so a source first becomes due `due_in_s` seconds
// after `t0`. Used to spread each source's first fetch across separate poll cycles
// at boot (unsigned tick wraparound makes now-last resolve to the true elapsed).
static TickType_t seed_due_in(TickType_t t0, uint32_t interval_s, uint32_t due_in_s)
{
    TickType_t last = t0 - pdMS_TO_TICKS((interval_s - due_in_s) * 1000);
    return last == 0 ? 1 : last;   // 0 means "never fetched" (immediately due)
}

static void send_simple_event(display_event_type_t type)
{
    display_event_t evt = {};
    evt.type = type;
    send_event(&evt);
}

// Stagger between fetches in a cycle, then refresh `now`.
static void poll_stagger(TickType_t *now)
{
    vTaskDelay(pdMS_TO_TICKS(STAGGER_S * 1000));
    *now = xTaskGetTickCount();
}

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

static void poll_radar(float lat, float lon, TickType_t *now,
                       TickType_t *last, poll_stats_t *st)
{
    if (!poll_due(*now, *last, RADAR_INTERVAL_S)) return;
    st->attempted++;
    uint8_t *png_buf = NULL;
    size_t   png_len = 0;
    esp_err_t err = nws_fetch_radar(lat, lon, &png_buf, &png_len);
    if (err == ESP_OK && png_buf) {
        free(s_radar_png);              // swap cached radar buffer
        s_radar_png     = png_buf;
        s_radar_png_len = png_len;
        ESP_LOGI(TAG, "radar cached: %zu bytes", png_len);
        send_simple_event(DISPLAY_EVT_RADAR_READY);
    } else {
        st->failed++;
        ESP_LOGW(TAG, "radar fetch failed: %s", esp_err_to_name(err));
    }
    *last = xTaskGetTickCount();
    poll_stagger(now);
}

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
static bool dram_too_low_backoff(void)
{
    size_t internal_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    if (internal_free >= BACKOFF_HEAP_MIN) return false;
    ESP_LOGW(TAG, "internal DRAM low (%zu B < %d) — skipping fetch cycle",
             internal_free, BACKOFF_HEAP_MIN);
    vTaskDelay(pdMS_TO_TICKS(BACKOFF_BASE_S * 1000));
    return true;
}

// Exponential backoff when every attempted fetch failed; normal cadence otherwise.
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

static void nws_task_fn(void *arg)
{
    (void)arg;
    float lat = s_location.lat;
    float lon = s_location.lon;

    ESP_LOGI(TAG, "task started: lat=%.4f lon=%.4f office=%s station=%s",
             lat, lon, s_location.office, s_location.station);

    vTaskDelay(pdMS_TO_TICKS(INITIAL_DELAY_S * 1000));  // let WiFi + C6 SDIO settle

    // Spread the FIRST fetch of each source across separate ~60s poll cycles rather
    // than bursting all five at boot — the burst is what trips the C6 SDIO panic.
    // Order by weight/importance: conditions first, then alerts, forecast, radar
    // (heaviest, deferred most), Kp last.
    TickType_t t0 = xTaskGetTickCount();
    poll_ticks_t last = {
        .conditions = seed_due_in(t0, CONDITIONS_INTERVAL_S,  0),   // ~cycle 1
        .alerts     = seed_due_in(t0, ALERTS_INTERVAL_S,     60),   // ~cycle 2
        .forecast   = seed_due_in(t0, FORECAST_INTERVAL_S,  120),   // ~cycle 3
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

esp_err_t nws_init(float lat, float lon, nws_event_cb_t event_cb)
{
    ESP_LOGI(TAG, "init: lat=%.4f lon=%.4f cb=%s", lat, lon,
             event_cb ? "set" : "NULL");
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
