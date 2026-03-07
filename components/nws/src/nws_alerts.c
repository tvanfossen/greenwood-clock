// components/nws/src/nws_alerts.c
//
// Fetch active weather alerts from NWS alerts endpoint.

#include "nws_internal.h"

static const char *TAG = "nws_alert";

#define NWS_ALERTS_URL_FMT  "https://api.weather.gov/alerts/active?point=%.4f,%.4f"

static void parse_alert(const cJSON *feature, nws_alert_t *out)
{
    const cJSON *props = cJSON_GetObjectItemCaseSensitive(feature, "properties");
    if (!props) return;

    json_get_str(props, "event", out->event, sizeof(out->event));
    json_get_str(props, "severity", out->severity, sizeof(out->severity));
    json_get_str(props, "urgency", out->urgency, sizeof(out->urgency));
    json_get_str(props, "certainty", out->certainty, sizeof(out->certainty));
    json_get_str(props, "headline", out->headline, sizeof(out->headline));
    json_get_str(props, "description", out->description, sizeof(out->description));
    json_get_str(props, "instruction", out->instruction, sizeof(out->instruction));
    json_get_str(props, "areaDesc", out->area_desc, sizeof(out->area_desc));

    // onset and expires are ISO 8601 strings — store as epoch if parseable,
    // otherwise leave as 0. Full ISO 8601 parsing is complex; for now just
    // store the raw strings would overflow our int64_t fields.
    // TODO: parse ISO 8601 → epoch
    out->onset = 0;
    out->expires = 0;
}

static esp_err_t parse_alerts(const char *json_str, nws_alerts_t *out)
{
    cJSON *root = cJSON_Parse(json_str);
    if (!root) {
        ESP_LOGE(TAG, "JSON parse failed");
        return ESP_ERR_INVALID_RESPONSE;
    }

    const cJSON *features = cJSON_GetObjectItemCaseSensitive(root, "features");
    if (!cJSON_IsArray(features)) {
        // No features array = no alerts (valid response)
        out->alert_count = 0;
        out->valid = true;
        cJSON_Delete(root);
        return ESP_OK;
    }

    int count = cJSON_GetArraySize(features);
    if (count > 8) count = 8;
    out->alert_count = count;

    for (int i = 0; i < count; i++) {
        const cJSON *f = cJSON_GetArrayItem(features, i);
        parse_alert(f, &out->alerts[i]);
    }

    out->valid = true;
    cJSON_Delete(root);

    if (count > 0) {
        ESP_LOGW(TAG, "alerts: %d active — first: %s (%s)",
                 count, out->alerts[0].event, out->alerts[0].severity);
    } else {
        ESP_LOGI(TAG, "alerts: none active");
    }
    return ESP_OK;
}

esp_err_t nws_fetch_alerts(float lat, float lon, nws_alerts_t *out)
{
    memset(out, 0, sizeof(*out));

    char url[128];
    snprintf(url, sizeof(url), NWS_ALERTS_URL_FMT, lat, lon);

    char *json = nws_http_fetch_json(TAG, url);
    if (!json) return ESP_ERR_HTTP_CONNECT;

    esp_err_t err = parse_alerts(json, out);
    free(json);
    return err;
}
