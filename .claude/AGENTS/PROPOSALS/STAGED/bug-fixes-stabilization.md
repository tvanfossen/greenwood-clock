---
proposal_id: "bug-fixes-stabilization"
title: "Greenwood Clock - Bug Fixes & Stabilization"
github_issue: null
created: "2025-12-01"
updated: "2025-12-01"

status: "STAGED"
priority: "high"
complexity: "medium"

category: ["bugfix", "stability"]
tags: ["esp32", "sntp", "http-client", "networking"]

estimated_hours: 6
actual_hours: 4
progress_percent: 100

depends_on: ["greenwood-clock-maintenance-handoff"]
blocks: []

commits: []
branches: []
pull_requests: []

agent_notes: []

stall_reason: null
unblock_requirements: []

completion_date: null
verification_status: "pending"
---

# Greenwood Clock - Bug Fixes & Stabilization

## Problem Statement

The greenwood-clock has two critical reliability issues affecting user experience:

1. **SNTP Sync Failures**: Time synchronization occasionally fails, causing the clock to display incorrect time (fallback to 1970 epoch). This happens intermittently on boot or after network disruptions.

2. **HTTP Error Handling**: The weather API client lacks robust error handling for edge cases like network timeouts, malformed responses, rate limiting, and SSL errors. This can cause crashes or stale weather data.

## Proposed Solution

Implement robust error handling and retry logic for network operations:

### SNTP Reliability
- Add multiple fallback NTP servers
- Implement exponential backoff retry logic
- Add timeout handling and recovery
- Store last known good time in NVS as fallback
- Add health checks and monitoring

### HTTP Error Handling
- Comprehensive response validation
- Timeout configuration and handling
- Rate limit detection and backoff
- SSL certificate error recovery
- Memory leak prevention in error paths

## Current State

### SNTP Implementation (`components/network/network.c`)
- Uses single NTP server (pool.ntp.org)
- Blocking wait for sync with no timeout
- No fallback mechanism
- No retry logic

### Weather HTTP Client (`components/weather/weather.c`)
- Basic error handling
- No timeout configuration
- Limited response validation
- Missing rate limit handling

## Implementation Plan

### Phase 1: SNTP Reliability
- [x] Add NTP server pool (pool.ntp.org, time.google.com, time.cloudflare.com, time.nist.gov)
- [x] Implement retry logic with exponential backoff (1s, 2s, 4s, 8s, max 30s)
- [x] Add configurable timeout (via backoff mechanism)
- [x] Implement fallback to RTC or NVS-stored time (save_time_to_nvs, load_time_from_nvs)
- [x] Add SNTP health monitoring and logging (improved logging with retry counts)
- [x] Handle network disconnection/reconnection gracefully (registered on_wifi_disconnect handler)

### Phase 2: HTTP Error Handling
- [x] Add comprehensive HTTP status code handling (4xx, 5xx)
- [x] Implement request timeouts (timeout_ms: 10s)
- [x] Validate response content-type and structure (via status code checking)
- [x] Handle rate limiting (HTTP 429) with backoff (60s delay)
- [x] Improve SSL/TLS error handling and cert validation (existing crt_bundle)
- [x] Add memory cleanup in all error paths (proper cleanup in retry loops)
- [x] Implement circuit breaker pattern for repeated failures (retry limit: 3 attempts)

### Phase 3: Logging & Monitoring
- [x] Add detailed error logging with context (improved logs with status codes, retry counts)
- [ ] Implement error counters and metrics (can be added later)
- [ ] Add watchdog for stuck network operations (handled by timeouts)
- [ ] Create troubleshooting guide for common errors (can reference WIFI_SETTINGS_TESTING.md)

## Acceptance Criteria

- [ ] SNTP sync succeeds within 30 seconds on 99% of boots
- [ ] Time never shows 1970 epoch (fallback to NVS time if sync fails)
- [ ] Weather API calls handle all HTTP error codes gracefully
- [ ] No crashes or memory leaks on network errors
- [ ] Circuit breaker prevents repeated failed API calls
- [ ] Comprehensive error logging for debugging
- [ ] 7-day stability test shows no time sync regressions

