#include "settings.h"

#include <esp_err.h>
#include <esp_log.h>
#include <nvs.h>
#include <string.h>

static const char *TAG = "settings";

#define NVS_NAMESPACE "nestronic"

static esp_err_t settings_set_uint8(const char *key, uint8_t value);
static esp_err_t settings_get_uint8(const char *key, uint8_t *value);
static esp_err_t settings_set_string(const char *key, const char *value);
static esp_err_t settings_get_string(const char *key, char **value);

esp_err_t settings_set_rtc_trim(bool coarse, uint8_t value)
{
    esp_err_t err;
    nvs_handle handle;

    err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open: %s (%d)", esp_err_to_name(err), err);
        return err;
    }

    uint16_t out_value = 0;
    err = nvs_get_u16(handle, "rtc_trim", &out_value);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGE(TAG, "nvs_get_u16: %s (%d)", esp_err_to_name(err), err);
        nvs_close(handle);
        return err;
    }

    uint16_t in_value = (coarse ? 0x0100 : 0x0000) | value;
    if (out_value != in_value) {
        err = nvs_set_u16(handle, "rtc_trim", in_value);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "nvs_set_u16: %s (%d)", esp_err_to_name(err), err);
            nvs_close(handle);
            return err;
        }

        err = nvs_commit(handle);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "nvs_commit: %s (%d)", esp_err_to_name(err), err);
            nvs_close(handle);
            return err;
        }
    }

    nvs_close(handle);

    return ESP_OK;
}

esp_err_t settings_get_rtc_trim(bool *coarse, uint8_t *value)
{
    esp_err_t err = ESP_OK;
    nvs_handle handle = 0;
    uint16_t out_value = 0;

    if (!coarse || !value) {
        return ESP_ERR_INVALID_ARG;
    }

    do {
        err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "nvs_open: %s (%d)", esp_err_to_name(err), err);
            break;
        }

        err = nvs_get_u16(handle, "rtc_trim", &out_value);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "nvs_get_u16: %s (%d)", esp_err_to_name(err), err);
            break;
        }
    } while(0);

    if (err == ESP_OK || err == ESP_ERR_NVS_NOT_FOUND) {
        *coarse = (out_value & 0x0100) == 0x0100;
        *value = (uint8_t)(out_value & 0x00FF);
        err = ESP_OK;
    }

    nvs_close(handle);
    return err;
}

esp_err_t settings_set_time_zone(const char *zone_name)
{
    return settings_set_string("time_zone", zone_name);
}

esp_err_t settings_get_time_zone(char **zone_name)
{
    return settings_get_string("time_zone", zone_name);
}

esp_err_t settings_set_time_format(bool twentyfour)
{
    esp_err_t err;
    nvs_handle handle;

    err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open: %s (%d)", esp_err_to_name(err), err);
        return err;
    }

    uint8_t out_value = 0;
    err = nvs_get_u8(handle, "time_format", &out_value);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGE(TAG, "nvs_get_u8: %s (%d)", esp_err_to_name(err), err);
        nvs_close(handle);
        return err;
    }

    uint8_t in_value = twentyfour ? 1 : 0;
    if (out_value != in_value) {
        err = nvs_set_u8(handle, "time_format", in_value);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "nvs_set_u8: %s (%d)", esp_err_to_name(err), err);
            nvs_close(handle);
            return err;
        }

        err = nvs_commit(handle);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "nvs_commit: %s (%d)", esp_err_to_name(err), err);
            nvs_close(handle);
            return err;
        }
    }

    nvs_close(handle);

    return ESP_OK;
}

