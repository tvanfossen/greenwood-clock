/**
 * @file http_api.h
 * @version 1
 * @brief Public API of the local REST server on port 80.
 */
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
 * @version 1
 * @req REQ-API-001
 * @return ESP_OK on success
 */
esp_err_t http_api_start(void);

/**
 * @brief Stop HTTP API server
 * @version 1
 * @req REQ-API-001
 * @return ESP_OK on success
 */
esp_err_t http_api_stop(void);

/**
 * @brief Check if HTTP API server is running
 * @version 1
 * @req REQ-API-001
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
 * @version 1
 * @req REQ-API-001
 */
void http_api_set_launch_sem(SemaphoreHandle_t sem);

/**
 * @brief The launch window is over (main calls this after boot_await_launch): handlers that
 *        process outside data (an address lookup) may now run.
 * @version 1
 * @req REQ-LOC-001
 */
void http_api_set_launched(void);

/**
 * @brief Whether the launch window is over.
 * @version 1
 * @req REQ-LOC-001
 * @return Whether.
 */
bool http_api_launched(void);

#ifdef __cplusplus
}
#endif

#endif // HTTP_API_H
