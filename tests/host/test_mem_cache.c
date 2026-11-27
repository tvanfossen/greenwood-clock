/**
 * @file test_mem_cache.c
 * @version 1
 * @brief Host tests for LVGL's small-allocation free lists: classes for requests and freed
 *        blocks, a kept block always fits its class, LIFO reuse, the depth cap.
 */
#include "mem_cache.h"

#include <stdio.h>
#include <stdlib.h>

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
    CHECK(mem_cache_class_for_request(0) == -1, "0 bytes: not cached");
    CHECK(mem_cache_class_for_request(1) == 0 && mem_cache_class_for_request(64) == 0, "1..64 -> class 0");
    CHECK(mem_cache_class_for_request(65) == 1, "65 -> class 1");
    CHECK(mem_cache_class_for_request(1024) == 15 && mem_cache_class_for_request(1025) == -1, "the top");
    CHECK(mem_cache_class_for_block(63) == -1 && mem_cache_class_for_block(64) == 0, "block floor");
    CHECK(mem_cache_class_for_block(136) == 1 && mem_cache_class_for_block(1088) == -1, "136 -> 1; 1088 too big");
    int bad = 0;   // a block kept in a class fits every request of that class
    for (size_t usable = 1; usable <= 1200; usable++) {
        int c = mem_cache_class_for_block(usable);
        if (c < 0) continue;
        for (size_t req = 1; req <= 1024; req++) bad += mem_cache_class_for_request(req) == c && req > usable;
    }
    CHECK(bad == 0, "%d requests got a block too small", bad);
    static mem_cache_t m;
    int a, b;
    CHECK(mem_cache_take(&m, 3) == NULL, "empty: miss");
    CHECK(mem_cache_put(&m, 3, &a) && mem_cache_put(&m, 3, &b), "kept");
    CHECK(mem_cache_take(&m, 3) == &b && mem_cache_take(&m, 3) == &a && mem_cache_take(&m, 3) == NULL, "LIFO");
    CHECK(!mem_cache_put(&m, -1, &a), "a class -1 block is freed");
    int kept = 0;
    for (int i = 0; i < MEM_CACHE_DEPTH + 5; i++) kept += mem_cache_put(&m, 0, &a) ? 1 : 0;
    CHECK(kept == MEM_CACHE_DEPTH, "capped at %d (%d)", MEM_CACHE_DEPTH, kept);
    printf("mem_cache: %d/%d checks passed\n", s_checks - s_failures, s_checks);
    return s_failures;
}