esp_err_t settings_get_time_format(bool *twentyfour)
{
    esp_err_t err = ESP_OK;
    nvs_handle handle = 0;
    uint8_t out_value = 0;

    if (!twentyfour) {
        return ESP_ERR_INVALID_ARG;
    }

    do {
        err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "nvs_open: %s (%d)", esp_err_to_name(err), err);
            break;
        }

        err = nvs_get_u8(handle, "time_format", &out_value);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "nvs_get_u8: %s (%d)", esp_err_to_name(err), err);
            break;
        }
    } while(0);

    if (err == ESP_OK || err == ESP_ERR_NVS_NOT_FOUND) {
        *twentyfour = (out_value == 1);
        err = ESP_OK;
    }

    nvs_close(handle);
    return err;
}

esp_err_t settings_set_ntp_server(const char *hostname)
{
    return settings_set_string("ntp_server", hostname);
}

esp_err_t settings_get_ntp_server(char **hostname)
{
    return settings_get_string("ntp_server", hostname);
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
        ESP_LOGE(TAG, "nvs_open: %s (%d)", esp_err_to_name(err), err);
        return err;
    }

    uint16_t out_value = 0;
    err = nvs_get_u16(handle, "alarm_time", &out_value);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGE(TAG, "nvs_get_u16: %s (%d)", esp_err_to_name(err), err);
        nvs_close(handle);
        return err;
    }

    uint16_t in_value = (hh << 8) | mm;
    if (out_value != in_value) {
        err = nvs_set_u16(handle, "alarm_time", in_value);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "nvs_set_u16: %s (%d)", esp_err_to_name(err), err);
            nvs_close(handle);
            return err;
        }

        err = nvs_commit(handle);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "nvs_commit: %s (%d)", esp_err_to_name(err), err);
            nvs_close(handle);
            return err;
        }
    }

    nvs_close(handle);

    return ESP_OK;
}

esp_err_t settings_get_alarm_time(uint8_t *hh, uint8_t *mm)
{
    esp_err_t err = ESP_OK;
    nvs_handle handle = 0;
    uint16_t out_value = 0;

    if (!hh || !mm) {
        return ESP_ERR_INVALID_ARG;
    }

    do {
        err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "nvs_open: %s (%d)", esp_err_to_name(err), err);
            break;
        }

        err = nvs_get_u16(handle, "alarm_time", &out_value);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "nvs_get_u16: %s (%d)", esp_err_to_name(err), err);
            break;
        }
    } while(0);

    if (err == ESP_OK || err == ESP_ERR_NVS_NOT_FOUND) {
        uint8_t hh_value = (uint8_t)((out_value & 0xFF00) >> 8);
        uint8_t mm_value = (uint8_t)(out_value & 0x00FF);

        if (hh_value > 23 || mm_value > 59) {
            *hh = 0;
            *mm = 0;
        } else {
            *hh = hh_value;
            *mm = mm_value;
        }
        err = ESP_OK;
    }

    nvs_close(handle);
    return err;
}

esp_err_t settings_set_alarm_tune(const char *filename, const char *title, const char *subtitle, uint8_t song)
{
    if (!filename || strlen(filename) == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t err = settings_set_string("alarm_tune", filename);
    settings_set_string("alarm_tune_tt", (title && strlen(title) > 0) ? title : "");
    settings_set_string("alarm_tune_st", (subtitle && strlen(subtitle) > 0) ? subtitle : "");
    settings_set_uint8("alarm_tune_sn", song);

    return err;
}

esp_err_t settings_get_alarm_tune(char **filename, char **title, char **subtitle, uint8_t *song)
{
    if (filename) {
        esp_err_t err = settings_get_string("alarm_tune", filename);
        if (err != ESP_OK) {
            return err;
        }
    }
    if (title) {
        settings_get_string("alarm_tune_tt", title);
    }
    if (subtitle) {
        settings_get_string("alarm_tune_st", subtitle);
    }
    if (song) {
        settings_get_uint8("alarm_tune_sn", song);
    }
    return ESP_OK;
}

static esp_err_t settings_set_uint8(const char *key, uint8_t value)
{
    esp_err_t err;
    nvs_handle handle;

    err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open: %s (%d)", esp_err_to_name(err), err);
        return err;
    }

    uint8_t out_value = 0;
    err = nvs_get_u8(handle, key, &out_value);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGE(TAG, "nvs_get_u8: %s (%d)", esp_err_to_name(err), err);
        nvs_close(handle);
        return err;
    }

    if (out_value != value) {
        err = nvs_set_u8(handle, key, value);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "nvs_set_u8: %s (%d)", esp_err_to_name(err), err);
            nvs_close(handle);
            return err;
        }

        err = nvs_commit(handle);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "nvs_commit: %s (%d)", esp_err_to_name(err), err);
            nvs_close(handle);
            return err;
        }
    }

    nvs_close(handle);

    return ESP_OK;
}

