/**
 * @file mem_cache.h
 * @version 1
 * @brief Free lists for LVGL's small allocations: freed blocks up to MEM_CACHE_MAX bytes are
 *        kept by 64-byte size class and handed back to the next allocation of that class,
 *        instead of a locked heap call each time. Measured on device: ~2,200 lv_malloc/free
 *        calls a frame under snowfall (a draw task and its descriptor per redraw area),
 *        ~13 ms a frame. A block sits in the class its usable size floors to, so it always
 *        fits any request of that class. Pure C (the caller locks): host-tested.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MEM_CACHE_ALIGN   64     /**< Every block is this aligned (the PPA's need). */
#define MEM_CACHE_MAX     1024   /**< Largest block kept. */
#define MEM_CACHE_CLASSES (MEM_CACHE_MAX / MEM_CACHE_ALIGN)
#define MEM_CACHE_DEPTH   64     /**< Blocks kept per class (bounds what the lists hold). */

/** The free lists. */
typedef struct {
    void    *slot[MEM_CACHE_CLASSES][MEM_CACHE_DEPTH];
    uint16_t n[MEM_CACHE_CLASSES];
    uint32_t hits, misses, kept, released;   /**< For the health log. */
} mem_cache_t;

/**
 * @brief The class of a request of @p size bytes (rounded up to MEM_CACHE_ALIGN).
 * @version 1
 * @req REQ-UI-001
 * @return 0..MEM_CACHE_CLASSES-1, or -1 when larger than MEM_CACHE_MAX (or 0 bytes).
 */
int mem_cache_class_for_request(size_t size);

/**
 * @brief The class a freed block of usable size @p usable belongs to (rounded down).
 * @version 1
 * @req REQ-UI-001
 * @return 0..MEM_CACHE_CLASSES-1, or -1 when it is not kept.
 */
int mem_cache_class_for_block(size_t usable);

/**
 * @brief A kept block for a request of class @p c.
 * @version 1
 * @req REQ-UI-001
 * @return It, or NULL (the caller allocates).
 */
void *mem_cache_take(mem_cache_t *m, int c);

/**
 * @brief Keep freed block @p p of class @p c.
 * @version 1
 * @req REQ-UI-001
 * @return Whether it was kept (else the caller frees it).
 */
bool mem_cache_put(mem_cache_t *m, int c, void *p);

#ifdef __cplusplus
}
#endif
