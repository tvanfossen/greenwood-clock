// components/display_fsm/src/image_decode.c
//
// Shared PNG → persistent SPIRAM ARGB8888 decoder. See image_decode.h.
//
// This translation unit owns the single STB_IMAGE_IMPLEMENTATION for the whole
// firmware; other files include only the public header above.

#include "image_decode.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_cache.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG           // Only compile the PNG decoder
#define STBI_NO_STDIO           // Decode from memory only
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#include "stb_image.h"

static const char *TAG = "image_decode";

// Reject absurd dimensions — also bounds the w*h*4 size math well under UINT32.
#define IMG_MAX_W 2048
#define IMG_MAX_H 1200

// Decode a PNG buffer to RGBA via stb_image (lodepng produces corrupt output on
// RISC-V/ESP32-P4). Validates dimensions. Returns raw pixels or NULL.
static unsigned char *decode_rgba(const uint8_t *png, size_t len, int *w, int *h)
{
    int channels = 0;
    unsigned char *raw = stbi_load_from_memory(png, (int)len, w, h, &channels, 4);
    if (!raw) {
        ESP_LOGE(TAG, "stb decode failed: %s (len=%zu)", stbi_failure_reason(), len);
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
static lv_image_dsc_t *build_dsc_from_raw(const unsigned char *raw, int w, int h)
{
    uint32_t stride = lv_draw_buf_width_to_stride((uint32_t)w, LV_COLOR_FORMAT_ARGB8888);
    uint32_t buf_size = stride * (uint32_t)h;
    uint8_t *pixels = (uint8_t *)heap_caps_aligned_alloc(64, buf_size,
                                                         MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!pixels) {
        ESP_LOGE(TAG, "SPIRAM pixel alloc failed (%lu B)", (unsigned long)buf_size);
        return NULL;
    }
    memset(pixels, 0, buf_size);
    swizzle_rgba_to_bgra(pixels, stride, raw, w, h);
    esp_cache_msync(pixels, buf_size,
                    ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_TYPE_DATA);

    lv_image_dsc_t *dsc = make_dsc(pixels, w, h, stride, buf_size, LV_COLOR_FORMAT_ARGB8888);
    if (!dsc) heap_caps_free(pixels);
    return dsc;
}

// RGBA -> RGB565 pack into a stride-aligned buffer (drops alpha; opaque images).
static void pack_rgba_to_rgb565(uint8_t *dst, uint32_t dstride,
                                const unsigned char *raw, int w, int h)
{
    uint32_t src_stride = (uint32_t)w * 4;
    for (int y = 0; y < h; y++) {
        uint16_t *drow = (uint16_t *)(dst + (uint32_t)y * dstride);
        const uint8_t *srow = raw + (uint32_t)y * src_stride;
        for (int x = 0; x < w; x++) {
            uint8_t r = srow[x * 4 + 0], g = srow[x * 4 + 1], b = srow[x * 4 + 2];
            drow[x] = (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
        }
    }
}

// Build a persistent SPIRAM RGB565 descriptor (opaque images only). RGB565
// matches the framebuffer, so redraws during animation are straight copies with
// no per-pixel conversion — far cheaper than ARGB8888 for a full-screen image.
static lv_image_dsc_t *build_dsc_rgb565(const unsigned char *raw, int w, int h)
{
    uint32_t stride = lv_draw_buf_width_to_stride((uint32_t)w, LV_COLOR_FORMAT_RGB565);
    uint32_t buf_size = stride * (uint32_t)h;
    uint8_t *pixels = (uint8_t *)heap_caps_aligned_alloc(64, buf_size,
                                                         MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!pixels) {
        ESP_LOGE(TAG, "SPIRAM RGB565 alloc failed (%lu B)", (unsigned long)buf_size);
        return NULL;
    }
    memset(pixels, 0, buf_size);
    pack_rgba_to_rgb565(pixels, stride, raw, w, h);
    esp_cache_msync(pixels, buf_size,
                    ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_TYPE_DATA);

    lv_image_dsc_t *dsc = make_dsc(pixels, w, h, stride, buf_size, LV_COLOR_FORMAT_RGB565);
    if (!dsc) heap_caps_free(pixels);
    return dsc;
}

lv_image_dsc_t *image_decode_png_buf(const uint8_t *png, size_t len)
{
    if (!png || len == 0) return NULL;

    int w = 0, h = 0;
    unsigned char *raw = decode_rgba(png, len, &w, &h);
    if (!raw) return NULL;

    lv_image_dsc_t *dsc = build_dsc_from_raw(raw, w, h);
    stbi_image_free(raw);
    if (dsc) {
        ESP_LOGI(TAG, "Decoded %dx%d (%lu B) from %zu-byte PNG", w, h,
                 (unsigned long)dsc->data_size, len);
    }
    return dsc;
}

// ftell-based file size; leaves the stream rewound to the start.
static long file_size(FILE *f)
{
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    return n;
}

// Read a whole file into a fresh SPIRAM buffer. Returns buffer + length, or NULL.
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

lv_image_dsc_t *image_decode_png_file(const char *path)
{
    if (!path) return NULL;

    size_t len = 0;
    uint8_t *png = read_file_spiram(path, &len);
    if (!png) return NULL;

    lv_image_dsc_t *dsc = image_decode_png_buf(png, len);
    heap_caps_free(png);
    return dsc;
}

lv_image_dsc_t *image_decode_png_file_rgb565(const char *path)
{
    if (!path) return NULL;

    size_t len = 0;
    uint8_t *png = read_file_spiram(path, &len);
    if (!png) return NULL;

    int w = 0, h = 0;
    unsigned char *raw = decode_rgba(png, len, &w, &h);
    heap_caps_free(png);
    if (!raw) return NULL;

    lv_image_dsc_t *dsc = build_dsc_rgb565(raw, w, h);
    stbi_image_free(raw);
    if (dsc) {
        ESP_LOGI(TAG, "Decoded %dx%d RGB565 (%lu B) from %s", w, h,
                 (unsigned long)dsc->data_size, path);
    }
    return dsc;
}

void image_decode_flip_vertical(lv_image_dsc_t *dsc)
{
    if (!dsc || !dsc->data) return;

    uint32_t stride = dsc->header.stride;
    uint32_t h      = dsc->header.h;
    uint8_t *data   = (uint8_t *)dsc->data;

    uint8_t *tmp = (uint8_t *)heap_caps_malloc(stride, MALLOC_CAP_8BIT);
    if (!tmp) {
        ESP_LOGE(TAG, "flip: row buffer alloc failed (%lu B)", (unsigned long)stride);
        return;
    }
    for (uint32_t y = 0; y < h / 2; y++) {
        uint8_t *top = data + (size_t)y * stride;
        uint8_t *bot = data + (size_t)(h - 1 - y) * stride;
        memcpy(tmp, top, stride);
        memcpy(top, bot, stride);
        memcpy(bot, tmp, stride);
    }
    heap_caps_free(tmp);

    esp_cache_msync(data, (size_t)stride * h,
                    ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_TYPE_DATA);
}

// Raw RGB565 cache file format: [u16 w][u16 h][u32 stride][RGB565 pixel data].
// Lets a one-time ~2s PNG decode be replaced by a fast fread on later loads.
#define R565_MAGIC_HDR 8

lv_image_dsc_t *image_decode_load_raw_rgb565(const char *path)
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
    uint8_t *pixels = (uint8_t *)heap_caps_aligned_alloc(64, buf_size,
                                                         MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!pixels) { fclose(f); return NULL; }

    if (fread(pixels, 1, buf_size, f) != buf_size) {
        heap_caps_free(pixels);
        fclose(f);
        ESP_LOGW(TAG, "r565 short read: %s", path);
        return NULL;
    }
    fclose(f);
    esp_cache_msync(pixels, buf_size,
                    ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_TYPE_DATA);

    lv_image_dsc_t *dsc = make_dsc(pixels, w, h, stride, buf_size, LV_COLOR_FORMAT_RGB565);
    if (!dsc) heap_caps_free(pixels);
    return dsc;
}

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

void image_decode_free(lv_image_dsc_t *dsc)
{
    if (!dsc) return;
    if (dsc->data) heap_caps_free((void *)dsc->data);
    heap_caps_free(dsc);
}