static esp_err_t settings_get_uint8(const char *key, uint8_t *value)
{
    esp_err_t err = ESP_OK;
    nvs_handle handle = 0;
    uint8_t out_value = 0;

    if (!value) {
        return ESP_ERR_INVALID_ARG;
    }

    do {
        err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "nvs_open: %s (%d)", esp_err_to_name(err), err);
            break;
        }

        err = nvs_get_u8(handle, key, &out_value);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "nvs_get_u8: %s (%d)", esp_err_to_name(err), err);
            break;
        }
    } while(0);

    if (err == ESP_OK || err == ESP_ERR_NVS_NOT_FOUND) {
        *value = out_value;
        err = ESP_OK;
    }

    nvs_close(handle);
    return err;
}

static esp_err_t settings_set_string(const char *key, const char *value)
{
    esp_err_t err;
    nvs_handle handle;
    bool value_changed;

    if (!value || strlen(value) == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open: %s (%d)", esp_err_to_name(err), err);
        return err;
    }

    size_t required_size = 0;
    err = nvs_get_str(handle, key, NULL, &required_size);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGE(TAG, "nvs_get_str: %s (%d)", esp_err_to_name(err), err);
        nvs_close(handle);
        return err;
    }

    if (required_size > 0) {
        char *existing_value = malloc(required_size);
        if (!existing_value) {
            nvs_close(handle);
            return ESP_ERR_NO_MEM;
        }

        err = nvs_get_str(handle, key, existing_value, &required_size);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "nvs_get_str: %s (%d)", esp_err_to_name(err), err);
            nvs_close(handle);
            free(existing_value);
            return err;
        }

        value_changed = (strcmp(existing_value, value) != 0);
        free(existing_value);
    } else {
        value_changed = true;
    }

    if (value_changed) {
        err = nvs_set_str(handle, key, value);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "nvs_set_str: %s (%d)", esp_err_to_name(err), err);
            nvs_close(handle);
            return err;
        }

        err = nvs_commit(handle);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "nvs_commit: %s (%d)", esp_err_to_name(err), err);
            nvs_close(handle);
            return err;
        }
    }

    nvs_close(handle);
    return ESP_OK;
}

static esp_err_t settings_get_string(const char *key, char **value)
{
    esp_err_t err = ESP_OK;
    nvs_handle handle = 0;
    size_t required_size = 0;
    char *existing_value = 0;

    if (!value) {
        return ESP_ERR_INVALID_ARG;
    }

    do {
        err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "nvs_open: %s (%d)", esp_err_to_name(err), err);
            break;
        }

        err = nvs_get_str(handle, key, NULL, &required_size);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "nvs_get_str: %s (%d)", esp_err_to_name(err), err);
            break;
        }

        existing_value = malloc(required_size);
        if (!existing_value) {
            err = ESP_ERR_NO_MEM;
            break;
        }

        err = nvs_get_str(handle, key, existing_value, &required_size);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "nvs_get_str: %s (%d)", esp_err_to_name(err), err);
            break;
        }
    } while(0);

    if (err == ESP_OK || err == ESP_ERR_NVS_NOT_FOUND) {
        *value = existing_value;
        err = ESP_OK;
    } else {
        if (existing_value) {
            free(existing_value);
        }
    }

    nvs_close(handle);
    return err;
}
