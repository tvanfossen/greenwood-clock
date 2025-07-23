// components/network/network.h
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Initialize Wi‑Fi STA + SNTP. */
esp_err_t network_init(const char* ssid, const char* pass);

/** Block until SNTP has set the RTC. */
void network_wait_for_time(void);

#ifdef __cplusplus
}
#endif
