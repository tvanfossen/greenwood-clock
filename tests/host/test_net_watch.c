/**
 * @file test_net_watch.c
 * @version 1
 * @brief Host tests for the network watchdog's rule.
 */
#include "net_watch.h"

#include <stdio.h>

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

/**
 * @brief Run the tests; exit status is the failure count.
 * @version 1
 * @utility
 * @return 0 when every check passed.
 */
int main(void)
{
    CHECK(net_watch_limit_s(0) == 600, "10 min first: %u", net_watch_limit_s(0));
    CHECK(net_watch_limit_s(1) == 1200 && net_watch_limit_s(2) == 2400, "doubles");
    CHECK(net_watch_limit_s(3) == 4800 && net_watch_limit_s(4) == 7200, "capped at 2 h");
    CHECK(net_watch_limit_s(40) == 7200, "no overflow");
    CHECK(!net_watch_due(599, 0, 0), "start counts as good: not yet");
    CHECK(net_watch_due(600, 0, 0), "10 min from start");
    CHECK(!net_watch_due(5000, 4500, 0), "a recent good fetch");
    CHECK(net_watch_due(5100, 4500, 0), "10 min after the last good fetch");
    CHECK(!net_watch_due(5100, 4500, 1), "backed off after a restart that did not help");
    CHECK(!net_watch_due(10, 20, 0), "a good fetch stamped after now (clock order)");
    printf("net_watch: %d/%d checks passed\n", s_checks - s_failures, s_checks);
    return s_failures;
}
