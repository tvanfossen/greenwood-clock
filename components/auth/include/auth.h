/**
 * @file auth.h
 * v2: auth_pair_secs_left.
 * @version 2
 * @brief Who may use the clock's HTTP API (see auth_core.h for the rules): the admin token
 *        (the OTA token in secrets.h, which the inv tasks send) reaches everything; a phone
 *        paired by the QR code on the clock's Phone screen reaches the panel's API for 30
 *        minutes; anyone reaches only the panel's static files and the pairing endpoint.
 *        Safe from any task; needs no init (usable in the pre-launch window).
 */
#pragma once

#include <stdbool.h>
#include "auth_core.h"

#ifdef __cplusplus
extern "C" {
#endif

/** What a request needs. */
typedef enum {
    AUTH_PUBLIC = 0,   /**< Anyone: static panel files, pairing. */
    AUTH_SESSION,      /**< A paired phone or the admin token. */
    AUTH_ADMIN,        /**< Only the admin token: OTA, debug, raw files. */
} auth_level_t;

/**
 * @brief Offer a new pairing code (the Phone screen opened); it is written to @p code_out.
 * @version 1
 * @req REQ-UI-005
 */
void auth_pair_begin(char code_out[AUTH_TOKEN_LEN]);

/**
 * @brief Withdraw the pairing code (the Phone screen closed).
 * @version 1
 * @req REQ-UI-005
 */
void auth_pair_end(void);

/**
 * @brief Trade a scanned code for a session token.
 * @version 1
 * @req REQ-UI-005
 * @return Whether the code was accepted (@p token_out set then).
 */
bool auth_pair(const char *code, char token_out[AUTH_TOKEN_LEN]);

/**
 * @brief Whether @p bearer (the token after "Bearer ", or NULL) may make a request that
 *        needs @p need.
 * @version 1
 * @req REQ-UI-005
 * @return True when allowed.
 */
bool auth_allows(auth_level_t need, const char *bearer);

/**
 * @brief Seconds the offered pairing code has left.
 * @version 1
 * @req REQ-UI-005
 * @return Seconds, or 0 when none is offered (used, expired or withdrawn).
 */
int auth_pair_secs_left(void);

/**
 * @brief How many phones are paired now.
 * @version 1
 * @req REQ-UI-005
 * @return The count.
 */
int auth_sessions(void);

/**
 * @brief Disconnect every phone and withdraw the code.
 * @version 1
 * @req REQ-UI-005
 */
void auth_revoke_all(void);

#ifdef __cplusplus
}
#endif
