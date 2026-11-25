/**
 * @file test_auth_core.c
 * v2: the code's time left.
 * @version 2
 * @brief Host tests for the pairing code and sessions: a code works once, only while
 *        offered and unexpired; a session works for its lifetime and then not; a fifth
 *        session pushes out the oldest; revoking ends everything; token comparison.
 */
#include "auth_core.h"

#include <stdio.h>
#include <string.h>

static int s_failures = 0;
static int s_checks   = 0;

#define CHECK(cond, ...)                                              \
    do {                                                              \
        s_checks++;                                                   \
        if (!(cond)) {                                                \
            s_failures++;                                             \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);               \
            printf(__VA_ARGS__);                                      \
            printf("\n");                                             \
        }                                                             \
    } while (0)

#define MIN_US (60LL * 1000000)

/**
 * @brief Sixteen bytes of "randomness" all equal to @p b (distinct per call site).
 * @version 1
 * @utility
 */
static void rnd(uint8_t out[AUTH_RANDOM_BYTES], uint8_t b)
{
    memset(out, b, AUTH_RANDOM_BYTES);
}

/**
 * @brief A code pairs once, not twice, not after it expires, not after the screen closed,
 *        and a wrong code never.
 * @version 1
 * @utility
 */
static void test_pairing(void)
{
    auth_state_t st;
    memset(&st, 0, sizeof(st));
    uint8_t r[AUTH_RANDOM_BYTES];
    char tok[AUTH_TOKEN_LEN], code[AUTH_TOKEN_LEN];

    rnd(r, 0xa1);
    auth_core_pair_begin(&st, 0, r);
    CHECK(strlen(st.code) == AUTH_TOKEN_HEX, "code is %d hex chars", AUTH_TOKEN_HEX);
    strcpy(code, st.code);
    rnd(r, 0xb2);
    CHECK(!auth_core_pair(&st, MIN_US, "0123", r, tok), "wrong code refused");
    CHECK(auth_core_pair(&st, MIN_US, code, r, tok), "right code pairs");
    CHECK(strlen(tok) == AUTH_TOKEN_HEX && strcmp(tok, code) != 0, "a fresh session token");
    CHECK(!auth_core_pair(&st, MIN_US, code, r, tok), "the code works once");

    rnd(r, 0xc3);
    auth_core_pair_begin(&st, 0, r);
    strcpy(code, st.code);
    CHECK(!auth_core_pair(&st, 11 * MIN_US, code, r, tok), "expired after 10 minutes");

    auth_core_pair_begin(&st, 0, r);
    strcpy(code, st.code);
    auth_core_pair_end(&st);
    CHECK(!auth_core_pair(&st, MIN_US, code, r, tok), "withdrawn when the screen closes");
    CHECK(!auth_core_pair(&st, MIN_US, "", r, tok), "the empty code never pairs");
}

/**
 * @brief A session works for 30 minutes from pairing; unknown tokens never.
 * @version 1
 * @utility
 */
static void test_sessions(void)
{
    auth_state_t st;
    memset(&st, 0, sizeof(st));
    uint8_t r[AUTH_RANDOM_BYTES];
    char tok[AUTH_TOKEN_LEN];
    rnd(r, 1);
    auth_core_pair_begin(&st, 0, r);
    char code[AUTH_TOKEN_LEN];
    strcpy(code, st.code);
    rnd(r, 2);
    auth_core_pair(&st, 0, code, r, tok);
    CHECK(auth_core_session_ok(&st, 29 * MIN_US, tok), "live at 29 minutes");
    CHECK(!auth_core_session_ok(&st, 31 * MIN_US, tok), "gone at 31 minutes");
    CHECK(!auth_core_session_ok(&st, MIN_US, "deadbeef"), "unknown token refused");
    CHECK(!auth_core_session_ok(&st, MIN_US, ""), "empty token refused");
    CHECK(!auth_core_session_ok(&st, MIN_US, NULL), "no token refused");
    CHECK(auth_core_sessions(&st, MIN_US) == 1, "one live session");
}

/**
 * @brief A fifth phone pushes out the oldest session; revoking ends all.
 * @version 1
 * @utility
 */
static void test_eviction_and_revoke(void)
{
    auth_state_t st;
    memset(&st, 0, sizeof(st));
    uint8_t r[AUTH_RANDOM_BYTES];
    char toks[AUTH_SESSIONS + 1][AUTH_TOKEN_LEN];
    for (int i = 0; i <= AUTH_SESSIONS; i++) {
        rnd(r, (uint8_t)(0x10 + i));
        auth_core_pair_begin(&st, i * MIN_US, r);
        char code[AUTH_TOKEN_LEN];
        strcpy(code, st.code);
        rnd(r, (uint8_t)(0x40 + i));
        CHECK(auth_core_pair(&st, i * MIN_US, code, r, toks[i]), "pair %d", i);
    }
    int64_t now = (AUTH_SESSIONS + 1) * MIN_US;
    CHECK(!auth_core_session_ok(&st, now, toks[0]), "the oldest was pushed out");
    CHECK(auth_core_session_ok(&st, now, toks[AUTH_SESSIONS]), "the newest is live");
    CHECK(auth_core_sessions(&st, now) == AUTH_SESSIONS, "four live");
    auth_core_revoke_all(&st);
    CHECK(auth_core_sessions(&st, now) == 0 && !auth_core_session_ok(&st, now, toks[1]),
          "revoked");
}

/**
 * @brief The code's time left: counts down, 0 once used, expired or withdrawn.
 * @version 1
 * @utility
 */
static void test_code_left(void)
{
    auth_state_t st;
    memset(&st, 0, sizeof(st));
    uint8_t r[AUTH_RANDOM_BYTES];
    char tok[AUTH_TOKEN_LEN], code[AUTH_TOKEN_LEN];
    CHECK(auth_core_code_secs_left(&st, 0) == 0, "none offered");
    rnd(r, 9);
    auth_core_pair_begin(&st, 0, r);
    CHECK(auth_core_code_secs_left(&st, MIN_US) == 540, "9 minutes left: %d",
          auth_core_code_secs_left(&st, MIN_US));
    CHECK(auth_core_code_secs_left(&st, 11 * MIN_US) == 0, "expired");
    strcpy(code, st.code);
    auth_core_pair(&st, MIN_US, code, r, tok);
    CHECK(auth_core_code_secs_left(&st, MIN_US) == 0, "used");
}

/**
 * @brief Token comparison.
 * @version 1
 * @utility
 */
static void test_equal(void)
{
    CHECK(auth_core_equal("abc", "abc"), "equal");
    CHECK(!auth_core_equal("abc", "abd"), "differ");
    CHECK(!auth_core_equal("abc", "abcd"), "length differs");
    CHECK(!auth_core_equal("", ""), "empty never equal");
    CHECK(!auth_core_equal(NULL, "abc"), "NULL never equal");
}

/**
 * @brief Run every check and report the tally.
 * v2: the code's time left.
 * @version 2
 * @utility
 * @return 0 when every check passed.
 */
int main(void)
{
    test_pairing();
    test_sessions();
    test_eviction_and_revoke();
    test_equal();
    test_code_left();
    printf("auth_core: %d/%d checks passed\n", s_checks - s_failures, s_checks);
    return s_failures;
}
