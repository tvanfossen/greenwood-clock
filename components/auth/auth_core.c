/**
 * @file auth_core.c
 * v2: auth_core_code_secs_left.
 * @version 2
 * @brief Pairing code and sessions as pure state (see auth_core.h).
 */
#include "auth_core.h"

#include <string.h>

/**
 * @brief Write @p rnd as lower-case hex into @p out (AUTH_TOKEN_LEN bytes).
 * @version 1
 * @utility
 */
static void to_hex(const uint8_t rnd[AUTH_RANDOM_BYTES], char out[AUTH_TOKEN_LEN])
{
    static const char HEX[] = "0123456789abcdef";
    for (int i = 0; i < AUTH_RANDOM_BYTES; i++) {
        out[2 * i] = HEX[rnd[i] >> 4];
        out[2 * i + 1] = HEX[rnd[i] & 0xf];
    }
    out[AUTH_TOKEN_HEX] = '\0';
}

/**
 * @brief Compare in constant time.
 * @version 1
 * @req REQ-UI-005
 * @return Whether equal (and not empty).
 */
bool auth_core_equal(const char *a, const char *b)
{
    bool ok = a && b && a[0];
    size_t la = ok ? strlen(a) : 0, lb = ok ? strlen(b) : 1;
    unsigned diff = la != lb;
    for (size_t i = 0; ok && i < la && i < lb; i++) diff |= (unsigned)(a[i] ^ b[i]);
    return ok && diff == 0;
}

/**
 * @brief Offer a new pairing code.
 * @version 1
 * @req REQ-UI-005
 */
void auth_core_pair_begin(auth_state_t *st, int64_t now_us, const uint8_t rnd[AUTH_RANDOM_BYTES])
{
    to_hex(rnd, st->code);
    st->code_expires_us = now_us + AUTH_PAIR_TTL_US;
}

/**
 * @brief Withdraw the pairing code.
 * @version 1
 * @req REQ-UI-005
 */
void auth_core_pair_end(auth_state_t *st)
{
    memset(st->code, 0, sizeof(st->code));
    st->code_expires_us = 0;
}

/**
 * @brief The session slot to fill: a free or expired one, else the one ending soonest.
 * @version 1
 * @utility
 * @return Its index.
 */
static int free_slot(const auth_state_t *st, int64_t now_us)
{
    int best = 0;
    for (int i = 0; i < AUTH_SESSIONS; i++) {
        bool dead = !st->sessions[i].token[0] || st->sessions[i].expires_us <= now_us;
        if (dead) return i;
        if (st->sessions[i].expires_us < st->sessions[best].expires_us) best = i;
    }
    return best;
}

/**
 * @brief Trade the code for a session.
 * @version 1
 * @req REQ-UI-005
 * @return Whether accepted.
 */
bool auth_core_pair(auth_state_t *st, int64_t now_us, const char *code,
                    const uint8_t rnd[AUTH_RANDOM_BYTES], char token_out[AUTH_TOKEN_LEN])
{
    bool ok = now_us < st->code_expires_us && auth_core_equal(code, st->code);
    if (ok) {
        auth_core_pair_end(st);   // used up
        int i = free_slot(st, now_us);
        to_hex(rnd, st->sessions[i].token);
        st->sessions[i].expires_us = now_us + AUTH_SESSION_TTL_US;
        memcpy(token_out, st->sessions[i].token, AUTH_TOKEN_LEN);
    }
    return ok;
}

/**
 * @brief Whether a token is a live session.
 * @version 1
 * @req REQ-UI-005
 * @return True when it is.
 */
bool auth_core_session_ok(const auth_state_t *st, int64_t now_us, const char *token)
{
    bool ok = false;
    for (int i = 0; i < AUTH_SESSIONS; i++) {
        ok |= st->sessions[i].expires_us > now_us && auth_core_equal(token, st->sessions[i].token);
    }
    return ok;
}

/**
 * @brief Seconds the offered code has left.
 * @version 1
 * @req REQ-UI-005
 * @return Seconds, or 0.
 */
int auth_core_code_secs_left(const auth_state_t *st, int64_t now_us)
{
    int64_t left = st->code[0] ? st->code_expires_us - now_us : 0;
    return left > 0 ? (int)(left / 1000000) : 0;
}

/**
 * @brief How many sessions are live.
 * @version 1
 * @req REQ-UI-005
 * @return The count.
 */
int auth_core_sessions(const auth_state_t *st, int64_t now_us)
{
    int n = 0;
    for (int i = 0; i < AUTH_SESSIONS; i++) {
        n += st->sessions[i].token[0] && st->sessions[i].expires_us > now_us;
    }
    return n;
}

/**
 * @brief End everything.
 * @version 1
 * @req REQ-UI-005
 */
void auth_core_revoke_all(auth_state_t *st)
{
    memset(st, 0, sizeof(*st));
}
