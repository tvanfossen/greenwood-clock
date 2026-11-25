/**
 * @file auth_core.h
 * v2: auth_core_code_secs_left.
 * @version 2
 * @brief Who may use the clock's HTTP API, as pure state (time and randomness passed in, so it
 *        is host-tested): a one-time pairing code that the clock shows as a QR code while its
 *        Phone screen is open, and the short sessions a phone gets by presenting that code.
 *        A code works once and only until it expires or the screen closes; a session lasts a
 *        fixed time from the scan; the oldest session makes room for a new one.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AUTH_TOKEN_HEX      32            /**< 128 random bits as hex. */
#define AUTH_TOKEN_LEN      (AUTH_TOKEN_HEX + 1)
#define AUTH_RANDOM_BYTES   16
#define AUTH_PAIR_TTL_US    (10LL * 60 * 1000000)   /**< A code works for 10 minutes. */
#define AUTH_SESSION_TTL_US (30LL * 60 * 1000000)   /**< A session lasts 30 minutes. */
#define AUTH_SESSIONS       4

/** The pairing code and the sessions. Zero it to start. */
typedef struct {
    char    code[AUTH_TOKEN_LEN];   /**< "" when no code is offered. */
    int64_t code_expires_us;
    struct {
        char    token[AUTH_TOKEN_LEN];   /**< "" when free. */
        int64_t expires_us;
    } sessions[AUTH_SESSIONS];
} auth_state_t;

/**
 * @brief Offer a new pairing code made from @p rnd (replacing any earlier one) until
 *        @p now_us + AUTH_PAIR_TTL_US.
 * @version 1
 * @req REQ-UI-005
 */
void auth_core_pair_begin(auth_state_t *st, int64_t now_us, const uint8_t rnd[AUTH_RANDOM_BYTES]);

/**
 * @brief Withdraw the pairing code (the Phone screen closed). Sessions stay.
 * @version 1
 * @req REQ-UI-005
 */
void auth_core_pair_end(auth_state_t *st);

/**
 * @brief Trade @p code for a session: when it is the offered, unexpired code, the code is
 *        used up and a session token made from @p rnd is written to @p token_out.
 * @version 1
 * @req REQ-UI-005
 * @return Whether the code was accepted.
 */
bool auth_core_pair(auth_state_t *st, int64_t now_us, const char *code,
                    const uint8_t rnd[AUTH_RANDOM_BYTES], char token_out[AUTH_TOKEN_LEN]);

/**
 * @brief Whether @p token is a live session at @p now_us.
 * @version 1
 * @req REQ-UI-005
 * @return True when it is.
 */
bool auth_core_session_ok(const auth_state_t *st, int64_t now_us, const char *token);

/**
 * @brief Seconds the offered code has left at @p now_us.
 * @version 1
 * @req REQ-UI-005
 * @return Seconds, or 0 when none is offered (used, expired or withdrawn).
 */
int auth_core_code_secs_left(const auth_state_t *st, int64_t now_us);

/**
 * @brief How many sessions are live at @p now_us.
 * @version 1
 * @req REQ-UI-005
 * @return The count.
 */
int auth_core_sessions(const auth_state_t *st, int64_t now_us);

/**
 * @brief End every session and withdraw the code.
 * @version 1
 * @req REQ-UI-005
 */
void auth_core_revoke_all(auth_state_t *st);

/**
 * @brief Compare two strings in time that does not depend on where they differ.
 * @version 1
 * @req REQ-UI-005
 * @return Whether they are equal (two NULLs or an empty one are never equal).
 */
bool auth_core_equal(const char *a, const char *b);

#ifdef __cplusplus
}
#endif
