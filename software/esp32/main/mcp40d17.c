#include "mcp40d17.h"

#include <esp_err.h>
#include <esp_log.h>
#include <driver/i2c.h>

static const char *TAG = "mcp40d17";

/* I2C device address */
#define MCP40D17_ADDRESS 0x2E

/* I2C registers */
#define MCP40D17_CONTROL 0x00

/* Misc constants */
#define WIPER_MAX 127

esp_err_t mcp40d17_init(i2c_port_t i2c_num)
{
    return ESP_OK;
}

esp_err_t mcp40d17_set_wiper(i2c_port_t i2c_num, uint8_t value)
{
    esp_err_t ret = ESP_OK;

    // Enforce the maximum value
    if (value > WIPER_MAX) { value = WIPER_MAX; }

    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    if (!cmd) {
        ESP_LOGE(TAG, "i2c_cmd_link_create error");
        return ESP_ERR_NO_MEM;
    }

    ESP_ERROR_CHECK(i2c_master_start(cmd));
    ESP_ERROR_CHECK(i2c_master_write_byte(cmd, MCP40D17_ADDRESS << 1 | I2C_MASTER_WRITE, true));
    ESP_ERROR_CHECK(i2c_master_write_byte(cmd, MCP40D17_CONTROL, true));
    ESP_ERROR_CHECK(i2c_master_write_byte(cmd, value, true));
    ESP_ERROR_CHECK(i2c_master_stop(cmd));

    ret = i2c_master_cmd_begin(i2c_num, cmd, 1000 / portTICK_RATE_MS);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2c_master_cmd_begin error: [%02X] %s (%d)",
                MCP40D17_ADDRESS, esp_err_to_name(ret), ret);
    }

    i2c_cmd_link_delete(cmd);

    return ret;
}

esp_err_t mcp40d17_get_wiper(i2c_port_t i2c_num, uint8_t *value)
{
    esp_err_t ret = ESP_OK;

    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    if (!cmd) {
        ESP_LOGE(TAG, "i2c_cmd_link_create error");
        return ESP_ERR_NO_MEM;
    }

    ESP_ERROR_CHECK(i2c_master_start(cmd));
    ESP_ERROR_CHECK(i2c_master_write_byte(cmd, MCP40D17_ADDRESS << 1 | I2C_MASTER_WRITE, true));
    ESP_ERROR_CHECK(i2c_master_write_byte(cmd, MCP40D17_CONTROL, true));
    ESP_ERROR_CHECK(i2c_master_stop(cmd));

    ret = i2c_master_cmd_begin(i2c_num, cmd, 1000 / portTICK_RATE_MS);
    i2c_cmd_link_delete(cmd);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2c_master_cmd_begin error: [%02X] %s (%d)",
                MCP40D17_ADDRESS, esp_err_to_name(ret), ret);
        return ret;
    }

    cmd = i2c_cmd_link_create();
    if (!cmd) {
        ESP_LOGE(TAG, "i2c_cmd_link_create error");
        return ESP_ERR_NO_MEM;
    }

    ESP_ERROR_CHECK(i2c_master_start(cmd));
    ESP_ERROR_CHECK(i2c_master_write_byte(cmd, MCP40D17_ADDRESS << 1 | I2C_MASTER_READ, true));
    ESP_ERROR_CHECK(i2c_master_read_byte(cmd, value, true));
    ESP_ERROR_CHECK(i2c_master_stop(cmd));

    ret = i2c_master_cmd_begin(i2c_num, cmd, 1000 / portTICK_RATE_MS);
    i2c_cmd_link_delete(cmd);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2c_master_cmd_begin error: [%02X] %s (%d)",
                MCP40D17_ADDRESS, esp_err_to_name(ret), ret);
    }

    return ret;
}
