/**
 * @file esp_timer.h
 * @version 1
 * @brief Host stand-in for esp_timer_get_time (monotonic microseconds).
 */
#pragma once
#include <stdint.h>
#include <time.h>

/**
 * @brief Microseconds since an arbitrary start (CLOCK_MONOTONIC).
 * @version 1
 * @utility
 * @return Microseconds.
 */
static inline int64_t esp_timer_get_time(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}
