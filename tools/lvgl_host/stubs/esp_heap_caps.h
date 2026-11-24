/**
 * @file esp_heap_caps.h
 * @version 1
 * @brief Host stand-in for ESP-IDF capability-aware heap calls (plain malloc).
 */
#pragma once
#include <stdlib.h>

#define MALLOC_CAP_SPIRAM   (1 << 10)
#define MALLOC_CAP_INTERNAL (1 << 11)
#define MALLOC_CAP_8BIT     (1 << 2)
#define heap_caps_malloc(n, caps) malloc(n)
#define heap_caps_calloc(c, n, caps) calloc(c, n)
#define heap_caps_aligned_alloc(al, n, caps) aligned_alloc(al, ((n) + (al) - 1) / (al) * (al))
#define heap_caps_free(p) free(p)
#define heap_caps_get_free_size(caps) ((size_t)8 << 20)
