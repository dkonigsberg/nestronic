#include "nes.h"

#include <esp_err.h>
#include <esp_log.h>
#include <driver/i2c.h>

#include "i2c_util.h"

static const char *TAG = "nes";

/* I2C device address */
#define NES_ADDRESS 0x08

/* I2C registers */
#define NES_OUTPUT  0x16 /*< NES OUTPUT register */
#define NES_CONFIG  0x80 /*< NES CONFIG register */

esp_err_t nes_init(i2c_port_t i2c_num)
{
    esp_err_t ret = ESP_OK;

    ret = nes_apu_init(i2c_num);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = nes_set_amplifier_enabled(i2c_num, false);
    if (ret != ESP_OK) {
        return ret;
    }

    ESP_LOGI(TAG, "NES CPU Initialized");

    return ret;
}

esp_err_t nes_set_config(i2c_port_t i2c_num, uint8_t value)
{
    return i2c_write_register(i2c_num, NES_ADDRESS, NES_CONFIG, value);
}

esp_err_t nes_get_config(i2c_port_t i2c_num, uint8_t *value)
{
    if (!value) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t data;
    esp_err_t ret = i2c_read_register(i2c_num, NES_ADDRESS, NES_CONFIG, &data);
    if (ret != ESP_OK) {
        return ret;
    }

    *value = data;

    return ESP_OK;
}

esp_err_t nes_set_amplifier_enabled(i2c_port_t i2c_num, bool enabled)
{
    return i2c_write_register(i2c_num, NES_ADDRESS, NES_OUTPUT, enabled ? 0x01 : 0x00);
}

esp_err_t nes_get_amplifier_enabled(i2c_port_t i2c_num, bool *enabled)
{
    if (!enabled) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t data;
    esp_err_t ret = i2c_read_register(i2c_num, NES_ADDRESS, NES_OUTPUT, &data);
    if (ret != ESP_OK) {
        return ret;
    }

    *enabled = (data & 0x01) == 0x01;

    return ESP_OK;
}

esp_err_t nes_apu_init(i2c_port_t i2c_num)
{
    uint8_t data;
    esp_err_t ret = i2c_read_register(i2c_num, NES_ADDRESS, NES_OUTPUT, &data);
    if (ret != ESP_OK) {
        return ret;
    }

    data |= 0x80;

    return i2c_write_register(i2c_num, NES_ADDRESS, NES_OUTPUT, data);
}

esp_err_t nes_apu_write(i2c_port_t i2c_num, nes_apu_register_t reg, uint8_t dat)
{
    return i2c_write_register(i2c_num, NES_ADDRESS, (uint8_t)(reg & 0xFF), dat);
}
