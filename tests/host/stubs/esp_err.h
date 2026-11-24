/**
 * @file esp_err.h
 * @version 1
 * @brief Host-test stand-in for ESP-IDF's esp_err_t (only for tests/host).
 */
#ifndef HOST_STUB_ESP_ERR_H
#define HOST_STUB_ESP_ERR_H

typedef int esp_err_t;

#define ESP_OK                   0
#define ESP_FAIL                 -1
#define ESP_ERR_NO_MEM           0x101
#define ESP_ERR_INVALID_RESPONSE 0x108
#define ESP_ERR_HTTP_CONNECT     0x7002

#endif /* HOST_STUB_ESP_ERR_H */
