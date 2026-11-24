/**
 * @file esp_log.h
 * @version 1
 * @brief Host stand-in for ESP-IDF logging (tools/lvgl_host): messages go to stderr.
 */
#pragma once
#include <stdio.h>

#define ESP_LOGE(tag, fmt, ...) fprintf(stderr, "E %s: " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGW(tag, fmt, ...) fprintf(stderr, "W %s: " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGI(tag, fmt, ...) fprintf(stderr, "I %s: " fmt "\n", tag, ##__VA_ARGS__)
#define ESP_LOGD(tag, fmt, ...) ((void)(tag))
#define esp_err_to_name(e) ((e) == 0 ? "ESP_OK" : "ESP_ERR")
