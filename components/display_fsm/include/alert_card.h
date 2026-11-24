/**
 * @file alert_card.h
 * @version 1
 * @brief Alert details card: what the banner's one-line headline leaves out. A modal card
 *        over everything (event, severity / urgency / certainty, onset and expiry, area, the
 *        full description and the safety instructions), paging through every active alert.
 *        Closes on a tap outside it, Close, after a minute, or alert_card_close(). Pure LVGL
 *        + nws types: the host preview renders it. Caller holds the LVGL lock.
 */
#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

struct nws_alerts_t;

/**
 * @brief Open the card on @p layer (the top layer on the device) at alert @p index, with its
 *        own copy of @p alerts (the NWS task rewrites them in place). Replaces an open card.
 * @version 1
 * @req REQ-ALERT-002
 */
void alert_card_open(lv_obj_t *layer, const struct nws_alerts_t *alerts, int index);

/**
 * @brief Close the card if it is open.
 * @version 1
 * @req REQ-ALERT-002
 */
void alert_card_close(void);

/**
 * @brief Whether the card is open.
 * @version 1
 * @req REQ-ALERT-002
 * @return true while it shows.
 */
bool alert_card_is_open(void);

#ifdef __cplusplus
}
#endif
