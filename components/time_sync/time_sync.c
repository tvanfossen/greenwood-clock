/**
 * @file time_sync.c
 * @version 1
 * @brief POSIX TZ timezone application and local-time lookup.
 */
// components/time_sync/time_sync.c
#include "time_sync.h"
#include "esp_log.h"
#include <stdlib.h>
#include <time.h>

static const char* TAG = "time_sync";

/**
 * @brief Set the POSIX TZ environment string and apply it with tzset().
 * @version 1
 * @req REQ-TIME-002
 */
void time_sync_setup(const char* tz_env) {
    setenv("TZ", tz_env, 1);
    tzset();
}

/**
 * @brief Get the current time as epoch seconds and as a local struct tm (per the TZ set by time_sync_setup).
 * @version 1
 * @req REQ-TIME-002
 */
void time_sync_get_local(time_t* out_secs, struct tm* out_tm) {
    time_t now = time(NULL);
    localtime_r(&now, out_tm);
    if (out_secs) *out_secs = now;
}
