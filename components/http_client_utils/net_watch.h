/**
 * @file net_watch.h
 * @version 1
 * @brief The network watchdog's rule (REQ-NET-005): how long the clock may go without one
 *        successful fetch before it restarts to get its Wi-Fi link back, backing off after
 *        restarts that did not help (a router outage must not restart it every few minutes
 *        and churn its three log slots). Pure C: host-tested.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NET_WATCH_BASE_S  (10 * 60)        /**< Offline this long: restart. */
#define NET_WATCH_MAX_S   (2 * 60 * 60)    /**< The longest wait, after repeated restarts. */

/**
 * @brief The wait before a restart after @p restarts watchdog restarts in a row: the base,
 *        doubled for each, at most NET_WATCH_MAX_S.
 * @version 1
 * @req REQ-NET-005
 * @return Seconds.
 */
uint32_t net_watch_limit_s(uint32_t restarts);

/**
 * @brief Whether to restart now: @p now_s since start, the last good fetch at @p last_ok_s
 *        (0: none yet; the start counts as one), after @p restarts restarts in a row.
 * @version 1
 * @req REQ-NET-005
 * @return Whether.
 */
bool net_watch_due(uint32_t now_s, uint32_t last_ok_s, uint32_t restarts);

#ifdef __cplusplus
}
#endif
