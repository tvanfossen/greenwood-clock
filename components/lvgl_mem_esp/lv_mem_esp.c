/**
 * @file lv_mem_esp.c
 * v2: freed blocks up to 1 KB are kept on free lists by size class and handed back (mem_cache):
 *     LVGL allocates and frees a draw task and its descriptor per redraw area, ~2,200 heap
 *     calls a frame under snowfall (~13 ms, measured).
 * @version 2
 * @brief ESP-IDF PSRAM/SPIRAM memory allocator for LVGL
 *
 * Provides lv_malloc_core/lv_realloc_core/lv_free_core functions
 * that allocate from SPIRAM when available, falling back to internal RAM.
 */

#include "mem_cache.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include <string.h>

static const char *TAG = "lv_mem_esp";

// The free lists, shared by every task that allocates through LVGL (lodepng runs in the
// Library's task too): a spinlock, held only for the list operation.
static mem_cache_t  s_cache;
static portMUX_TYPE s_cache_mux = portMUX_INITIALIZER_UNLOCKED;

// Forward declarations
void * lv_malloc_core(size_t size);
void * lv_realloc_core(void * p, size_t new_size);
void lv_free_core(void * p);
void lv_mem_init(void);
void lv_mem_deinit(void);
void lv_mem_monitor_core(void * mon_p);

/**
 * @brief A new 64-byte-aligned block of @p aligned_size: SPIRAM first above 4 KB, else the
 *        default heap.
 * @version 1
 * @utility
 * @return It, or NULL.
 */
static void *heap_alloc(size_t aligned_size)
{
    void *ptr = NULL;
    if (aligned_size > 4096) {
        ptr = heap_caps_aligned_alloc(MEM_CACHE_ALIGN, aligned_size, MALLOC_CAP_SPIRAM);
        if (!ptr) ESP_LOGW(TAG, "SPIRAM allocation failed for %u bytes, trying internal RAM", (unsigned)aligned_size);
    }
    return ptr ? ptr : heap_caps_aligned_alloc(MEM_CACHE_ALIGN, aligned_size, MALLOC_CAP_DEFAULT);
}

/**
 * @brief Allocate memory with SPIRAM preference and 64-byte alignment for PPA
 * Falls back to internal RAM if SPIRAM allocation fails
 * v2: a kept block of the request's class first (mem_cache); a new small block is the
 *     class's full size, so it returns to that class when freed.
 * @version 2
 * @req REQ-MEM-001
 * @return 64-byte-aligned block of @p size rounded up to 64 bytes, or NULL on failure.
 */
void * lv_malloc_core(size_t size)
{
    int c = mem_cache_class_for_request(size);
    void *p = NULL;
    if (c >= 0) {
        portENTER_CRITICAL(&s_cache_mux);
        p = mem_cache_take(&s_cache, c);
        portEXIT_CRITICAL(&s_cache_mux);
    }
    // PPA requires 64-byte alignment for both ADDRESS and SIZE
    size_t aligned_size = c >= 0 ? (size_t)(c + 1) * MEM_CACHE_ALIGN
                                 : (size + MEM_CACHE_ALIGN - 1) & ~(size_t)(MEM_CACHE_ALIGN - 1);
    if (!p) p = heap_alloc(aligned_size);
    return p;
}

/**
 * @brief Reallocate memory with SPIRAM preference and 64-byte alignment for PPA
 * Note: heap_caps_realloc doesn't guarantee alignment, so we allocate new + copy
 * v2: the old block goes back through lv_free_core (its free list).
 * @version 2
 * @req REQ-MEM-001
 * @return New aligned block holding the old contents, or NULL if @p new_size is 0 or allocation failed.
 */
void * lv_realloc_core(void * p, size_t new_size)
{
    if (p == NULL) {
        return lv_malloc_core(new_size);
    }

    if (new_size == 0) {
        lv_free_core(p);
        return NULL;
    }

    // Allocate new aligned buffer
    void *new_ptr = lv_malloc_core(new_size);
    if (new_ptr) {
        // Get the actual allocated size of the old buffer
        size_t old_size = heap_caps_get_allocated_size(p);

        // Copy old data to new buffer (copy the smaller of old_size or new_size)
        size_t copy_size = (old_size < new_size) ? old_size : new_size;
        memcpy(new_ptr, p, copy_size);
        lv_free_core(p);
    }

    return new_ptr;
}

/**
 * @brief Free memory
 * v2: a small block is kept on its class's free list (up to MEM_CACHE_DEPTH), else freed.
 * @version 2
 * @req REQ-MEM-001
 */
void lv_free_core(void * p)
{
    if (p) {
        // Only blocks this allocator made are kept (64-byte aligned): one freed through
        // lv_free from elsewhere must not be handed out later as an aligned block.
        bool ours = ((uintptr_t)p % MEM_CACHE_ALIGN) == 0;
        int c = ours ? mem_cache_class_for_block(heap_caps_get_allocated_size(p)) : -1;
        portENTER_CRITICAL(&s_cache_mux);
        bool kept = mem_cache_put(&s_cache, c, p);
        portEXIT_CRITICAL(&s_cache_mux);
        if (!kept) heap_caps_free(p);
    }
}

/**
 * @brief Initialize memory - no-op for heap_caps allocator
 * (only logs the allocator configuration).
 * v2: says that small blocks are kept on free lists.
 * @version 2
 * @req REQ-MEM-001
 */
void lv_mem_init(void)
{
    ESP_LOGI(TAG, "LVGL memory using ESP-IDF heap_caps with SPIRAM preference");
    ESP_LOGI(TAG, "All allocations aligned to 64 bytes for PPA compatibility; blocks up to %d B kept on free lists (%d per class)",
             MEM_CACHE_MAX, MEM_CACHE_DEPTH);
}

/**
 * @brief Deinitialize memory - no-op for heap_caps allocator
 * @version 1
 * @req REQ-MEM-001
 */
void lv_mem_deinit(void)
{
    // Nothing to clean up
}

/**
 * @brief Get memory monitor info
 * Currently a no-op: @p mon_p is left untouched (LVGL headers are not
 * available here to fill lv_mem_monitor_t), so the LVGL monitor reports nothing.
 * @version 1
 * @req REQ-MEM-001
 */
void lv_mem_monitor_core(void * mon_p)
{
    // LVGL expects lv_mem_monitor_t structure but we can't include LVGL headers
    // The structure has: total_size, free_cnt, free_size, free_biggest_size, used_cnt, max_used, used_pct, frag_pct
    // For now, leave it as no-op - monitor won't work but won't crash
    (void)mon_p;
}
