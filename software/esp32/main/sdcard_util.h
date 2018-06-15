#ifndef SDCARD_UTIL_H
#define SDCARD_UTIL_H

#include <esp_err.h>
#include <driver/sdmmc_types.h>

esp_err_t sdcard_init();

esp_err_t sdcard_mount(const char *base_path);
bool sdcard_is_detected();
bool sdcard_is_mounted();
esp_err_t sdcard_unmount();

#endif /* SDCARD_UTIL_H */
