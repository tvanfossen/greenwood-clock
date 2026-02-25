// components/udp_log/udp_log.h

#ifndef UDP_LOG_H
#define UDP_LOG_H

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Start streaming ESP log output to a UDP socket.
 *
 * Chains into the existing vprintf hook so console, SD card, and UDP all
 * receive output simultaneously.  Non-blocking (MSG_DONTWAIT): packets are
 * dropped on congestion rather than stalling the calling task.
 *
 * @param host_ip  Destination IPv4 address string (e.g. "192.168.1.66").
 * @param port     Destination UDP port.
 * @return ESP_OK on success, ESP_FAIL if socket creation or address parsing fails.
 */
esp_err_t udp_log_init(const char* host_ip, uint16_t port);

/**
 * @brief Stop UDP log streaming and restore the previous vprintf hook.
 */
void udp_log_deinit(void);

/**
 * @brief Return true if UDP log streaming is currently active.
 */
bool udp_log_is_active(void);

#ifdef __cplusplus
}
#endif

#endif // UDP_LOG_H
