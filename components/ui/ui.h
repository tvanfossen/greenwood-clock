// components/ui/ui.h

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <time.h>
#include "weather.h"   // for weather_data_t

void ui_show_splash(void);
void ui_clock_init(const struct tm *timeinfo);

#ifdef __cplusplus
}
#endif
