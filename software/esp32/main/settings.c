#include "settings.h"

#include <esp_err.h>
#include <esp_log.h>
#include <nvs.h>
#include <string.h>

static const char *TAG = "settings";

#define NVS_NAMESPACE "nestronic"

esp_err_t settings_set_rtc_trim(bool coarse, uint8_t value)
{
    esp_err_t err;
    nvs_handle handle;

    err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open: %d", err);
        return err;
    }

    uint16_t out_value = 0;
    err = nvs_get_u16(handle, "rtc_trim", &out_value);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGE(TAG, "nvs_get_u16: %d", err);
        nvs_close(handle);
        return err;
    }

    uint16_t in_value = (coarse ? 0x0100 : 0x0000) | value;
    if (out_value != in_value) {
        err = nvs_set_u16(handle, "rtc_trim", in_value);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "nvs_set_u16: %d", err);
            nvs_close(handle);
            return err;
        }

        err = nvs_commit(handle);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "nvs_commit: %d", err);
            nvs_close(handle);
            return err;
        }
    }

    nvs_close(handle);

    return ESP_OK;
}

esp_err_t settings_get_rtc_trim(bool *coarse, uint8_t *value)
{
    esp_err_t err;
    nvs_handle handle;

    if (!coarse || !value) {
        return ESP_ERR_INVALID_ARG;
    }

    err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open: %d", err);
        return err;
    }

    uint16_t out_value = 0;
    err = nvs_get_u16(handle, "rtc_trim", &out_value);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGE(TAG, "nvs_get_u16: %d", err);
        nvs_close(handle);
        return err;
    }

    *coarse = (out_value & 0x0100) == 0x0100;
    *value = (uint8_t)(out_value & 0x00FF);

    nvs_close(handle);
    return ESP_OK;
}

esp_err_t settings_set_time_zone(const char *zone_name)
{
    esp_err_t err;
    nvs_handle handle;
    bool value_changed;

    if (!zone_name || strlen(zone_name) == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open: %d", err);
        return err;
    }

    size_t required_size = 0;
    err = nvs_get_str(handle, "time_zone", NULL, &required_size);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGE(TAG, "nvs_get_str: %d", err);
        nvs_close(handle);
        return err;
    }

    if (required_size > 0) {
        char *existing_value = malloc(required_size);
        if (!existing_value) {
            nvs_close(handle);
            return ESP_ERR_NO_MEM;
        }

        err = nvs_get_str(handle, "time_zone", existing_value, &required_size);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "nvs_get_str: %d", err);
            nvs_close(handle);
            free(existing_value);
            return err;
        }

        value_changed = (strcmp(existing_value, zone_name) != 0);
        free(existing_value);
    } else {
        value_changed = true;
    }

    if (value_changed) {
        err = nvs_set_str(handle, "time_zone", zone_name);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "nvs_set_str: %d", err);
            nvs_close(handle);
            return err;
        }

        err = nvs_commit(handle);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "nvs_commit: %d", err);
            nvs_close(handle);
            return err;
        }
    }

    nvs_close(handle);
    return ESP_OK;
}

esp_err_t settings_get_time_zone(char **zone_name)
{
    esp_err_t err;
    nvs_handle handle;

    if (!zone_name) {
        return ESP_ERR_INVALID_ARG;
    }

    err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open: %d", err);
        return err;
    }

    size_t required_size = 0;
    err = nvs_get_str(handle, "time_zone", NULL, &required_size);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGE(TAG, "nvs_get_str: %d", err);
        nvs_close(handle);
        return err;
    }

    char *existing_value = malloc(required_size);
    if (!existing_value) {
        nvs_close(handle);
        return ESP_ERR_NO_MEM;
    }

    err = nvs_get_str(handle, "time_zone", existing_value, &required_size);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_get_str: %d", err);
        free(existing_value);
        nvs_close(handle);
        return err;
    }

    *zone_name = existing_value;

    nvs_close(handle);
    return ESP_OK;
}

esp_err_t settings_set_time_format(bool twentyfour)
{
    esp_err_t err;
    nvs_handle handle;

    err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open: %d", err);
        return err;
    }

    uint8_t out_value = 0;
    err = nvs_get_u8(handle, "time_format", &out_value);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGE(TAG, "nvs_get_u8: %d", err);
        nvs_close(handle);
        return err;
    }

    uint8_t in_value = twentyfour ? 1 : 0;
    if (out_value != in_value) {
        err = nvs_set_u8(handle, "time_format", in_value);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "nvs_set_u8: %d", err);
            nvs_close(handle);
            return err;
        }

        err = nvs_commit(handle);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "nvs_commit: %d", err);
            nvs_close(handle);
            return err;
        }
    }

    nvs_close(handle);

    return ESP_OK;
}

