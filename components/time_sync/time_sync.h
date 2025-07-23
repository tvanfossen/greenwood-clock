// components/time_sync/time_sync.h
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Set TZ string and sync via SNTP. */
void time_sync_setup(const char* tz_env);

/** Return a filled local struct tm. */
void time_sync_get_local(time_t* out_secs, struct tm* out_tm);

#ifdef __cplusplus
}
#endif
