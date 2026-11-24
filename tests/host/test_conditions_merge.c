/**
 * @file test_conditions_merge.c
 * @version 1
 * @brief Host tests for filling an observation's missing values from another station.
 */
#include "nws_conditions_merge.h"

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

/**
 * @brief An observation with every value present.
 * @version 1
 * @utility
 * @return The observation.
 */
static nws_conditions_t complete(float temp, int humidity, const char *desc)
{
    nws_conditions_t c;
    memset(&c, 0, sizeof(c));
    c.temp_c = temp;
    c.feels_like_c = temp - 2.0f;
    c.dew_point_c = temp - 5.0f;
    c.humidity = humidity;
    snprintf(c.description, sizeof(c.description), "%s", desc);
    c.valid = true;
    return c;
}

/**
 * @brief The station that sent nulls: temperature, feels-like, dew point, humidity missing.
 * @version 1
 * @utility
 * @return The observation.
 */
static nws_conditions_t blank_station(void)
{
    nws_conditions_t c = complete(0.0f, 0, "Partly Cloudy");
    c.temp_c = NWS_TEMP_MISSING;
    c.feels_like_c = NWS_TEMP_MISSING;
    c.dew_point_c = NWS_TEMP_MISSING;
    c.humidity = NWS_HUMIDITY_MISSING;
    return c;
}

/**
 * @brief Complete observations need no other station; gaps do.
 * @version 1
 * @utility
 */
static void test_incomplete(void)
{
    nws_conditions_t ok = complete(4.0f, 80, "Cloudy");
    nws_conditions_t blank = blank_station();
    nws_conditions_t no_desc = complete(4.0f, 80, "");
    CHECK(!nws_conditions_incomplete(&ok), "complete");
    CHECK(nws_conditions_incomplete(&blank), "nulls from the station");
    CHECK(nws_conditions_incomplete(&no_desc), "no description");
}

/**
 * @brief Missing values come from the other station; present ones are kept.
 * @version 1
 * @utility
 */
static void test_fill(void)
{
    nws_conditions_t dst = blank_station();
    nws_conditions_t src = complete(3.0f, 91, "Light Snow");
    int n = nws_conditions_fill(&dst, &src);
    CHECK(n == 4, "temp, feels-like, dew point, humidity filled: %d", n);
    CHECK(dst.temp_c == 3.0f && dst.humidity == 91, "values from the other station");
    CHECK(strcmp(dst.description, "Partly Cloudy") == 0, "own description kept");
    CHECK(!nws_conditions_incomplete(&dst), "complete afterwards");
}

/**
 * @brief A source that lacks the same values fills nothing.
 * @version 1
 * @utility
 */
static void test_fill_from_blank(void)
{
    nws_conditions_t dst = blank_station();
    nws_conditions_t src = blank_station();
    CHECK(nws_conditions_fill(&dst, &src) == 0, "nothing to fill from");
    CHECK(dst.temp_c == NWS_TEMP_MISSING, "still missing");
}

/**
 * @brief Run the conditions merge tests; exit status is the failure count.
 * @version 1
 * @utility
 * @return 0 when every check passed.
 */
int main(void)
{
    test_incomplete();
    test_fill();
    test_fill_from_blank();
    printf("conditions_merge: %d/%d checks passed\n", s_checks - s_failures, s_checks);
    return s_failures;
}