esp_err_t settings_get_time_format(bool *twentyfour)
{
    esp_err_t err;
    nvs_handle handle;

    if (!twentyfour) {
        return ESP_ERR_INVALID_ARG;
    }

    err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open: %d", err);
        return err;
    }

    uint8_t out_value = 0;
    err = nvs_get_u8(handle, "time_format", &out_value);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGE(TAG, "nvs_get_u8: %d", err);
        nvs_close(handle);
        return err;
    }

    *twentyfour = (out_value == 1);

    nvs_close(handle);
    return ESP_OK;
}

esp_err_t settings_set_ntp_server(const char *hostname)
{
    esp_err_t err;
    nvs_handle handle;
    bool value_changed;

    if (!hostname || strlen(hostname) == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open: %d", err);
        return err;
    }

    size_t required_size = 0;
    err = nvs_get_str(handle, "ntp_server", NULL, &required_size);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGE(TAG, "nvs_get_str: %d", err);
        nvs_close(handle);
        return err;
    }

    if (required_size > 0) {
        char *existing_value = malloc(required_size);
        if (!existing_value) {
            nvs_close(handle);
            return ESP_ERR_NO_MEM;
        }

        err = nvs_get_str(handle, "ntp_server", existing_value, &required_size);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "nvs_get_str: %d", err);
            nvs_close(handle);
            free(existing_value);
            return err;
        }

        value_changed = (strcmp(existing_value, hostname) != 0);
        free(existing_value);
    } else {
        value_changed = true;
    }

    if (value_changed) {
        err = nvs_set_str(handle, "ntp_server", hostname);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "nvs_set_str: %d", err);
            nvs_close(handle);
            return err;
        }

        err = nvs_commit(handle);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "nvs_commit: %d", err);
            nvs_close(handle);
            return err;
        }
    }

    nvs_close(handle);
    return ESP_OK;
}

esp_err_t settings_get_ntp_server(char **hostname)
{
    esp_err_t err;
    nvs_handle handle;

    if (!hostname) {
        return ESP_ERR_INVALID_ARG;
    }

    err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open: %d", err);
        return err;
    }

    size_t required_size = 0;
    err = nvs_get_str(handle, "ntp_server", NULL, &required_size);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGE(TAG, "nvs_get_str: %d", err);
        nvs_close(handle);
        return err;
    }

    char *existing_value = malloc(required_size);
    if (!existing_value) {
        nvs_close(handle);
        return ESP_ERR_NO_MEM;
    }

    err = nvs_get_str(handle, "ntp_server", existing_value, &required_size);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_get_str: %d", err);
        free(existing_value);
        nvs_close(handle);
        return err;
    }

    *hostname = existing_value;

    nvs_close(handle);
    return ESP_OK;
}

esp_err_t settings_set_alarm_time(uint8_t hh, uint8_t mm)
{
    esp_err_t err;
    nvs_handle handle;

    if (hh > 23 || mm > 59) {
        return ESP_ERR_INVALID_ARG;
    }

    err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open: %d", err);
        return err;
    }

    uint16_t out_value = 0;
    err = nvs_get_u16(handle, "alarm_time", &out_value);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGE(TAG, "nvs_get_u16: %d", err);
        nvs_close(handle);
        return err;
    }

    uint16_t in_value = (hh << 8) | mm;
    if (out_value != in_value) {
        err = nvs_set_u16(handle, "alarm_time", in_value);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "nvs_set_u16: %d", err);
            nvs_close(handle);
            return err;
        }

        err = nvs_commit(handle);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "nvs_commit: %d", err);
            nvs_close(handle);
            return err;
        }
    }

    nvs_close(handle);

    return ESP_OK;
}

esp_err_t settings_get_alarm_time(uint8_t *hh, uint8_t *mm)
{
    esp_err_t err;
    nvs_handle handle;

    if (!hh || !mm) {
        return ESP_ERR_INVALID_ARG;
    }

    err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open: %d", err);
        return err;
    }

    uint16_t out_value = 0;
    err = nvs_get_u16(handle, "alarm_time", &out_value);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGE(TAG, "nvs_get_u16: %d", err);
        nvs_close(handle);
        return err;
    }

    uint8_t hh_value = (uint8_t)((out_value & 0xFF00) >> 8);
    uint8_t mm_value = (uint8_t)(out_value & 0x00FF);

    if (hh_value > 23 || mm_value > 59) {
        *hh = 0;
        *mm = 0;
    } else {
        *hh = hh_value;
        *mm = mm_value;
    }

    nvs_close(handle);
    return ESP_OK;
}
