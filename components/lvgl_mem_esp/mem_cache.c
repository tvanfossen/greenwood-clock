/**
 * @file mem_cache.c
 * @version 1
 * @brief Free lists for LVGL's small allocations (see mem_cache.h). Pure C.
 */
#include "mem_cache.h"

/**
 * @brief The class of a request.
 * @version 1
 * @req REQ-UI-001
 * @return The class, or -1.
 */
int mem_cache_class_for_request(size_t size)
{
    size_t r = (size + MEM_CACHE_ALIGN - 1) / MEM_CACHE_ALIGN;   // 64-byte units, rounded up
    return size == 0 || r > MEM_CACHE_CLASSES ? -1 : (int)r - 1;
}

/**
 * @brief The class of a freed block.
 * @version 1
 * @req REQ-UI-001
 * @return The class, or -1.
 */
int mem_cache_class_for_block(size_t usable)
{
    size_t r = usable / MEM_CACHE_ALIGN;   // rounded down: the block fits all of its class
    return r == 0 || r > MEM_CACHE_CLASSES ? -1 : (int)r - 1;
}

/**
 * @brief Take a kept block.
 * @version 1
 * @req REQ-UI-001
 * @return It, or NULL.
 */
void *mem_cache_take(mem_cache_t *m, int c)
{
    void *p = c >= 0 && m->n[c] ? m->slot[c][--m->n[c]] : NULL;
    if (p) m->hits++;
    else m->misses++;
    return p;
}

/**
 * @brief Keep a freed block.
 * @version 1
 * @req REQ-UI-001
 * @return Whether it was kept.
 */
bool mem_cache_put(mem_cache_t *m, int c, void *p)
{
    bool keep = c >= 0 && m->n[c] < MEM_CACHE_DEPTH;
    if (keep) m->slot[c][m->n[c]++] = p;
    if (keep) m->kept++;
    else m->released++;
    return keep;
}
