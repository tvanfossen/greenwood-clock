// components/sdcard/sdcard.c

#include "sdcard.h"
#include "esp_log.h"
#include "bsp/esp32_p4_function_ev_board.h"
#include "sdmmc_cmd.h"
#include <string.h>
#include <sys/stat.h>

static const char* TAG = "sdcard";

static sdcard_status_t s_status = SDCARD_STATUS_NOT_PRESENT;

esp_err_t sdcard_init(void) {
    ESP_LOGI(TAG, "Initializing SD card subsystem");
    s_status = SDCARD_STATUS_NOT_PRESENT;
    return ESP_OK;
}

esp_err_t sdcard_mount(bool format_if_failed) {
    if (s_status == SDCARD_STATUS_MOUNTED) {
        ESP_LOGW(TAG, "SD card already mounted");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Mounting SD card using BSP...");
    s_status = SDCARD_STATUS_MOUNTING;

    // Use the BSP's SD card mount function
    esp_err_t ret = bsp_sdcard_mount();

    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount filesystem.");
            if (format_if_failed) {
                ESP_LOGW(TAG, "Formatting not yet implemented via BSP");
            } else {
                ESP_LOGE(TAG, "Card may need formatting");
            }
        } else {
            ESP_LOGE(TAG, "Failed to initialize SD card: %s", esp_err_to_name(ret));
        }
        s_status = SDCARD_STATUS_MOUNT_FAILED;
        return ret;
    }

    // Card mounted successfully
    s_status = SDCARD_STATUS_MOUNTED;

    // Print card info
    sdmmc_card_t *card = bsp_sdcard_get_handle();
    if (card) {
        sdmmc_card_print_info(stdout, card);
    }
    ESP_LOGI(TAG, "SD card mounted at %s", SDCARD_MOUNT_POINT);

    return ESP_OK;
}

esp_err_t sdcard_unmount(void) {
    if (s_status != SDCARD_STATUS_MOUNTED) {
        ESP_LOGW(TAG, "SD card not mounted");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Unmounting SD card...");
    esp_err_t ret = bsp_sdcard_unmount();

    if (ret == ESP_OK) {
        s_status = SDCARD_STATUS_UNMOUNTED;
        ESP_LOGI(TAG, "SD card unmounted");
    } else {
        ESP_LOGE(TAG, "Failed to unmount: %s", esp_err_to_name(ret));
        s_status = SDCARD_STATUS_ERROR;
    }

    return ret;
}

bool sdcard_is_mounted(void) {
    return (s_status == SDCARD_STATUS_MOUNTED);
}

esp_err_t sdcard_get_info(sdcard_info_t* info) {
    if (!info) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(info, 0, sizeof(sdcard_info_t));
    info->status = s_status;

    if (s_status != SDCARD_STATUS_MOUNTED) {
        return ESP_ERR_INVALID_STATE;
    }

    sdmmc_card_t *card = bsp_sdcard_get_handle();
    if (!card) {
        return ESP_ERR_INVALID_STATE;
    }

    // Get filesystem info
    FATFS* fs;
    DWORD free_clusters;
    if (f_getfree("0:", &free_clusters, &fs) == FR_OK) {
        uint64_t total_sectors = (fs->n_fatent - 2) * fs->csize;
        uint64_t free_sectors = free_clusters * fs->csize;
        uint32_t sector_size = fs->ssize;

        info->total_bytes = total_sectors * sector_size;
        info->used_bytes = info->total_bytes - (free_sectors * sector_size);
        info->sector_size = sector_size;
    }

    // Get card info
    snprintf(info->card_name, sizeof(info->card_name), "%s", card->cid.name);

    // Determine card type based on capacity
    uint64_t capacity_mb = ((uint64_t)card->csd.capacity * card->csd.sector_size) / (1024 * 1024);
    if (capacity_mb > 32 * 1024) {
        strcpy(info->card_type, "SDXC");
    } else if (capacity_mb > 2 * 1024) {
        strcpy(info->card_type, "SDHC");
    } else {
        strcpy(info->card_type, "SD");
    }

    return ESP_OK;
}

esp_err_t sdcard_format(void) {
    ESP_LOGW(TAG, "Format via BSP not yet implemented");
    ESP_LOGW(TAG, "Please format the SD card using a PC (FAT32)");
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t sdcard_create_directories(void) {
    if (!sdcard_is_mounted()) {
        ESP_LOGW(TAG, "SD card not mounted, cannot create directories");
        return ESP_ERR_INVALID_STATE;
    }

    const char* dirs[] = {
        SDCARD_MOUNT_POINT "/backgrounds",
        SDCARD_MOUNT_POINT "/logs",
        SDCARD_MOUNT_POINT "/settings",
        SDCARD_MOUNT_POINT "/screenshots",
        SDCARD_MOUNT_POINT "/firmware",
    };

    for (int i = 0; i < sizeof(dirs)/sizeof(dirs[0]); i++) {
        struct stat st;
        if (stat(dirs[i], &st) != 0) {
            ESP_LOGI(TAG, "Creating directory: %s", dirs[i]);
            if (mkdir(dirs[i], 0775) != 0) {
                ESP_LOGW(TAG, "Failed to create directory: %s", dirs[i]);
            }
        } else {
            ESP_LOGI(TAG, "Directory exists: %s", dirs[i]);
        }
    }

    return ESP_OK;
}
