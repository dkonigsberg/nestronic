/*
 * Initialization and utility functions for the I2C controllers
 */

#include "i2c_util.h"

#include <freertos/FreeRTOS.h>
#include <esp_err.h>
#include <esp_log.h>
#include <driver/i2c.h>

#include "board_config.h"

static const char *TAG = "i2c_util";

SemaphoreHandle_t i2c_p0_mutex = NULL;
SemaphoreHandle_t i2c_p1_mutex = NULL;

esp_err_t i2c_init_master_port0()
{
    esp_err_t ret;
    const i2c_port_t port = I2C_P0_NUM;

    i2c_p0_mutex = xSemaphoreCreateMutex();
    if (!i2c_p0_mutex) {
        ESP_LOGE(TAG, "xSemaphoreCreateMutex error");
        return ESP_ERR_NO_MEM;
    }

    i2c_config_t conf;
    conf.mode = I2C_MODE_MASTER;
    conf.sda_io_num = I2C_P0_SDA_IO;
    conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
    conf.scl_io_num = I2C_P0_SCL_IO;
    conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
    conf.master.clk_speed = I2C_P0_FREQ_HZ;

    ret = i2c_param_config(port, &conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2c_param_config error: %d", ret);
        return ret;
    }

    ret = i2c_driver_install(port, conf.mode,
                             I2C_P0_RX_BUF_DISABLE,
                             I2C_P0_TX_BUF_DISABLE, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2c_driver_install error: %d", ret);
        return ret;
    }

    // Need to use a longer timeout due to the low clock speed
    // and in-line command processing of the NES CPU.
    ret = i2c_set_timeout(port, 6400);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2c_set_timeout error: %d", ret);
        return ret;
    }

    ESP_LOGI(TAG, "I2C Master port 0 initialized");
    return ESP_OK;
}

esp_err_t i2c_init_master_port1()
{
    esp_err_t ret;
    const i2c_port_t port = I2C_P1_NUM;

    i2c_p1_mutex = xSemaphoreCreateMutex();
    if (!i2c_p1_mutex) {
        ESP_LOGE(TAG, "xSemaphoreCreateMutex error");
        return ESP_ERR_NO_MEM;
    }

    i2c_config_t conf;
    conf.mode = I2C_MODE_MASTER;
    conf.sda_io_num = I2C_P1_SDA_IO;
    conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
    conf.scl_io_num = I2C_P1_SCL_IO;
    conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
    conf.master.clk_speed = I2C_P1_FREQ_HZ;

    ret = i2c_param_config(port, &conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2c_param_config error: %d", ret);
        return ret;
    }
    ret = i2c_driver_install(port, conf.mode,
                             I2C_P1_RX_BUF_DISABLE,
                             I2C_P1_TX_BUF_DISABLE, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2c_driver_install error: %d", ret);
        return ret;
    }

    ESP_LOGI(TAG, "I2C Master port 1 initialized");
    return ESP_OK;
}

void i2c_mutex_lock(i2c_port_t port)
{
    SemaphoreHandle_t i2c_mutex = (port == I2C_P1_NUM) ? i2c_p1_mutex : i2c_p0_mutex;
    if (i2c_mutex) {
        xSemaphoreTake(i2c_mutex, portMAX_DELAY);
    }
}

void i2c_mutex_unlock(i2c_port_t port)
{
    SemaphoreHandle_t i2c_mutex = (port == I2C_P1_NUM) ? i2c_p1_mutex : i2c_p0_mutex;
    if (i2c_mutex) {
        xSemaphoreGive(i2c_mutex);
    }
}

void i2c_bus_scan(i2c_port_t port)
{
    i2c_mutex_lock(port);

    ESP_LOGI(TAG, "SCAN START: %d", port);
    uint8_t i;
    for(i = 0; i < 0x7F; i++) {
        i2c_cmd_handle_t cmd = i2c_cmd_link_create();
        if (!cmd) {
            ESP_LOGE(TAG, "i2c_cmd_link_create error");
            continue;
        }

        ESP_ERROR_CHECK(i2c_master_start(cmd));
        ESP_ERROR_CHECK(i2c_master_write_byte(cmd, i << 1 | I2C_MASTER_WRITE, true));
        ESP_ERROR_CHECK(i2c_master_stop(cmd));

        esp_err_t ret = i2c_master_cmd_begin(port, cmd, 1000 / portTICK_RATE_MS);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Device(%d): %02X", port, i);
        }

        i2c_cmd_link_delete(cmd);
    }
    ESP_LOGI(TAG, "SCAN END: %d", port);

    i2c_mutex_unlock(port);
}

esp_err_t i2c_read_byte(i2c_port_t i2c_num, uint8_t device_id, uint8_t *data)
{
    if (!data) {
        return ESP_ERR_INVALID_ARG;
    }

    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    if (!cmd) {
        ESP_LOGE(TAG, "i2c_cmd_link_create error");
        return ESP_ERR_NO_MEM;
    }

    ESP_ERROR_CHECK(i2c_master_start(cmd));
    ESP_ERROR_CHECK(i2c_master_write_byte(cmd, device_id << 1 | I2C_MASTER_READ, true));
    ESP_ERROR_CHECK(i2c_master_read_byte(cmd, data, true));
    ESP_ERROR_CHECK(i2c_master_stop(cmd));

    esp_err_t ret = i2c_master_cmd_begin(i2c_num, cmd, 1000 / portTICK_RATE_MS);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2c_master_cmd_begin error: [%02X] %s (%d)",
                device_id, esp_err_to_name(ret), ret);
    }

    i2c_cmd_link_delete(cmd);

    return ret;
}

