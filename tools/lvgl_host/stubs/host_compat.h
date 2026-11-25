/**
 * @file host_compat.h
 * v2: not under Emscripten (its libc has strlcpy).
 * @version 2
 * @brief Force-included in host builds of firmware sources: the libc extensions ESP-IDF's
 *        newlib has and the host glibc may lack (strlcpy, strcasestr).
 */
#pragma once
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <string.h>
#include <stddef.h>

#if !defined(__EMSCRIPTEN__) && \
    (!defined(__GLIBC__) || __GLIBC__ < 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ < 38))
/**
 * @brief BSD strlcpy: copy at most @p n - 1 bytes, always NUL-terminate.
 * @version 1
 * @utility
 * @return strlen(@p src).
 */
static inline size_t strlcpy(char *dst, const char *src, size_t n)
{
    size_t len = strlen(src);
    if (n) {
        size_t k = len < n - 1 ? len : n - 1;
        memcpy(dst, src, k);
        dst[k] = '\0';
    }
    return len;
}
#endif
