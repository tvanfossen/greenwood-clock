// components/http_api/http_api.h

#ifndef HTTP_API_H
#define HTTP_API_H

#include "esp_err.h"
#include "esp_http_server.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Start HTTP API server for file management and logs
 * @return ESP_OK on success
 */
esp_err_t http_api_start(void);

/**
 * @brief Stop HTTP API server
 * @return ESP_OK on success
 */
esp_err_t http_api_stop(void);

/**
 * @brief Check if HTTP API server is running
 * @return true if running
 */
bool http_api_is_running(void);

/**
 * @brief Register the semaphore that boot_await_launch() blocks on.
 *
 * Must be called before the 60-second window opens. The debug/launch HTTP
 * handler gives this semaphore to unblock app_main immediately.
 *
 * @param sem Binary semaphore created by boot_await_launch().
 */
void http_api_set_launch_sem(SemaphoreHandle_t sem);

#ifdef __cplusplus
}
#endif

#endif // HTTP_API_H
