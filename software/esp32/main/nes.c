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
#define NES_CONFIG  0x7F /*< NES CONFIG register */

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

esp_err_t nes_data_write(i2c_port_t i2c_num, uint8_t block, uint8_t *data, size_t data_len)
{
	if (block < 8 || block > 127) {
		return ESP_ERR_INVALID_ARG;
	}
	if (!data || data_len == 0 || data_len > 256) {
		return ESP_ERR_INVALID_ARG;
	}

    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    if (!cmd) {
        ESP_LOGE(TAG, "i2c_cmd_link_create error");
        return ESP_ERR_NO_MEM;
    }

    ESP_ERROR_CHECK(i2c_master_start(cmd));
    ESP_ERROR_CHECK(i2c_master_write_byte(cmd, NES_ADDRESS << 1 | I2C_MASTER_WRITE, true));
    ESP_ERROR_CHECK(i2c_master_write_byte(cmd, block | 0x80, true))
    ESP_ERROR_CHECK(i2c_master_write(cmd, data, data_len, true));
    ESP_ERROR_CHECK(i2c_master_stop(cmd));

    esp_err_t ret = i2c_master_cmd_begin(i2c_num, cmd, 1000 / portTICK_RATE_MS);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2c_master_cmd_begin error: [%02X] %s (%d)",
        		NES_ADDRESS, esp_err_to_name(ret), ret);
    }

    i2c_cmd_link_delete(cmd);

    return ret;
}

esp_err_t nes_data_read(i2c_port_t i2c_num, uint8_t block, uint8_t *data, size_t data_len)
{
	esp_err_t ret;
	if (block < 8 || block > 127) {
		return ESP_ERR_INVALID_ARG;
	}
	if (!data || data_len == 0 || data_len > 256) {
		return ESP_ERR_INVALID_ARG;
	}

    ret = i2c_write_byte(i2c_num, NES_ADDRESS, block | 0x80);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "nes_data_write error: %d", ret);
        return ret;
    }

    ret = i2c_read_buffer(i2c_num, NES_ADDRESS, data, data_len);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "nes_data_write error: %d", ret);
        return ret;
    }

    return ret;
}

uint16_t nes_addr_to_apu_block(uint16_t addr)
{
    if (addr >= 0xC000) {
        return (addr >> 6) & 0xFF;
    } else if (addr >= 0x8000) {
        return (((addr - 0xC000) >> 6) & 0xFF) + 256;
    } else {
        ESP_LOGE(TAG, "Invalid block address: $%04X", addr);
        return 0; // consider a better magic value, or simply validate beforehand
    }
}

uint16_t nes_len_to_apu_blocks(uint32_t len)
{
    if ((len & 0x3F) == 0) {
        return len >> 6;
    } else {
        return ((len | 0x3F) + 1) >> 6;
    }
}
