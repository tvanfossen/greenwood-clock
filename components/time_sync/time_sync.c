// components/time_sync/time_sync.c
#include "time_sync.h"
#include "esp_log.h"
#include <stdlib.h>
#include <time.h>

static const char* TAG = "time_sync";

void time_sync_setup(const char* tz_env) {
    setenv("TZ", tz_env, 1);
    tzset();
}

void time_sync_get_local(time_t* out_secs, struct tm* out_tm) {
    time_t now = time(NULL);
    localtime_r(&now, out_tm);
    if (out_secs) *out_secs = now;
}
