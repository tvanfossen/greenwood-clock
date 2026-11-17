/**
 * @file lv_mem_esp.c
 * @version 1
 * @brief ESP-IDF PSRAM/SPIRAM memory allocator for LVGL
 *
 * Provides lv_malloc_core/lv_realloc_core/lv_free_core functions
 * that allocate from SPIRAM when available, falling back to internal RAM.
 */

#include "esp_heap_caps.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "lv_mem_esp";

// Forward declarations
void * lv_malloc_core(size_t size);
void * lv_realloc_core(void * p, size_t new_size);
void lv_free_core(void * p);
void lv_mem_init(void);
void lv_mem_deinit(void);
void lv_mem_monitor_core(void * mon_p);

/**
 * @brief Allocate memory with SPIRAM preference and 64-byte alignment for PPA
 * Falls back to internal RAM if SPIRAM allocation fails
 * @version 1
 * @req REQ-MEM-001
 * @return 64-byte-aligned block of @p size rounded up to 64 bytes, or NULL on failure.
 */
void * lv_malloc_core(size_t size)
{
    // PPA requires 64-byte alignment for both ADDRESS and SIZE
    const size_t alignment = 64;

    // Round size up to multiple of 64 bytes for PPA compatibility
    size_t aligned_size = (size + alignment - 1) & ~(alignment - 1);

    // Try SPIRAM first for large allocations (>4KB)
    if (aligned_size > 4096) {
        void *ptr = heap_caps_aligned_alloc(alignment, aligned_size, MALLOC_CAP_SPIRAM);
        if (ptr) {
            return ptr;
        }
        ESP_LOGW(TAG, "SPIRAM allocation failed for %u bytes, trying internal RAM", aligned_size);
    }

    // Fall back to default heap with alignment
    return heap_caps_aligned_alloc(alignment, aligned_size, MALLOC_CAP_DEFAULT);
}

/**
 * @brief Reallocate memory with SPIRAM preference and 64-byte alignment for PPA
 * Note: heap_caps_realloc doesn't guarantee alignment, so we allocate new + copy
 * @version 1
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
    if (new_ptr && p) {
        // Get the actual allocated size of the old buffer
        size_t old_size = heap_caps_get_allocated_size(p);

        // Copy old data to new buffer (copy the smaller of old_size or new_size)
        size_t copy_size = (old_size < new_size) ? old_size : new_size;
        memcpy(new_ptr, p, copy_size);
        heap_caps_free(p);
    }

    return new_ptr;
}

/**
 * @brief Free memory
 * @version 1
 * @req REQ-MEM-001
 */
void lv_free_core(void * p)
{
    if (p) {
        heap_caps_free(p);
    }
}

/**
 * @brief Initialize memory - no-op for heap_caps allocator
 * (only logs the allocator configuration).
 * @version 1
 * @req REQ-MEM-001
 */
void lv_mem_init(void)
{
    ESP_LOGI(TAG, "LVGL memory using ESP-IDF heap_caps with SPIRAM preference");
    ESP_LOGI(TAG, "All allocations aligned to 64 bytes for PPA compatibility");
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
