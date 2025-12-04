// components/ui/ui.h

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <time.h>
#include "weather.h"   // for weather_data_t
#include "settings.h"  // for clock_settings_t

void ui_show_splash(void);
void ui_clock_init(const struct tm *timeinfo, const clock_settings_t *settings);

#ifdef __cplusplus
}
#endif
