/**
 * @file log_async.h
 * @version 1
 * @brief Non-blocking ESP log output: callers enqueue formatted lines, a writer task emits them.
 */
#pragma once

#include "esp_err.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Put a ring buffer + low-priority writer task in front of the current ESP log hook chain.
 *
 * Every ESP_LOGx call then costs a formatted copy into the ring buffer instead of a
 * synchronous UART (115200 baud, ~8 ms per line) + SD write in the calling task, which
 * held the LVGL lock through every logged display transition. Lines are never truncated;
 * when the buffer is full a line is dropped and counted. Call once, after the hooks it
 * should forward to (debug_log) are installed. Idempotent.
 *
 * @return ESP_OK, or ESP_ERR_NO_MEM if the buffer/task could not be created (logging
 *         then stays synchronous, unchanged).
 */
esp_err_t log_async_init(void);

/**
 * @brief Number of log lines dropped because the ring buffer was full (since boot).
 * @return Dropped-line count.
 */
uint32_t log_async_dropped(void);

#ifdef __cplusplus
}
#endif
