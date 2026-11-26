/**
 * @file test_location.c
 * @version 1
 * @brief Host tests for setting the clock's place: the Census geocoder's URL (encoded), its
 *        answers (a real match for a public building, Chicago City Hall, and a real no-match),
 *        and the timezone table (rules, regions, places).
 */
#include "location_census.h"
#include "tz.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
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
 * @brief A fixture file's text (free it).
 * @version 1
 * @utility
 * @return It, or NULL.
 */
static char *fixture(const char *name)
{
    char path[256];
    snprintf(path, sizeof(path), "tests/host/fixtures/%s", name);
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    char *buf = calloc(1, 65536);
    size_t got = buf ? fread(buf, 1, 65535, f) : 0;
    if (buf) buf[got] = '\0';
    fclose(f);
    return buf;
}

/**
 * @brief The URL: the address percent-encoded into the one-line query; empty or too long
 *        refused.
 * @version 1
 * @utility
 */
static void test_url(void)
{
    char url[256];
    size_t n = location_census_url("121 N LaSalle St, Chicago, IL", url, sizeof(url));
    CHECK(n > 0 && strstr(url, "address=121%20N%20LaSalle%20St%2C%20Chicago%2C%20IL&") != NULL,
          "encoded: %s", url);
    static const char BASE[] = "https://geocoding.geo.census.gov/geocoder/locations/onelineaddress?";
    CHECK(strncmp(url, BASE, sizeof(BASE) - 1) == 0,
          "the Census geocoder over https: %s", url);
    CHECK(strstr(url, "format=json") != NULL, "json");
    CHECK(location_census_url("", url, sizeof(url)) == 0, "empty refused");
    CHECK(location_census_url("121 N LaSalle St", url, 40) == 0, "too long for the buffer refused");
}

/**
 * @brief The answers: a match gives "City, ST" and coordinates to 3 decimals; none, nothing.
 * @version 1
 * @utility
 */
static void test_parse(void)
{
    char *match = fixture("census_match.json"), *none = fixture("census_none.json");
    location_place_t p;
    memset(&p, 0, sizeof(p));
    CHECK(match && location_census_parse(match, &p), "a match");
    CHECK(strcmp(p.label, "Chicago, IL") == 0, "label: %s", p.label);
    CHECK(fabsf(p.lat - 41.883f) < 0.0006f && fabsf(p.lon + 87.632f) < 0.0006f,
          "coordinates to 3 decimals: %.4f %.4f", (double)p.lat, (double)p.lon);
    CHECK(none && !location_census_parse(none, &p), "no match");
    CHECK(!location_census_parse("not json", &p), "garbage");
    free(match);
    free(none);
}

/**
 * @brief The timezones: rules by name, the regions, the places in one.
 * @version 1
 * @utility
 */
static void test_tz(void)
{
    const char *r = tz_posix("America/Chicago");
    CHECK(r && strcmp(r, "CST6CDT,M3.2.0,M11.1.0") == 0, "Chicago: %s", r ? r : "(none)");
    r = tz_posix("Asia/Kolkata");
    CHECK(r && strcmp(r, "IST-5:30") == 0, "Kolkata: %s", r ? r : "(none)");
    CHECK(tz_posix("UTC") != NULL, "UTC");
    CHECK(tz_posix("Mars/Olympus") == NULL, "not a zone");
    CHECK(TZ_ZONE_COUNT > 250, "every canonical zone: %d", TZ_ZONE_COUNT);
    char regions[16][TZ_REGION_LEN];
    int n = tz_regions(regions, 16);
    int america = -1, europe = -1, utc = -1;
    for (int i = 0; i < n; i++) {
        if (strcmp(regions[i], "America") == 0) america = i;
        if (strcmp(regions[i], "Europe") == 0) europe = i;
        if (strcmp(regions[i], "UTC") == 0) utc = i;
    }
    CHECK(america >= 0 && europe > america && utc >= 0, "regions in order (%d)", n);
    static int idx[400];
    int k = tz_zones_in("America", idx, 400);
    bool chicago = false;
    for (int i = 0; i < k; i++) {
        CHECK(strncmp(TZ_ZONES[idx[i]].name, "America/", 8) == 0, "%s in America",
              TZ_ZONES[idx[i]].name);
        chicago |= strcmp(tz_place(TZ_ZONES[idx[i]].name), "Chicago") == 0;
    }
    CHECK(k > 100 && chicago, "America has Chicago among %d", k);
    CHECK(strcmp(tz_place("America/Argentina/Salta"), "Argentina/Salta") == 0, "nested place");
    CHECK(strcmp(tz_place("UTC"), "UTC") == 0, "UTC's place");
}

/**
 * @brief Run the location tests (from the repo root); exit status is the failure count.
 * @version 1
 * @utility
 * @return 0 when every check passed.
 */
int main(void)
{
    test_url();
    test_parse();
    test_tz();
    printf("location: %d/%d checks passed\n", s_checks - s_failures, s_checks);
    return s_failures;
}
