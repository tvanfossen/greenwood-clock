/**
 * @file library_backend.h
 * @version 1
 * @brief The clock's Library calls for its settings backend (library_store on the SD card,
 *        each SD access with the LVGL lock released).
 */
#pragma once

#include "settings_backend.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Fill @p be's library_* and theme_load with the clock's.
 * @version 1
 * @req REQ-LIB-001
 */
void library_backend_fill(settings_backend_t *be);

#ifdef __cplusplus
}
#endif
