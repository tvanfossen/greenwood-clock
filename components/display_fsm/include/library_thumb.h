/**
 * @file library_thumb.h
 * @version 1
 * @brief A Library thumbnail as an LVGL image (an ARGB8888 draw buffer): decoded from its PNG
 *        (the clock reads it from the card, the phone fetches it) or painted, freed with
 *        library_thumb_free. Shared by the clock and the phone panel.
 */
#pragma once

#include "lvgl.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A new thumbnail @p w x @p h to paint into (ARGB8888; its stride is header.stride).
 * @version 1
 * @req REQ-LIB-001
 * @return It, or NULL.
 */
lv_image_dsc_t *library_thumb_new(uint32_t w, uint32_t h);

/**
 * @brief Decode @p png (@p len bytes) into a new image.
 * @version 1
 * @req REQ-LIB-001
 * @return It, or NULL (not a PNG, too large, out of memory).
 */
lv_image_dsc_t *library_thumb_decode(const uint8_t *png, size_t len);

/**
 * @brief Free a thumbnail (NULL is fine).
 * @version 1
 * @req REQ-LIB-001
 */
void library_thumb_free(lv_image_dsc_t *thumb);

#ifdef __cplusplus
}
#endif
