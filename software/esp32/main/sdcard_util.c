/*
 * Initialization and utility functions for the SD/MMC controller
 */

#include "sdcard_util.h"

#include <stdio.h>
#include <esp_err.h>
#include <esp_log.h>
#include <esp_types.h>
#include <sdmmc_cmd.h>
#include <esp_vfs_fat.h>
#include <driver/sdmmc_host.h>
#include <driver/gpio.h>

#include "board_config.h"

static const char *TAG = "sdcard_util";

static sdmmc_card_t *card = NULL;

esp_err_t sdcard_init()
{
    // Configure the GPIO for the SD card detect pin
    gpio_config_t config = {
        .pin_bit_mask = 1ULL << SDMMC_CD,
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE
    };
    ESP_ERROR_CHECK(gpio_config(&config));

    // GPIOs used for CMD, D0, D1, D2, D3 have external pull-ups.
    // However, the examples recommend also enabling the internal pull-ups.
    ESP_ERROR_CHECK(gpio_set_pull_mode(SDMMC_CMD, GPIO_PULLUP_ONLY));
    ESP_ERROR_CHECK(gpio_set_pull_mode(SDMMC_D0, GPIO_PULLUP_ONLY));
    ESP_ERROR_CHECK(gpio_set_pull_mode(SDMMC_D1, GPIO_PULLUP_ONLY));
    ESP_ERROR_CHECK(gpio_set_pull_mode(SDMMC_D2, GPIO_PULLUP_ONLY));
    ESP_ERROR_CHECK(gpio_set_pull_mode(SDMMC_D3, GPIO_PULLUP_ONLY));

    ESP_LOGI(TAG, "SD/MMC GPIO pins initialized");

    card = NULL;

    return ESP_OK;
}

esp_err_t sdcard_mount(const char *base_path)
{
    esp_err_t ret;
    sdmmc_card_t *out_card;

    if (card) {
        ESP_LOGE(TAG, "SD Card already mounted");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Mounting SD Card in SD/MMC mode");

    // SD/MMC host configuration
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();

    // SD/MMC slot configuration
    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.gpio_cd = SDMMC_CD;

    // SD/MMC filesystem mount configuration
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false,
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };

    // Use settings defined above to initialize SD card and mount FAT filesystem.
    // Note: esp_vfs_fat_sdmmc_mount is an all-in-one convenience function.
    // Please check its source code and implement error recovery when developing
    // production applications.
    ret = esp_vfs_fat_sdmmc_mount(base_path, &host, &slot_config, &mount_config, &out_card);

    if (ret != ESP_OK) {
        if (ret == ESP_FAIL) {
            ESP_LOGE(TAG, "Failed to mount filesystem. "
                "If you want the card to be formatted, set format_if_mount_failed = true.");
        } else {
            if (gpio_get_level(SDMMC_CD) == 1) {
                ESP_LOGE(TAG, "Card was not detected (%d)", ret);
            } else {
                ESP_LOGE(TAG, "Failed to initialize the card (%d)", ret);
            }
        }
    } else {
        ESP_LOGI(TAG, "SD Card mounted");
        sdmmc_card_print_info(stdout, out_card);
        card = out_card;
    }

    return ret;
}

bool sdcard_is_detected()
{
    return gpio_get_level(SDMMC_CD) == 0;
}

bool sdcard_is_mounted()
{
    return card != NULL;
}

esp_err_t sdcard_unmount()
{
    esp_err_t ret;

    ret = esp_vfs_fat_sdmmc_unmount();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_vfs_fat_sdmmc_unmount error: %d", ret);
    } else {
        ESP_LOGI(TAG, "SD Card unmounted");
    }

    card = NULL;

    return ret;
}
