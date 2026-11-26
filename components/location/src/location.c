/**
 * @file location.c
 * @version 1
 * @brief The clock's place and timezone (see location.h).
 */
#include "location.h"
#include "tz.h"
#include "settings.h"
#include "time_sync.h"
#include "http_client_utils.h"

#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "nvs.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "location";

#define LOC_NAMESPACE "location"
#define LOC_LABEL_KEY "label"
#define LOC_TZ_KEY    "tz"
#define URL_MAX       512

/**
 * @brief Read string @p key into @p out ("" when missing).
 * @version 1
 * @utility
 */
static void nvs_str(nvs_handle_t h, const char *key, char *out, size_t n)
{
    size_t len = n;
    if (nvs_get_str(h, key, out, &len) != ESP_OK) out[0] = '\0';
}

/**
 * @brief The clock's place and zone now.
 * @version 1
 * @req REQ-LOC-001
 */
void location_get(location_t *out)
{
    memset(out, 0, sizeof(*out));
    clock_settings_t cfg;
    settings_load(&cfg);
    out->place.lat = cfg.latitude;
    out->place.lon = cfg.longitude;
    nvs_handle_t h;
    if (nvs_open(LOC_NAMESPACE, NVS_READONLY, &h) == ESP_OK) {
        nvs_str(h, LOC_LABEL_KEY, out->place.label, sizeof(out->place.label));
        nvs_str(h, LOC_TZ_KEY, out->tz, sizeof(out->tz));
        nvs_close(h);
    }
}

/**
 * @brief Look an address up with the Census geocoder.
 * @version 1
 * @req REQ-LOC-001
 * @return ESP_OK, ESP_ERR_NOT_FOUND, or the request's failure.
 */
esp_err_t location_lookup(const char *address, location_place_t *out, char *why, size_t n)
{
    char *url = malloc(URL_MAX);
    esp_err_t err = url && location_census_url(address, url, URL_MAX) ? ESP_OK : ESP_ERR_INVALID_ARG;
    http_attempt_result_t res = HTTP_ATTEMPT_ABORT;
    size_t len = 0;
    char *body = NULL;
    if (err == ESP_OK) {
        esp_http_client_config_t cfg = {
            .url = url,   // never logged: it carries the address
            .transport_type = HTTP_TRANSPORT_OVER_SSL,
            .crt_bundle_attach = esp_crt_bundle_attach,
            .timeout_ms = 15000,
            .user_agent = "greenwood-clock",
        };
        body = http_request_execute(&cfg, &res, &len);
        err = body ? ESP_OK : ESP_FAIL;
    }
    if (err == ESP_OK && !location_census_parse(body, out)) err = ESP_ERR_NOT_FOUND;
    free(body);
    free(url);
    const char *reason = err == ESP_OK ? "" : err == ESP_ERR_NOT_FOUND ? "No match for that address"
                         : err == ESP_ERR_INVALID_ARG ? "Enter a street address"
                                                      : "The address lookup did not answer";
    if (why) snprintf(why, n, "%s", reason);
    ESP_LOGI(TAG, "lookup: %s", err == ESP_OK ? "matched" : reason);
    return err;
}

/**
 * @brief settings_update mutator: the coordinates and POSIX rule in @p ctx (a location_t; no
 *        zone keeps the rule the clock has), and the weather's grid forgotten.
 * @version 1
 * @utility
 * @return true (always save).
 */
static bool place_mutator(clock_settings_t *cfg, void *ctx)
{
    const location_t *loc = (const location_t *)ctx;
    cfg->latitude = loc->place.lat;
    cfg->longitude = loc->place.lon;
    const char *rule = tz_posix(loc->tz);
    if (rule) snprintf(cfg->timezone, sizeof(cfg->timezone), "%s", rule);   // none: keep the rule
    cfg->nws_office[0] = '\0';   // re-resolved for the new place on the next weather poll
    cfg->nws_station[0] = '\0';
    return true;
}

/**
 * @brief Keep the label and zone name.
 * @version 1
 * @utility
 * @return ESP_OK, or the NVS error.
 */
static esp_err_t save_names(const location_t *loc)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(LOC_NAMESPACE, NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_set_str(h, LOC_LABEL_KEY, loc->place.label);
        if (err == ESP_OK) err = nvs_set_str(h, LOC_TZ_KEY, loc->tz);
        if (err == ESP_OK) err = nvs_commit(h);
        nvs_close(h);
    }
    return err;
}

/**
 * @brief Keep a place and zone, and apply the zone.
 * @version 1
 * @req REQ-LOC-001
 * @return ESP_OK, ESP_ERR_INVALID_ARG, or the storage error.
 */
esp_err_t location_save(const location_t *loc)
{
    const char *rule = tz_posix(loc->tz);
    bool zone_ok = !loc->tz[0] || (rule && strlen(rule) < sizeof(((clock_settings_t *)0)->timezone));
    bool ok = zone_ok &&
              loc->place.lat >= -90.0f && loc->place.lat <= 90.0f &&
              loc->place.lon >= -180.0f && loc->place.lon <= 180.0f;
    esp_err_t err = ok ? settings_update(place_mutator, (void *)loc) : ESP_ERR_INVALID_ARG;
    if (err == ESP_OK) err = save_names(loc);
    if (err == ESP_OK && rule) time_sync_setup(rule);
    if (err == ESP_OK) ESP_LOGI(TAG, "saved (zone %s)", loc->tz[0] ? loc->tz : "unchanged");
    else ESP_LOGE(TAG, "save failed: %s", esp_err_to_name(err));
    return err;
}
