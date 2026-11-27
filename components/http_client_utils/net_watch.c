/**
 * @file net_watch.c
 * @version 1
 * @brief The network watchdog's rule (see net_watch.h). Pure C.
 */
#include "net_watch.h"

/**
 * @brief The wait before a restart.
 * @version 1
 * @req REQ-NET-005
 * @return Seconds.
 */
uint32_t net_watch_limit_s(uint32_t restarts)
{
    uint32_t limit = NET_WATCH_BASE_S;
    for (uint32_t i = 0; i < restarts && limit < NET_WATCH_MAX_S; i++) limit *= 2;
    return limit < NET_WATCH_MAX_S ? limit : NET_WATCH_MAX_S;
}

/**
 * @brief Whether to restart now.
 * @version 1
 * @req REQ-NET-005
 * @return Whether.
 */
bool net_watch_due(uint32_t now_s, uint32_t last_ok_s, uint32_t restarts)
{
    return now_s >= last_ok_s && now_s - last_ok_s >= net_watch_limit_s(restarts);
}
