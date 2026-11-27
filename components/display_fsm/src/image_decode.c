/**
 * @file image_decode.c
 * v2: one big buffer a decode, not two: stb allocates 64-byte-aligned PSRAM and its output
 *     becomes the image (converted in place); full-screen RGB565 pictures go into slots
 *     reserved once (image_pool); the backgrounds share the album's .r565 cache, checked
 *     against its PNG. Seen on device: 13 MB free, no 2.5 MB block, a background left
 *     unchanged.
 * @version 2
 * @brief Shared PNG decoder (stb_image) into persistent SPIRAM ARGB8888/RGB565 LVGL descriptors, plus a raw RGB565 cache format.
 */
// Shared PNG → persistent SPIRAM ARGB8888 decoder. See image_decode.h.
//
// This translation unit owns the single STB_IMAGE_IMPLEMENTATION for the whole
// firmware; other files include only the public header above.

#include "image_decode.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_cache.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "image_pool.h"
#include <sys/stat.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static void *stb_alloc(size_t n);
static void *stb_realloc_sized(void *p, size_t old_n, size_t n);
static void stb_free(void *p);

#define STB_IMAGE_IMPLEMENTATION
#define STBI_MALLOC(sz)                 stb_alloc(sz)   // 64-byte aligned PSRAM: the output is the image
#define STBI_REALLOC_SIZED(p, old, sz)  stb_realloc_sized(p, old, sz)
#define STBI_FREE(p)                    stb_free(p)
#define STBI_ONLY_PNG           // Only compile the PNG decoder
#define STBI_NO_STDIO           // Decode from memory only
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#include "stb_image.h"

static const char *TAG = "image_decode";

static SemaphoreHandle_t s_decode_mutex;   // decode_lock

// Full-screen RGB565 slots (background current + incoming, album shown + next): a 1088 px
// wide stride (a 1025 px background is padded to 1056) by 600 rows.
#define SLOT_BYTES ((size_t)2176 * 600)
static image_pool_t s_pool;
static portMUX_TYPE s_pool_mux = portMUX_INITIALIZER_UNLOCKED;

/**
 * @brief stb's allocations: 64-byte-aligned PSRAM, so its output can be the image itself.
 * @version 1
 * @utility
 * @return The block, or NULL.
 */
