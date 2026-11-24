/**
 * @file esp_lvgl_port.h
 * @version 1
 * @brief Host stand-in for esp_lvgl_port: one thread, so the lock is a no-op.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define lvgl_port_lock(timeout) ((void)(timeout), true)
#define lvgl_port_unlock() ((void)0)