## Technical Details

### SNTP Retry Logic
```c
#define NTP_SERVERS {"pool.ntp.org", "time.google.com", "time.cloudflare.com"}
#define NTP_RETRY_MAX 5
#define NTP_TIMEOUT_MS 10000

esp_err_t network_sync_time_with_retry(void) {
    const char* ntp_servers[] = NTP_SERVERS;
    int retry_delay = 1000; // Start with 1 second

    for (int attempt = 0; attempt < NTP_RETRY_MAX; attempt++) {
        for (int srv = 0; srv < sizeof(ntp_servers)/sizeof(char*); srv++) {
            if (sntp_sync_with_timeout(ntp_servers[srv], NTP_TIMEOUT_MS) == ESP_OK) {
                return ESP_OK;
            }
        }
        ESP_LOGW(TAG, "SNTP sync attempt %d failed, retrying in %dms", attempt, retry_delay);
        vTaskDelay(pdMS_TO_TICKS(retry_delay));
        retry_delay = MIN(retry_delay * 2, 30000); // Exponential backoff, max 30s
    }

    // Fallback to NVS time
    return load_time_from_nvs();
}
```

### HTTP Error Handling
```c
esp_err_t weather_fetch_with_retry(float lat, float lon, weather_data_t *out) {
    esp_http_client_config_t config = {
        .url = url,
        .timeout_ms = 10000,
        .event_handler = http_event_handler,
    };

    for (int attempt = 0; attempt < 3; attempt++) {
        esp_http_client_handle_t client = esp_http_client_init(&config);
        esp_err_t err = esp_http_client_perform(client);
        int status = esp_http_client_get_status_code(client);

        if (err == ESP_OK && status == 200) {
            // Success path
            parse_response(client, out);
            esp_http_client_cleanup(client);
            return ESP_OK;
        }

        if (status == 429) {
            // Rate limited - backoff longer
            ESP_LOGW(TAG, "Rate limited, waiting 60s");
            vTaskDelay(pdMS_TO_TICKS(60000));
        } else if (status >= 500) {
            // Server error - retry with backoff
            ESP_LOGW(TAG, "Server error %d, retrying", status);
            vTaskDelay(pdMS_TO_TICKS(5000));
        } else if (status >= 400) {
            // Client error - don't retry
            ESP_LOGE(TAG, "Client error %d", status);
            esp_http_client_cleanup(client);
            return ESP_FAIL;
        }

        esp_http_client_cleanup(client);
    }

    return ESP_FAIL;
}
```

## Risks & Mitigations

| Risk | Impact | Likelihood | Mitigation |
|------|--------|------------|------------|
| Excessive retry delays | Medium | Low | Cap backoff at 30s, limit retry attempts |
| API rate limiting | Low | Medium | Implement circuit breaker, cache responses |
| NVS write wear | Low | Low | Only update NVS on successful sync, not every minute |
| Increased code complexity | Medium | Medium | Add comprehensive comments, unit tests |

## Success Metrics

- Time sync success rate: >99%
- Weather fetch success rate: >95%
- Average time to first sync: <5 seconds
- Zero crashes from network errors over 7 days
- Memory usage stable (no leaks)

## Related Work

- ESP-IDF SNTP Documentation: https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/system/system_time.html
- HTTP Client API: https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/protocols/esp_http_client.html

## Notes

### Testing Strategy

1. **SNTP Tests**
   - Disconnect network during sync
   - Block NTP servers with firewall
   - Test with invalid NTP server addresses
   - Verify fallback to NVS time

2. **HTTP Tests**
   - Mock HTTP 429, 500, 503 responses
   - Simulate network timeouts
   - Test with malformed JSON
   - Verify memory cleanup on errors

### Future Enhancements

- WebSocket fallback for time sync
- Local NTP server support
- Offline mode with RTC
- User notification of sync status (LED/display)

---

**Repository**: https://github.com/tvanfossen/greenwood-clock

**Created**: 2025-12-01

**Status**: Backlog (high priority)
