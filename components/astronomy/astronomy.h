
#pragma once
#include <time.h>
#include "esp_err.h"

// Returns moon phase info (phase name, illumination, icon URL, etc) for given date/location
// Returns 0 on success, nonzero on error
int astronomy_fetch_moon_phase(double lat, double lon, const struct tm* date, char* phase_name, size_t phase_name_sz, float* illumination, char* icon_url, size_t icon_url_sz);

// Download moon phase image from URL
esp_err_t astronomy_fetch_icon(const char* image_url, uint8_t **out_buf, size_t *out_len);
