// components/sdcard/sdcard.h

#ifndef SDCARD_H
#define SDCARD_H

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Mount point for SD card
#define SDCARD_MOUNT_POINT "/sdcard"

/**
 * @brief SD card status
 */
typedef enum {
    SDCARD_STATUS_NOT_PRESENT,
    SDCARD_STATUS_MOUNTING,
    SDCARD_STATUS_MOUNTED,
    SDCARD_STATUS_MOUNT_FAILED,
    SDCARD_STATUS_UNMOUNTED,
    SDCARD_STATUS_ERROR
} sdcard_status_t;

/**
 * @brief SD card information
 */
typedef struct {
    sdcard_status_t status;
    uint64_t total_bytes;
    uint64_t used_bytes;
    uint32_t sector_size;
    char card_name[64];
    char card_type[16];  // "SD", "SDHC", "SDXC"
} sdcard_info_t;

/**
 * @brief Initialize SD card subsystem
 * @return ESP_OK on success
 */
esp_err_t sdcard_init(void);

/**
 * @brief Mount SD card
 * @param format_if_failed Format the card if mount fails
 * @return ESP_OK on success
 */
esp_err_t sdcard_mount(bool format_if_failed);

/**
 * @brief Unmount SD card (safe removal)
 * @return ESP_OK on success
 */
esp_err_t sdcard_unmount(void);

/**
 * @brief Get SD card status and information
 * @param info Pointer to info structure
 * @return ESP_OK on success
 */
esp_err_t sdcard_get_info(sdcard_info_t* info);

/**
 * @brief Check if SD card is mounted
 * @return true if mounted
 */
bool sdcard_is_mounted(void);

/**
 * @brief Format SD card (WARNING: erases all data)
 * @return ESP_OK on success
 */
esp_err_t sdcard_format(void);

/**
 * @brief Create standard directories on SD card
 * @return ESP_OK on success
 */
esp_err_t sdcard_create_directories(void);

#ifdef __cplusplus
}
#endif

#endif // SDCARD_H
