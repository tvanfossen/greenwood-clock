// components/sdcard/sdcard.c

#include "sdcard.h"
#include "esp_log.h"
#include "bsp/esp32_p4_function_ev_board.h"
#include "sdmmc_cmd.h"
#include <string.h>
#include <sys/stat.h>

static const char* TAG = "sdcard";

static sdcard_status_t s_status = SDCARD_STATUS_NOT_PRESENT;

// =============================================================================
// Internal helpers
// =============================================================================

/**
 * @brief Return the card-type string (SDXC, SDHC, or SD) for @p card.
 *
 * @param card  Card descriptor.
 * @return Literal string; no heap allocation.
 */
static const char* sdcard_card_type_str(const sdmmc_card_t* card)
{
    uint64_t cap_mb = ((uint64_t)card->csd.capacity * card->csd.sector_size) / (1024 * 1024);
    if (cap_mb > 32 * 1024) return "SDXC";
    if (cap_mb > 2 * 1024)  return "SDHC";
    return "SD";
}

/**
 * @brief Populate the filesystem statistics fields of @p info.
 *
 * No-op if f_getfree() fails (fields remain zero-initialised).
 *
 * @param info  Info struct to fill.
 */
static void sdcard_fill_fs_stats(sdcard_info_t* info)
{
    FATFS* fs;
    DWORD free_clusters;
    if (f_getfree("0:", &free_clusters, &fs) != FR_OK) return;
    uint64_t total_sectors = (fs->n_fatent - 2) * fs->csize;
    uint64_t free_sectors  = free_clusters * fs->csize;
    uint32_t sector_size   = fs->ssize;
    info->total_bytes  = total_sectors * sector_size;
    info->used_bytes   = info->total_bytes - (free_sectors * sector_size);
    info->sector_size  = sector_size;
}

/**
 * @brief Fill card name, card type, and filesystem stats into @p info.
 *
 * @param info  Destination info struct.
 * @param card  Mounted card descriptor.
 */
static void sdcard_populate_info(sdcard_info_t* info, const sdmmc_card_t* card)
{
    sdcard_fill_fs_stats(info);
    snprintf(info->card_name, sizeof(info->card_name), "%s", card->cid.name);
    strcpy(info->card_type, sdcard_card_type_str(card));
}

/**
 * @brief Ensure @p path exists as a directory, creating it if absent.
 *
 * @param path  Absolute directory path to create.
 */
static void sdcard_ensure_dir(const char* path)
{
    struct stat st;
    if (stat(path, &st) == 0) {
        ESP_LOGI(TAG, "Directory exists: %s", path);
        return;
    }
    ESP_LOGI(TAG, "Creating directory: %s", path);
    if (mkdir(path, 0775) != 0) ESP_LOGW(TAG, "Failed to create: %s", path);
}

/**
 * @brief Log an appropriate message for a BSP mount failure.
 *
 * @param ret             Error code from bsp_sdcard_mount().
 * @param format_if_failed  Whether the caller requested formatting on failure.
 */
static void sdcard_log_mount_failure(esp_err_t ret, bool format_if_failed)
{
    if (ret == ESP_FAIL) {
        ESP_LOGE(TAG, "Failed to mount filesystem");
        if (format_if_failed) {
            ESP_LOGW(TAG, "Formatting not yet implemented via BSP");
        } else {
            ESP_LOGE(TAG, "Card may need formatting");
        }
    } else {
        ESP_LOGE(TAG, "Failed to initialize SD card: %d", (int)ret);
    }
}

/**
 * @brief Print the mounted card info via SDMMC and log the mount point.
 */
static void sdcard_print_card_info(void)
{
    sdmmc_card_t* card = bsp_sdcard_get_handle();
    if (card) sdmmc_card_print_info(stdout, card);
    ESP_LOGI(TAG, "SD card mounted at %s", SDCARD_MOUNT_POINT);
}

// =============================================================================
// Public API
// =============================================================================

esp_err_t sdcard_init(void)
{
    ESP_LOGI(TAG, "Initializing SD card subsystem");
    s_status = SDCARD_STATUS_NOT_PRESENT;
    return ESP_OK;
}

/**
 * @brief Mount the SD card via the BSP.
 *
 * @param format_if_failed  If true, a formatting notice is logged on ESP_FAIL
 *                          (actual formatting is not yet implemented via BSP).
 * @return ESP_OK on success; error code on failure.
 */
esp_err_t sdcard_mount(bool format_if_failed)
{
    if (s_status == SDCARD_STATUS_MOUNTED) {
        ESP_LOGW(TAG, "SD card already mounted");
        return ESP_OK;
    }
    ESP_LOGI(TAG, "Mounting SD card using BSP...");
    s_status = SDCARD_STATUS_MOUNTING;
    esp_err_t ret = bsp_sdcard_mount();
    if (ret != ESP_OK) {
        sdcard_log_mount_failure(ret, format_if_failed);
        s_status = SDCARD_STATUS_MOUNT_FAILED;
        return ret;
    }
    s_status = SDCARD_STATUS_MOUNTED;
    sdcard_print_card_info();
    return ESP_OK;
}

esp_err_t sdcard_unmount(void)
{
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

bool sdcard_is_mounted(void)
{
    return (s_status == SDCARD_STATUS_MOUNTED);
}

/**
 * @brief Populate @p info with the current card status and, if mounted,
 *        filesystem statistics and card metadata.
 *
 * @param info  Destination info struct.
 * @return ESP_OK, ESP_ERR_INVALID_ARG if info is NULL, or
 *         ESP_ERR_INVALID_STATE if not mounted / card handle unavailable.
 */
esp_err_t sdcard_get_info(sdcard_info_t* info)
{
    if (!info) return ESP_ERR_INVALID_ARG;
    memset(info, 0, sizeof(sdcard_info_t));
    info->status = s_status;
    sdmmc_card_t* card = (s_status == SDCARD_STATUS_MOUNTED) ? bsp_sdcard_get_handle() : NULL;
    if (!card) return ESP_ERR_INVALID_STATE;
    sdcard_populate_info(info, card);
    return ESP_OK;
}

esp_err_t sdcard_format(void)
{
    ESP_LOGW(TAG, "Format via BSP not yet implemented");
    ESP_LOGW(TAG, "Please format the SD card using a PC (FAT32)");
    return ESP_ERR_NOT_SUPPORTED;
}

/**
 * @brief Create the standard application directories on the SD card.
 *
 * Silently skips directories that already exist.
 *
 * @return ESP_OK, or ESP_ERR_INVALID_STATE if the card is not mounted.
 */
esp_err_t sdcard_create_directories(void)
{
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
    for (int i = 0; i < (int)(sizeof(dirs) / sizeof(dirs[0])); i++) {
        sdcard_ensure_dir(dirs[i]);
    }
    return ESP_OK;
}
