// components/http_api/http_api.h

#ifndef HTTP_API_H
#define HTTP_API_H

#include "esp_err.h"
#include "esp_http_server.h"

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

#ifdef __cplusplus
}
#endif

#endif // HTTP_API_H