esp_err_t i2c_write_byte(i2c_port_t i2c_num, uint8_t device_id, uint8_t data)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    if (!cmd) {
        ESP_LOGE(TAG, "i2c_cmd_link_create error");
        return ESP_ERR_NO_MEM;
    }

    ESP_ERROR_CHECK(i2c_master_start(cmd));
    ESP_ERROR_CHECK(i2c_master_write_byte(cmd, device_id << 1 | I2C_MASTER_WRITE, true));
    ESP_ERROR_CHECK(i2c_master_write_byte(cmd, data, true));
    ESP_ERROR_CHECK(i2c_master_stop(cmd));

    esp_err_t ret = i2c_master_cmd_begin(i2c_num, cmd, 1000 / portTICK_RATE_MS);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2c_master_cmd_begin error: [%02X] %s (%d)",
                device_id, esp_err_to_name(ret), ret);
    }

    i2c_cmd_link_delete(cmd);

    return ret;
}

esp_err_t i2c_read_buffer(i2c_port_t i2c_num, uint8_t device_id, uint8_t *data, size_t data_len)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    if (!cmd) {
        ESP_LOGE(TAG, "i2c_cmd_link_create error");
        return ESP_ERR_NO_MEM;
    }

    ESP_ERROR_CHECK(i2c_master_start(cmd));
    ESP_ERROR_CHECK(i2c_master_write_byte(cmd, device_id << 1 | I2C_MASTER_READ, true));

    if (data_len > 1) {
        ESP_ERROR_CHECK(i2c_master_read(cmd, data, data_len - 1, false));
    }
    if (data_len > 0) {
        ESP_ERROR_CHECK(i2c_master_read_byte(cmd, data + (data_len - 1), true));
    }

    ESP_ERROR_CHECK(i2c_master_stop(cmd));

    esp_err_t ret = i2c_master_cmd_begin(i2c_num, cmd, 1000 / portTICK_RATE_MS);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2c_master_cmd_begin error: [%02X] %s (%d)",
                device_id, esp_err_to_name(ret), ret);
    }

    i2c_cmd_link_delete(cmd);

    return ret;
}

esp_err_t i2c_write_buffer(i2c_port_t i2c_num, uint8_t device_id, uint8_t *data, size_t data_len)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    if (!cmd) {
        ESP_LOGE(TAG, "i2c_cmd_link_create error");
        return ESP_ERR_NO_MEM;
    }

    ESP_ERROR_CHECK(i2c_master_start(cmd));
    ESP_ERROR_CHECK(i2c_master_write_byte(cmd, device_id << 1 | I2C_MASTER_WRITE, true));
    ESP_ERROR_CHECK(i2c_master_write(cmd, data, data_len, true));
    ESP_ERROR_CHECK(i2c_master_stop(cmd));

    esp_err_t ret = i2c_master_cmd_begin(i2c_num, cmd, 1000 / portTICK_RATE_MS);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2c_master_cmd_begin error: [%02X] %s (%d)",
                device_id, esp_err_to_name(ret), ret);
    }

    i2c_cmd_link_delete(cmd);

    return ret;
}

esp_err_t i2c_read_register(i2c_port_t i2c_num, uint8_t device_id, uint8_t reg, uint8_t *data)
{
    esp_err_t ret = ESP_OK;

    if (!data) {
        return ESP_ERR_INVALID_ARG;
    }

    ret = i2c_write_byte(i2c_num, device_id, reg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2c_write_byte error: %d", ret);
        return ret;
    }

    ret = i2c_read_byte(i2c_num, device_id, data);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2c_read_byte error: %d", ret);
    }

    return ret;
}

esp_err_t i2c_write_register(i2c_port_t i2c_num, uint8_t device_id, uint8_t reg, uint8_t data)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    if (!cmd) {
        ESP_LOGE(TAG, "i2c_cmd_link_create error");
        return ESP_ERR_NO_MEM;
    }

    ESP_ERROR_CHECK(i2c_master_start(cmd));
    ESP_ERROR_CHECK(i2c_master_write_byte(cmd, device_id << 1 | I2C_MASTER_WRITE, true));
    ESP_ERROR_CHECK(i2c_master_write_byte(cmd, reg, true));
    ESP_ERROR_CHECK(i2c_master_write_byte(cmd, data, true));
    ESP_ERROR_CHECK(i2c_master_stop(cmd));

    esp_err_t ret = i2c_master_cmd_begin(i2c_num, cmd, 1000 / portTICK_RATE_MS);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2c_master_cmd_begin error: [%02X] %s (%d)",
                device_id, esp_err_to_name(ret), ret);
    }

    i2c_cmd_link_delete(cmd);

    return ret;
}
