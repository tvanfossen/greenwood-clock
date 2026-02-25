// components/udp_log/udp_log.c

#include "udp_log.h"
#include "esp_log.h"
#include <string.h>
#include <stdarg.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

static const char* TAG = "udp_log";

// 1 MTU minus IP+UDP headers — avoids fragmentation on most networks.
#define UDP_LOG_BUF_SIZE  1408

static int              s_sock        = -1;
static struct sockaddr_in s_dest;
static vprintf_like_t   s_prev_vprintf = NULL;
static bool             s_active      = false;
static bool             s_in_hook     = false;  // reentrancy guard

static char             s_buf[UDP_LOG_BUF_SIZE];

// =============================================================================
// Internal helpers
// =============================================================================

static int udp_log_vprintf(const char* fmt, va_list args)
{
    // Copy args before passing to the chained vprintf (consuming args is UB).
    va_list udp_args;
    va_copy(udp_args, args);

    int ret = s_prev_vprintf ? s_prev_vprintf(fmt, args) : 0;

    if (!s_active || s_in_hook) {
        va_end(udp_args);
        return ret;
    }
    s_in_hook = true;

    int n = vsnprintf(s_buf, sizeof(s_buf), fmt, udp_args);
    va_end(udp_args);

    if (n > 0) {
        sendto(s_sock, s_buf, (size_t)n, MSG_DONTWAIT,
               (struct sockaddr*)&s_dest, sizeof(s_dest));
        // Dropped packets on congestion are acceptable — never block LVGL task.
    }

    s_in_hook = false;
    return ret;
}

// =============================================================================
// Public API
// =============================================================================

/**
 * @brief Create a UDP socket for log streaming.
 *
 * @return Socket fd on success, or -1 on failure (error already logged).
 */
static int udp_log_create_socket(void)
{
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) {
        ESP_LOGE(TAG, "Failed to create UDP socket: errno %d", errno);
    }
    return sock;
}

/**
 * @brief Populate @p dest with the target address for UDP log packets.
 *
 * @param host_ip  IPv4 address string.
 * @param port     Destination UDP port.
 * @param dest     Output sockaddr_in to populate.
 * @return ESP_OK on success, ESP_FAIL if host_ip is not a valid IPv4 address.
 */
static esp_err_t udp_log_build_dest(const char* host_ip, uint16_t port,
                                     struct sockaddr_in* dest)
{
    memset(dest, 0, sizeof(*dest));
    dest->sin_family = AF_INET;
    dest->sin_port   = htons(port);
    if (inet_pton(AF_INET, host_ip, &dest->sin_addr) != 1) {
        ESP_LOGE(TAG, "Invalid host IP address: %s", host_ip);
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t udp_log_init(const char* host_ip, uint16_t port)
{
    if (s_active) udp_log_deinit();
    s_sock = udp_log_create_socket();
    if (s_sock < 0) return ESP_FAIL;
    if (udp_log_build_dest(host_ip, port, &s_dest) != ESP_OK) {
        close(s_sock);
        s_sock = -1;
        return ESP_FAIL;
    }
    s_active       = true;
    s_prev_vprintf = esp_log_set_vprintf(udp_log_vprintf);
    ESP_LOGI(TAG, "UDP log streaming started → %s:%u", host_ip, (unsigned)port);
    return ESP_OK;
}

void udp_log_deinit(void)
{
    if (!s_active) return;
    s_active = false;
    esp_log_set_vprintf(s_prev_vprintf);
    s_prev_vprintf = NULL;
    if (s_sock >= 0) {
        close(s_sock);
        s_sock = -1;
    }
    ESP_LOGI(TAG, "UDP log streaming stopped");
}

bool udp_log_is_active(void)
{
    return s_active;
}
