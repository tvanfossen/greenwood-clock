// components/nws/src/nws_task.c
//
// FreeRTOS task: staggered polling of NWS endpoints, delivering events to
// the display FSM.  Single task, priority 3, 8 KB stack.

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

// Stagger initial fetches so they don't all fire at once
#define INITIAL_DELAY_S         5
#define STAGGER_S               10

// Aurora visibility threshold for latitude ~43°N
#define AURORA_KP_THRESHOLD     6.0f

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

static void nws_task_fn(void *arg)
{
    (void)arg;
    float lat = s_location.lat;
    float lon = s_location.lon;

    ESP_LOGI(TAG, "task started: lat=%.4f lon=%.4f office=%s station=%s",
             lat, lon, s_location.office, s_location.station);

    // Stagger initial fetches
    vTaskDelay(pdMS_TO_TICKS(INITIAL_DELAY_S * 1000));

    TickType_t last_conditions = 0;
    TickType_t last_forecast   = 0;
    TickType_t last_alerts     = 0;
    TickType_t last_radar      = 0;
    TickType_t last_kp         = 0;

    while (true) {
        TickType_t now = xTaskGetTickCount();
        display_event_t evt = {};

        // --- Conditions ---
        if (last_conditions == 0 ||
            (now - last_conditions) >= pdMS_TO_TICKS(CONDITIONS_INTERVAL_S * 1000)) {
            esp_err_t err = nws_fetch_conditions(&s_location, &s_conditions);
            if (err == ESP_OK) {
                evt.type = DISPLAY_EVT_WEATHER_UPDATE;
                send_event(&evt);
            } else {
                ESP_LOGW(TAG, "conditions fetch failed: %s", esp_err_to_name(err));
            }
            last_conditions = xTaskGetTickCount();

            // Stagger next fetch
            vTaskDelay(pdMS_TO_TICKS(STAGGER_S * 1000));
            now = xTaskGetTickCount();
        }

        // --- Forecast ---
        if (last_forecast == 0 ||
            (now - last_forecast) >= pdMS_TO_TICKS(FORECAST_INTERVAL_S * 1000)) {
            esp_err_t err = nws_fetch_forecast(&s_location, &s_forecast);
            if (err == ESP_OK) {
                evt.type = DISPLAY_EVT_FORECAST_UPDATE;
                send_event(&evt);
            } else {
                ESP_LOGW(TAG, "forecast fetch failed: %s", esp_err_to_name(err));
            }
            last_forecast = xTaskGetTickCount();

            vTaskDelay(pdMS_TO_TICKS(STAGGER_S * 1000));
            now = xTaskGetTickCount();
        }

        // --- Alerts ---
        if (last_alerts == 0 ||
            (now - last_alerts) >= pdMS_TO_TICKS(ALERTS_INTERVAL_S * 1000)) {
            nws_alerts_t new_alerts;
            esp_err_t err = nws_fetch_alerts(lat, lon, &new_alerts);
            if (err == ESP_OK) {
                if (alerts_changed(&new_alerts)) {
                    memcpy(&s_alerts, &new_alerts, sizeof(s_alerts));
                    alerts_snapshot(&s_alerts);
                    evt.type = DISPLAY_EVT_ALERT_RECEIVED;
                    send_event(&evt);
                    ESP_LOGI(TAG, "alert change: %d active", s_alerts.alert_count);
                } else {
                    // No change — update cached data but don't send event
                    memcpy(&s_alerts, &new_alerts, sizeof(s_alerts));
                }
            } else {
                ESP_LOGW(TAG, "alerts fetch failed: %s", esp_err_to_name(err));
            }
            last_alerts = xTaskGetTickCount();

            vTaskDelay(pdMS_TO_TICKS(STAGGER_S * 1000));
            now = xTaskGetTickCount();
        }

        // --- Radar ---
        if (last_radar == 0 ||
            (now - last_radar) >= pdMS_TO_TICKS(RADAR_INTERVAL_S * 1000)) {
            uint8_t *png_buf = NULL;
            size_t   png_len = 0;
            esp_err_t err = nws_fetch_radar(lat, lon, &png_buf, &png_len);
            if (err == ESP_OK && png_buf) {
                // Swap cached radar buffer — free old, cache new
                free(s_radar_png);
                s_radar_png     = png_buf;
                s_radar_png_len = png_len;
                ESP_LOGI(TAG, "radar cached: %zu bytes", png_len);
                evt.type = DISPLAY_EVT_RADAR_READY;
                send_event(&evt);
            } else {
                ESP_LOGW(TAG, "radar fetch failed: %s", esp_err_to_name(err));
            }
            last_radar = xTaskGetTickCount();

            vTaskDelay(pdMS_TO_TICKS(STAGGER_S * 1000));
            now = xTaskGetTickCount();
        }

        // --- Kp index (aurora) ---
        if (last_kp == 0 ||
            (now - last_kp) >= pdMS_TO_TICKS(KP_INTERVAL_S * 1000)) {
            float kp = 0.0f;
            esp_err_t err = nws_fetch_kp_index(&kp);
            if (err == ESP_OK) {
                s_kp_index = kp;
                if (kp >= AURORA_KP_THRESHOLD) {
                    ESP_LOGW(TAG, "Aurora possible! Kp=%.1f (threshold=%.1f)",
                             kp, AURORA_KP_THRESHOLD);
                    // Trigger astronomy display for aurora visibility
                    evt.type = DISPLAY_EVT_ASTRO_TRIGGER;
                    send_event(&evt);
                }
            } else {
                ESP_LOGW(TAG, "Kp fetch failed: %s", esp_err_to_name(err));
            }
            last_kp = xTaskGetTickCount();
        }

        // Sleep 60s between poll cycles
        vTaskDelay(pdMS_TO_TICKS(60 * 1000));
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
    BaseType_t ret = xTaskCreate(nws_task_fn, "nws_task", 8192, NULL, 3, NULL);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "failed to create nws_task");
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "init complete — polling task started");
    return ESP_OK;
}
