/**
 * @file location_census.c
 * @version 1
 * @brief The Census geocoder's request and answer (see location_census.h). Pure C.
 */
#include "location_census.h"
#include "cJSON.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define CENSUS_URL "https://geocoding.geo.census.gov/geocoder/locations/onelineaddress?address="
#define CENSUS_TAIL "&benchmark=Public_AR_Current&format=json"

/**
 * @brief Whether @p c goes into a URL query as itself.
 * @version 1
 * @utility
 * @return Whether.
 */
static bool url_safe(unsigned char c)
{
    return isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~';
}

/**
 * @brief The geocoder's URL for @p address.
 * @version 1
 * @req REQ-LOC-001
 * @return Its length, or 0.
 */
size_t location_census_url(const char *address, char *out, size_t n)
{
    size_t at = (size_t)snprintf(out, n, "%s", CENSUS_URL);
    bool ok = address && address[0] && at < n;
    for (const unsigned char *c = (const unsigned char *)address; ok && *c; c++) {
        int w = url_safe(*c) ? snprintf(out + at, n - at, "%c", *c)
                             : snprintf(out + at, n - at, "%%%02X", *c);
        at += (size_t)w;
        ok = at < n;
    }
    if (ok) at += (size_t)snprintf(out + at, n - at, "%s", CENSUS_TAIL);
    ok = ok && at < n;
    if (!ok && n) out[0] = '\0';
    return ok ? at : 0;
}

/**
 * @brief @p in ("CHICAGO", "NEW YORK") as a title ("Chicago", "New York") into @p out.
 * @version 1
 * @utility
 */
static void title_case(const char *in, char *out, size_t n)
{
    size_t i = 0;
    bool start = true;
    for (; in[i] && i + 1 < n; i++) {
        unsigned char c = (unsigned char)in[i];
        out[i] = (char)(start ? toupper(c) : tolower(c));
        start = !isalpha(c);
    }
    out[i] = '\0';
}

/**
 * @brief @p v rounded to 3 decimals (about 100 m).
 * @version 1
 * @utility
 * @return It.
 */
static float round3(double v)
{
    return (float)(round(v * 1000.0) / 1000.0);
}

/**
 * @brief Read the geocoder's answer.
 * @version 1
 * @req REQ-LOC-001
 * @return Whether it matched.
 */
bool location_census_parse(const char *json, location_place_t *out)
{
    cJSON *root = cJSON_Parse(json);
    const cJSON *result = cJSON_GetObjectItemCaseSensitive(root, "result");
    const cJSON *match = cJSON_GetArrayItem(cJSON_GetObjectItemCaseSensitive(result, "addressMatches"), 0);
    const cJSON *xy = cJSON_GetObjectItemCaseSensitive(match, "coordinates");
    const cJSON *x = cJSON_GetObjectItemCaseSensitive(xy, "x"), *y = cJSON_GetObjectItemCaseSensitive(xy, "y");
    const cJSON *parts = cJSON_GetObjectItemCaseSensitive(match, "addressComponents");
    const cJSON *city = cJSON_GetObjectItemCaseSensitive(parts, "city");
    const cJSON *state = cJSON_GetObjectItemCaseSensitive(parts, "state");
    bool ok = cJSON_IsNumber(x) && cJSON_IsNumber(y) && cJSON_IsString(city) && cJSON_IsString(state);
    if (ok) {
        char town[LOCATION_LABEL_LEN - 8];   // room for ", ST"
        title_case(city->valuestring, town, sizeof(town));
        snprintf(out->label, sizeof(out->label), "%s, %s", town, state->valuestring);
        out->lat = round3(y->valuedouble);
        out->lon = round3(x->valuedouble);
    }
    cJSON_Delete(root);
    return ok;
}