static void *stb_alloc(size_t n)
{
    return heap_caps_aligned_alloc(64, n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

/**
 * @brief stb growing a buffer: a new aligned block with the old one's bytes.
 * @version 1
 * @utility
 * @return The new block (the old one freed), or NULL (the old one kept, as realloc does).
 */
static void *stb_realloc_sized(void *p, size_t old_n, size_t n)
{
    void *q = stb_alloc(n);
    if (q && p) memcpy(q, p, old_n < n ? old_n : n);
    if (q) heap_caps_free(p);
    return q;
}

/**
 * @brief stb freeing.
 * @version 1
 * @utility
 */
static void stb_free(void *p)
{
    heap_caps_free(p);
}

/**
 * @brief A buffer of @p size for a full-screen RGB565 picture: a reserved slot when @p pooled
 *        and one is free, else the heap.
 * @version 1
 * @utility
 * @return It, or NULL.
 */
static uint8_t *rgb565_buffer(size_t size, bool pooled)
{
    portENTER_CRITICAL(&s_pool_mux);
    uint8_t *b = pooled ? image_pool_take(&s_pool, size) : NULL;
    portEXIT_CRITICAL(&s_pool_mux);
    if (pooled && !b && s_pool.n) ESP_LOGW(TAG, "no free picture slot: %u B from the heap", (unsigned)size);
    return b ? b : heap_caps_aligned_alloc(64, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

/**
 * @brief Free a picture's pixels: its slot back, or the heap.
 * @version 1
 * @utility
 */
static void pixels_free(void *px)
{
    portENTER_CRITICAL(&s_pool_mux);
    bool slot = image_pool_give(&s_pool, px);
    portEXIT_CRITICAL(&s_pool_mux);
    if (!slot) heap_caps_free(px);
}

/**
 * @brief Reserve the full-screen slots (once).
 * @version 1
 * @req REQ-BG-001
 */
void image_decode_pool_init(void)
{
    uint8_t *bufs[IMAGE_POOL_SLOTS];
    int n = 0;
    while (s_pool.n == 0 && n < IMAGE_POOL_SLOTS &&
           (bufs[n] = heap_caps_aligned_alloc(64, SLOT_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)) != NULL) {
        n++;
    }
    if (s_pool.n == 0) image_pool_setup(&s_pool, bufs, n, SLOT_BYTES);
    ESP_LOGI(TAG, "picture slots: %d of %d reserved (%u B each); SPIRAM free %u, largest block %u",
             s_pool.n, IMAGE_POOL_SLOTS, (unsigned)SLOT_BYTES,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));
}


// Reject absurd dimensions — also bounds the w*h*4 size math well under UINT32.
#define IMG_MAX_W 2048
#define IMG_MAX_H 1200

// Decode a PNG buffer to RGBA via stb_image (lodepng produces corrupt output on
// RISC-V/ESP32-P4). Validates dimensions. Returns raw pixels or NULL.
/**
 * @brief Take the decode lock: one full-picture decode at a time across tasks (a background,
 *        the album, a thumbnail each need several MB of contiguous PSRAM at the peak).
 *        Recursive: the file decoders call the buffer one.
 * @version 1
 * @utility
 */
static void decode_lock(void)
{
    static StaticSemaphore_t s_buf;
    static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
    portENTER_CRITICAL(&s_mux);
    if (!s_decode_mutex) s_decode_mutex = xSemaphoreCreateRecursiveMutexStatic(&s_buf);
    portEXIT_CRITICAL(&s_mux);
    xSemaphoreTakeRecursive(s_decode_mutex, portMAX_DELAY);
}

/**
 * @brief Give the decode lock back.
 * @version 1
 * @utility
 */
static void decode_unlock(void)
{
    xSemaphoreGiveRecursive(s_decode_mutex);
}

/**
 * @brief Decode a PNG buffer to top-down RGBA with stb_image and reject dimensions outside 1..2048 x 1..1200.
 * v2: pin stb's thread-local flip-on-load flag to 0 before decoding (top-down rows always).
 * v3: a failure logs PSRAM's free size and largest block (fragmentation or not).
 * @version 3
 * @req REQ-BG-001
 * @return stb-allocated RGBA pixels (free with stbi_image_free), or NULL on decode failure or bad size.
 */
static unsigned char *decode_rgba(const uint8_t *png, size_t len, int *w, int *h)
{
    int channels = 0;
    // stb_image keeps its flip-on-load flag in two implicitly-initialised thread-local
    // ints; on the old IDF beta they read non-zero in fsm_task, so every decode was
    // silently bottom-up and callers 'flipped' it upright. Pin it explicitly so decode
    // output is always top-down regardless of thread-local memory state.
    stbi_set_flip_vertically_on_load_thread(0);
    unsigned char *raw = stbi_load_from_memory(png, (int)len, w, h, &channels, 4);
    if (!raw) {
        ESP_LOGE(TAG, "stb decode failed: %s (len=%zu; SPIRAM free=%u largest block=%u)",
                 stbi_failure_reason(), len,
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));
        return NULL;
    }
    if (*w <= 0 || *h <= 0 || *w > IMG_MAX_W || *h > IMG_MAX_H) {
        ESP_LOGE(TAG, "PNG rejected: %dx%d (channels=%d)", *w, *h, channels);
        stbi_image_free(raw);
        return NULL;
    }
    return raw;
}

// RGBA → BGRA swizzle into a stride-aligned buffer (LVGL ARGB8888 is BGRA in
// little-endian memory).
/**
 * @brief Swizzle RGBA pixels into a stride-aligned BGRA (LVGL ARGB8888) buffer.
 * @version 1
 * @utility
 */
static void swizzle_rgba_to_bgra(uint8_t *dst, uint32_t dstride,
                                 const unsigned char *raw, int w, int h)
{
    uint32_t src_stride = (uint32_t)w * 4;
    for (int y = 0; y < h; y++) {
        uint8_t *drow = dst + (uint32_t)y * dstride;
        const uint8_t *srow = raw + (uint32_t)y * src_stride;
        for (int x = 0; x < w; x++) {
            drow[x * 4 + 0] = srow[x * 4 + 2]; // B
            drow[x * 4 + 1] = srow[x * 4 + 1]; // G
            drow[x * 4 + 2] = srow[x * 4 + 0]; // R
            drow[x * 4 + 3] = srow[x * 4 + 3]; // A
        }
    }
}

// Allocate + fill an image descriptor pointing at a ready pixel buffer.
/**
 * @brief Allocate a SPIRAM image descriptor pointing at a ready pixel buffer.
 * @version 1
 * @utility
 * @return New descriptor, or NULL if allocation fails.
 */
static lv_image_dsc_t *make_dsc(uint8_t *pixels, int w, int h,
                                uint32_t stride, uint32_t size, lv_color_format_t cf)
{
    lv_image_dsc_t *dsc = (lv_image_dsc_t *)heap_caps_malloc(
            sizeof(lv_image_dsc_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!dsc) {
        ESP_LOGE(TAG, "SPIRAM descriptor alloc failed");
        return NULL;
    }
    memset(dsc, 0, sizeof(*dsc));
    dsc->header.magic  = LV_IMAGE_HEADER_MAGIC;
    dsc->header.cf     = cf;
    dsc->header.w      = (uint32_t)w;
    dsc->header.h      = (uint32_t)h;
    dsc->header.stride = stride;
    dsc->data_size     = size;
    dsc->data          = pixels;
    return dsc;
}

// Build a persistent SPIRAM ARGB8888 descriptor from decoded RGBA pixels.
/**
 * @brief Build a persistent 64-byte-aligned, cache-synced SPIRAM ARGB8888 descriptor from RGBA
 *        pixels, taking @p raw (stb's, freed or kept as the image).
 * v2: in place when the rows need no padding (one buffer, not two); takes @p raw.
 * @version 2
 * @utility
 * @return New descriptor, or NULL on allocation failure.
 */
static lv_image_dsc_t *build_dsc_from_raw(unsigned char *raw, int w, int h)
{
    uint32_t stride = lv_draw_buf_width_to_stride((uint32_t)w, LV_COLOR_FORMAT_ARGB8888);
    uint32_t buf_size = stride * (uint32_t)h;
    bool in_place = stride == (uint32_t)w * 4;
    uint8_t *pixels = in_place ? raw
                               : (uint8_t *)heap_caps_aligned_alloc(64, buf_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!pixels) {
        ESP_LOGE(TAG, "SPIRAM pixel alloc failed (%lu B)", (unsigned long)buf_size);
        stbi_image_free(raw);
        return NULL;
    }
    if (in_place) {
        image_px_swap_rb(pixels, (size_t)w * (size_t)h);
    } else {
        memset(pixels, 0, buf_size);
        swizzle_rgba_to_bgra(pixels, stride, raw, w, h);
        stbi_image_free(raw);
    }
    esp_cache_msync(pixels, buf_size,
                    ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_TYPE_DATA);

    lv_image_dsc_t *dsc = make_dsc(pixels, w, h, stride, buf_size, LV_COLOR_FORMAT_ARGB8888);
    if (!dsc) heap_caps_free(pixels);
    return dsc;
}

// Build a persistent SPIRAM RGB565 descriptor (opaque images only). RGB565
// matches the framebuffer, so redraws during animation are straight copies with
// no per-pixel conversion — far cheaper than ARGB8888 for a full-screen image.
/**
 * @brief Build a persistent 64-byte-aligned, cache-synced SPIRAM RGB565 descriptor from RGBA
 *        pixels (opaque images), taking @p raw: packed in place, then copied into a reserved
 *        slot (@p pooled) or a heap block of the RGB565 size.
 * v2: packed in place; a slot when @p pooled; takes @p raw.
 * @version 2
 * @utility
 * @return New descriptor, or NULL on allocation failure.
 */
static lv_image_dsc_t *build_dsc_rgb565(unsigned char *raw, int w, int h, bool pooled)
{
    uint32_t stride = lv_draw_buf_width_to_stride((uint32_t)w, LV_COLOR_FORMAT_RGB565);
    uint32_t buf_size = stride * (uint32_t)h;
    bool fits = stride <= (uint32_t)w * 4;   // packing in place needs the 565 row inside the RGBA one
    if (fits) image_px_rgba_to_rgb565_inplace(raw, w, h, stride);
    uint8_t *pixels = fits ? rgb565_buffer(buf_size, pooled) : NULL;
    if (!fits) ESP_LOGE(TAG, "RGB565: %d px wide is too narrow to pack in place", w);
    if (pixels) memcpy(pixels, raw, buf_size);
    stbi_image_free(raw);
    if (!pixels) {
        ESP_LOGE(TAG, "SPIRAM RGB565 alloc failed (%lu B)", (unsigned long)buf_size);
        return NULL;
    }
    esp_cache_msync(pixels, buf_size,
                    ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_TYPE_DATA);

    lv_image_dsc_t *dsc = make_dsc(pixels, w, h, stride, buf_size, LV_COLOR_FORMAT_RGB565);
    if (!dsc) pixels_free(pixels);
    return dsc;
}

/**
 * @brief image_decode_png_buf without the decode lock.
 * v2: the descriptor takes stb's buffer.
 * @version 2
 * @utility
 * @return The descriptor, or NULL.
 */
static lv_image_dsc_t *image_decode_png_buf_unlocked(const uint8_t *png, size_t len)
{
    if (!png || len == 0) return NULL;

    int w = 0, h = 0;
    unsigned char *raw = decode_rgba(png, len, &w, &h);
    if (!raw) return NULL;

    lv_image_dsc_t *dsc = build_dsc_from_raw(raw, w, h);   // takes raw
    if (dsc) {
        ESP_LOGI(TAG, "Decoded %dx%d (%lu B) from %zu-byte PNG", w, h,
                 (unsigned long)dsc->data_size, len);
    }
    return dsc;
}

/**
 * @brief Decode an in-memory PNG into a persistent SPIRAM ARGB8888 descriptor.
 * v2: one full decode at a time (decode_lock): two at once doubled the multi-MB peak.
 * @version 2
 * @req REQ-RADAR-001
 * @req REQ-BG-001
 * @return New descriptor (free with image_decode_free), or NULL on bad input or failure.
 */
lv_image_dsc_t *image_decode_png_buf(const uint8_t *png, size_t len)
{
    decode_lock();
    lv_image_dsc_t *dsc = image_decode_png_buf_unlocked(png, len);
    decode_unlock();
    return dsc;
}

// ftell-based file size; leaves the stream rewound to the start.
/**
 * @brief Get a file's size via ftell and rewind it to the start.
 * @version 1
 * @utility
 * @return File size in bytes, or a negative value on error.
 */
static long file_size(FILE *f)
{
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    return n;
}

// Read a whole file into a fresh SPIRAM buffer. Returns buffer + length, or NULL.
/**
 * @brief Read a whole file into a fresh SPIRAM buffer.
 * @version 1
 * @utility
 * @return Buffer (caller frees with heap_caps_free) with *len_out set, or NULL on open/alloc/read failure.
 */
static uint8_t *read_file_spiram(const char *path, size_t *len_out)
{
    FILE *f = fopen(path, "rb");
    if (!f) { ESP_LOGE(TAG, "open failed: %s", path); return NULL; }

    long fsize = file_size(f);
    uint8_t *buf = (fsize > 0)
        ? (uint8_t *)heap_caps_malloc((size_t)fsize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
        : NULL;
    if (buf && fread(buf, 1, (size_t)fsize, f) == (size_t)fsize) {
        *len_out = (size_t)fsize;
    } else {
        heap_caps_free(buf);   // free(NULL) is safe
        buf = NULL;
        ESP_LOGE(TAG, "read failed (size=%ld): %s", fsize, path);
    }
    fclose(f);
    return buf;
}

/**
 * @brief Read a PNG file and decode it into a persistent SPIRAM ARGB8888 descriptor.
 * v2: under the decode lock, the file read included.
 * @version 2
 * @req REQ-BG-001
 * @return New descriptor, or NULL on failure.
 */
lv_image_dsc_t *image_decode_png_file(const char *path)
{
    if (!path) return NULL;

    decode_lock();   // the file buffer is part of the peak too
    size_t len = 0;
    uint8_t *png = read_file_spiram(path, &len);
    lv_image_dsc_t *dsc = png ? image_decode_png_buf_unlocked(png, len) : NULL;
    heap_caps_free(png);
    decode_unlock();
    return dsc;
}

/**
 * @brief image_decode_png_file_rgb565 without the decode lock; into a slot when @p pooled.
 * v2: into a slot when @p pooled; the descriptor takes stb's buffer.
 * @version 2
 * @utility
 * @return The descriptor, or NULL.
 */
static lv_image_dsc_t *image_decode_png_file_rgb565_unlocked(const char *path, bool pooled)
{
    if (!path) return NULL;

    size_t len = 0;
    uint8_t *png = read_file_spiram(path, &len);
    if (!png) return NULL;

    int w = 0, h = 0;
    unsigned char *raw = decode_rgba(png, len, &w, &h);
    heap_caps_free(png);
    if (!raw) return NULL;

    lv_image_dsc_t *dsc = build_dsc_rgb565(raw, w, h, pooled);   // takes raw
    if (dsc) {
        ESP_LOGI(TAG, "Decoded %dx%d RGB565 (%lu B) from %s", w, h,
                 (unsigned long)dsc->data_size, path);
    }
    return dsc;
}

/**
 * @brief Read a PNG file and decode it into a persistent SPIRAM RGB565 descriptor (opaque, framebuffer format).
 * v2: one full decode at a time (decode_lock): two at once doubled the multi-MB peak.
 * v3: into the heap, not a slot (the slots are image_decode_cached_rgb565's).
 * @version 3
 * @req REQ-BG-001
 * @req REQ-PHOTO-001
 * @return New descriptor, or NULL on failure.
 */
lv_image_dsc_t *image_decode_png_file_rgb565(const char *path)
{
    decode_lock();
    lv_image_dsc_t *dsc = image_decode_png_file_rgb565_unlocked(path, false);
    decode_unlock();
    return dsc;
}

// Raw RGB565 cache file format: [u16 w][u16 h][u32 stride][RGB565 pixel data].
// Lets a one-time ~2s PNG decode be replaced by a fast fread on later loads.
#define R565_MAGIC_HDR 8

/**
 * @brief Load a raw RGB565 cache file ([u16 w][u16 h][u32 stride][pixels]) into a persistent SPIRAM descriptor.
 * v2: into a slot when @p pooled (was image_decode_load_raw_rgb565).
 * @version 2
 * @req REQ-PHOTO-001
 * @return New descriptor, or NULL if missing, malformed, oversized or short.
 */
static lv_image_dsc_t *load_raw(const char *path, bool pooled)
{
    if (!path) return NULL;
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;

    uint16_t w = 0, h = 0;
    uint32_t stride = 0;
    if (fread(&w, sizeof(w), 1, f) != 1 || fread(&h, sizeof(h), 1, f) != 1 ||
        fread(&stride, sizeof(stride), 1, f) != 1 ||
        w == 0 || h == 0 || w > IMG_MAX_W || h > IMG_MAX_H ||
        stride < (uint32_t)w * 2) {
        fclose(f);
        return NULL;
    }
    uint32_t buf_size = stride * h;
    uint8_t *pixels = rgb565_buffer(buf_size, pooled);
    if (!pixels) { fclose(f); return NULL; }

    if (fread(pixels, 1, buf_size, f) != buf_size) {
        pixels_free(pixels);
        fclose(f);
        ESP_LOGW(TAG, "r565 short read: %s", path);
        return NULL;
    }
    fclose(f);
    esp_cache_msync(pixels, buf_size,
                    ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_TYPE_DATA);

    lv_image_dsc_t *dsc = make_dsc(pixels, w, h, stride, buf_size, LV_COLOR_FORMAT_RGB565);
    if (!dsc) pixels_free(pixels);
    return dsc;
}

/**
 * @brief Load a raw RGB565 cache file into a heap descriptor.
 * v2: through load_raw (a slot when the caller asks: image_decode_cached_rgb565).
 * @version 2
 * @req REQ-PHOTO-001
 * @return New descriptor, or NULL if missing, malformed, oversized or short.
 */
lv_image_dsc_t *image_decode_load_raw_rgb565(const char *path)
{
    return load_raw(path, false);
}

/**
 * @brief The cache file of PNG @p png ("<png>.r565") into @p out.
 * @version 1
 * @utility
 */
static void cache_path(const char *png, char *out, size_t n)
{
    snprintf(out, n, "%s.r565", png);
}

/**
 * @brief Whether @p cache is missing or older than @p png (a picture replaced under its name).
 * @version 1
 * @utility
 * @return Whether it must be made again.
 */
static bool cache_stale(const char *png, const char *cache)
{
    struct stat p, c;
    return stat(cache, &c) != 0 || (stat(png, &p) == 0 && c.st_mtime < p.st_mtime);
}

/**
 * @brief A full-screen opaque picture for showing: its .r565 cache read into a reserved slot,
 *        or (no cache, or older than the PNG) decoded into one and the cache written.
 * v2: the cache write under the decode lock.
 * @version 2
 * @req REQ-BG-001
 * @req REQ-PHOTO-001
 * @return New descriptor (image_decode_free gives its slot back), or NULL.
 */
lv_image_dsc_t *image_decode_cached_rgb565(const char *png)
{
    char cache[192];
    cache_path(png, cache, sizeof(cache));
    lv_image_dsc_t *dsc = cache_stale(png, cache) ? NULL : load_raw(cache, true);
    if (!dsc) {
        decode_lock();   // the cache write too: two writers shared its .tmp
        dsc = image_decode_png_file_rgb565_unlocked(png, true);
        if (dsc) image_decode_save_raw_rgb565(cache, dsc);
        decode_unlock();
    }
    return dsc;
}

/**
 * @brief Make PNG @p png's .r565 cache when missing or stale (the Library's background task:
 *        heap, not a slot, freed at once).
 * v2: the cache write under the decode lock, staleness checked again inside it.
 * @version 2
 * @req REQ-LIB-001
 * @return Whether a fresh cache exists now.
 */
bool image_decode_make_cache_rgb565(const char *png)
{
    char cache[192];
    cache_path(png, cache, sizeof(cache));
    bool ok = !cache_stale(png, cache);
    if (!ok) {
        decode_lock();   // the cache write too: two writers shared its .tmp
        lv_image_dsc_t *dsc = cache_stale(png, cache) ? image_decode_png_file_rgb565_unlocked(png, false) : NULL;
        ok = dsc ? image_decode_save_raw_rgb565(cache, dsc) : !cache_stale(png, cache);
        decode_unlock();
        image_decode_free(dsc);
    }
    return ok;
}

/**
 * @brief Remove PNG @p png's .r565 cache (the picture was deleted or renamed).
 * @version 1
 * @req REQ-LIB-001
 */
void image_decode_drop_cache(const char *png)
{
    char cache[192];
    cache_path(png, cache, sizeof(cache));
    remove(cache);
}

/**
 * @brief Write an RGB565 descriptor to a raw cache file via a temp file and rename.
 * @version 1
 * @req REQ-PHOTO-001
 * @return true on success, false on bad input or write/rename failure.
 */
bool image_decode_save_raw_rgb565(const char *path, const lv_image_dsc_t *dsc)
{
    if (!path || !dsc || !dsc->data || dsc->header.cf != LV_COLOR_FORMAT_RGB565) return false;

    // Write to a temp file then rename, so an interrupted write never leaves a
    // half-written cache file that would later load as garbage.
    char tmp[160];
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    FILE *f = fopen(tmp, "wb");
    if (!f) { ESP_LOGW(TAG, "r565 cache create failed: %s", tmp); return false; }

    uint16_t w = (uint16_t)dsc->header.w, h = (uint16_t)dsc->header.h;
    uint32_t stride = dsc->header.stride;
    bool ok = fwrite(&w, sizeof(w), 1, f) == 1 &&
              fwrite(&h, sizeof(h), 1, f) == 1 &&
              fwrite(&stride, sizeof(stride), 1, f) == 1 &&
              fwrite(dsc->data, 1, dsc->data_size, f) == dsc->data_size;
    fclose(f);
    if (!ok || rename(tmp, path) != 0) {
        remove(tmp);
        ESP_LOGW(TAG, "r565 cache write failed: %s", path);
        return false;
    }
    ESP_LOGI(TAG, "r565 cache written: %s (%lu B)", path, (unsigned long)dsc->data_size);
    return true;
}

/**
 * @brief Average one 2x2 block of ARGB8888 pixels at @p p (row pitch @p stride bytes) into @p out.
 * @version 1
 * @utility
 */
static void avg_2x2(const uint8_t *p, uint32_t stride, uint8_t *out)
{
    for (int c = 0; c < 4; c++) {
        out[c] = (uint8_t)((p[c] + p[4 + c] + p[stride + c] + p[stride + 4 + c] + 2) / 4);
    }
}

/**
 * @brief Make a half-resolution copy of an ARGB8888 descriptor (2x2 box filter), e.g. to
 *        hold several full-screen overlays at a quarter of the memory.
 * @version 1
 * @req REQ-RADAR-003
 * @return New descriptor (free with image_decode_free), or NULL on bad input or OOM.
 */
lv_image_dsc_t *image_decode_halve_argb(const lv_image_dsc_t *src)
{
    if (!src || src->header.cf != LV_COLOR_FORMAT_ARGB8888) return NULL;
    int w = src->header.w / 2, h = src->header.h / 2;
    uint32_t stride = lv_draw_buf_width_to_stride((uint32_t)w, LV_COLOR_FORMAT_ARGB8888);
    uint32_t size = stride * (uint32_t)h;
    uint8_t *pixels = (uint8_t *)heap_caps_aligned_alloc(64, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!pixels) {
        ESP_LOGE(TAG, "halve: SPIRAM alloc failed (%lu B)", (unsigned long)size);
        return NULL;
    }
    uint32_t sstride = src->header.stride;
    for (int y = 0; y < h; y++) {
        const uint8_t *row = src->data + (size_t)(2 * y) * sstride;
        for (int x = 0; x < w; x++) avg_2x2(row + 8 * x, sstride, pixels + y * stride + 4 * x);
    }
    esp_cache_msync(pixels, size, ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_TYPE_DATA);
    lv_image_dsc_t *dsc = make_dsc(pixels, w, h, stride, size, LV_COLOR_FORMAT_ARGB8888);
    if (!dsc) heap_caps_free(pixels);
    return dsc;
}

/**
 * @brief Free a descriptor and its pixel buffer (no-op if NULL).
 * v2: a reserved slot is given back, not freed.
 * @version 2
 * @req REQ-BG-001
 * @req REQ-PHOTO-001
 * @req REQ-RADAR-001
 */
void image_decode_free(lv_image_dsc_t *dsc)
{
    if (!dsc) return;
    if (dsc->data) pixels_free((void *)dsc->data);   // a slot goes back
    heap_caps_free(dsc);
}
