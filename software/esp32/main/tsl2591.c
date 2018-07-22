#include "tsl2591.h"

#include <esp_err.h>
#include <esp_log.h>
#include <driver/i2c.h>

#include "i2c_util.h"

static const char *TAG = "tsl2591";

/* I2C device address */
#define TSL2591_ADDRESS  0x29

/* Registers */
#define TSL2591_ENABLE  0x00 // Enables states and interrupts
#define TSL2591_CONFIG  0x01 // ALS gain and integration time configuration
#define TSL2591_AILTL   0x04 // ALS interrupt low threshold low byte
#define TSL2591_AILTH   0x05 // ALS interrupt low threshold high byte
#define TSL2591_AIHTL   0x06 // ALS interrupt high threshold low byte
#define TSL2591_AIHTH   0x07 // ALS interrupt high threshold high byte
#define TSL2591_NPAILTL 0x08 // No Persist ALS interrupt low threshold low byte
#define TSL2591_NPAILTH 0x09 // No Persist ALS interrupt low threshold high byte
#define TSL2591_NPAIHTL 0x0A // No Persist ALS interrupt high threshold low byte
#define TSL2591_NPAIHTH 0x0B // No Persist ALS interrupt high threshold high byte
#define TSL2591_PERSIST 0x0C // Interrupt persistence filter
#define TSL2591_PID     0x11 // Package
#define TSL2591_ID      0x12 // Device ID
#define TSL2591_STATUS  0x13 // Device status
#define TSL2591_C0DATAL 0x14 // CH0 ADC low data byte
#define TSL2591_C0DATAH 0x15 // CH0 ADC high data byte
#define TSL2591_C1DATAL 0x16 // CH1 ADC low data byte
#define TSL2591_C1DATAH 0x17 // CH1 ADC high data byte

/* Command Register Values */
#define TSL2591_CMD_NORMAL                   0xA0 // Normal operation
#define TSL2591_CMD_INT_FORCE                0xE4 // Interrupt set - forces an interrupt
#define TSL2591_CMD_INT_CLEAR_ALS            0xE6 // Clears ALS interrupt
#define TSL2591_CMD_INT_CLEAR_ALS_NO_PERSIST 0xE7 // Clears ALS and no persist ALS interrupt
#define TSL2591_CMD_INT_CLEAR_NO_PERSIST     0xEA // Clears no persist ALS interrupt

/* Enable Register Values */
#define TSL2591_ENABLE_NPIEN 0x80 // No persist interrupt enable
#define TSL2591_ENABLE_SAI   0x40 // Sleep after interrupt
#define TSL2591_ENABLE_AIEN  0x10 // ALS interrupt enable
#define TSL2591_ENABLE_AEN   0x02 // ALS enable
#define TSL2591_ENABLE_PON   0x01 // Power ON

esp_err_t tsl2591_init(i2c_port_t i2c_num)
{
    esp_err_t ret;
    uint8_t data;

    ESP_LOGI(TAG, "Initializing TSL2591");

    ret = i2c_read_register(i2c_num, TSL2591_ADDRESS, TSL2591_CMD_NORMAL | TSL2591_PID, &data);
    if (ret != ESP_OK) {
        return ret;
    }

    ESP_LOGI(TAG, "Package: %02X", data);

    ret = i2c_read_register(i2c_num, TSL2591_ADDRESS, TSL2591_CMD_NORMAL | TSL2591_ID, &data);
    if (ret != ESP_OK) {
        return ret;
    }

    ESP_LOGI(TAG, "Device ID: %02X", data);

    if (data != 0x50) {
        ESP_LOGE(TAG, "Invalid Device ID");
        return ESP_ERR_NOT_FOUND;
    }

    ret = i2c_read_register(i2c_num, TSL2591_ADDRESS, TSL2591_CMD_NORMAL | TSL2591_STATUS, &data);
    if (ret != ESP_OK) {
        return ret;
    }

    ESP_LOGI(TAG, "Status: %02X", data);

    // Power on the sensor
    ret = tsl2591_enable(i2c_num);
    if (ret != ESP_OK) {
        return ret;
    }

    // Set default integration and gain
    ret = tsl2591_set_config(i2c_num, TSL2591_GAIN_MEDIUM, TSL2591_TIME_300MS);
    if (ret != ESP_OK) {
        return ret;
    }

    // Power off the sensor
    ret = tsl2591_disable(i2c_num);
    if (ret != ESP_OK) {
        return ret;
    }

    ESP_LOGI(TAG, "TSL2591 Initialized");

    return ESP_OK;
}

esp_err_t tsl2591_enable(i2c_port_t i2c_num)
{
    return i2c_write_register(i2c_num, TSL2591_ADDRESS, TSL2591_CMD_NORMAL | TSL2591_ENABLE,
            TSL2591_ENABLE_PON | TSL2591_ENABLE_AEN);
}

esp_err_t tsl2591_disable(i2c_port_t i2c_num)
{
    return i2c_write_register(i2c_num, TSL2591_ADDRESS, TSL2591_CMD_NORMAL | TSL2591_ENABLE,
            0x00);
}

esp_err_t tsl2591_set_config(i2c_port_t i2c_num, tsl2591_gain_t gain, tsl2591_time_t time)
{
    if (gain < TSL2591_GAIN_LOW || gain > TSL2591_GAIN_MAXIMUM) {
        return ESP_ERR_INVALID_ARG;
    }
    if (time < TSL2591_TIME_100MS || time > TSL2591_TIME_600MS) {
        return ESP_ERR_INVALID_ARG;
    }

    return i2c_write_register(i2c_num, TSL2591_ADDRESS, TSL2591_CMD_NORMAL | TSL2591_CONFIG,
            ((uint8_t)(gain & 0x03) << 4) | ((uint8_t)(time & 0x07)));
}

esp_err_t tsl2591_get_config(i2c_port_t i2c_num, tsl2591_gain_t *gain, tsl2591_time_t *time)
{
    esp_err_t ret = ESP_OK;
    uint8_t data;

    ret = i2c_read_register(i2c_num, TSL2591_ADDRESS, TSL2591_CMD_NORMAL | TSL2591_CONFIG, &data);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2c_read_register error: %d", ret);
        return ret;
    }

    if (gain) {
        *gain = (data & 0x30) >> 4;
    }
    if (time) {
        *time = data & 0x07;
    }

    return ret;
}

esp_err_t tsl2591_get_status_valid(i2c_port_t i2c_num, bool *valid)
{
    if (!valid) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = ESP_OK;
    uint8_t data;

    ret = i2c_read_register(i2c_num, TSL2591_ADDRESS, TSL2591_CMD_NORMAL | TSL2591_STATUS, &data);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2c_read_register error: %d", ret);
        return ret;
    }

    *valid = (data & 0x01) ? true : false;

    return ret;
}

esp_err_t tsl2591_get_full_channel_data(i2c_port_t i2c_num, uint16_t *ch0_val, uint16_t *ch1_val)
{
    esp_err_t ret;
    uint8_t data[4];

    ret = i2c_write_byte(i2c_num, TSL2591_ADDRESS, TSL2591_CMD_NORMAL | TSL2591_C0DATAL);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = i2c_read_buffer(i2c_num, TSL2591_ADDRESS, data, sizeof(data));
    if (ret != ESP_OK) {
        return ret;
    }

    // Channel 0 - visible + infrared
    if (ch0_val) {
        *ch0_val = data[0] | data[1] << 8;
    }

    // Channel 1 - infrared only
    if (ch1_val) {
        *ch1_val = data[2] | data[3] << 8;
    }

    return ESP_OK;
}
