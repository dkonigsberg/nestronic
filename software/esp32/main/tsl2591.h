/*
 * TSL2591 - Light-to-Digital Converter with I2C Interface
 */

#ifndef TSL2591_H
#define TSL2591_H

#include <esp_err.h>
#include <driver/i2c.h>

typedef enum {
    TSL2591_GAIN_LOW = 0,
    TSL2591_GAIN_MEDIUM = 1,
    TSL2591_GAIN_HIGH = 2,
    TSL2591_GAIN_MAXIMUM = 3
} tsl2591_gain_t;

typedef enum {
    TSL2591_TIME_100MS = 0,
    TSL2591_TIME_200MS = 1,
    TSL2591_TIME_300MS = 2,
    TSL2591_TIME_400MS = 3,
    TSL2591_TIME_500MS = 4,
    TSL2591_TIME_600MS = 5
} tsl2591_time_t;

esp_err_t tsl2591_init(i2c_port_t i2c_num);
esp_err_t tsl2591_enable(i2c_port_t i2c_num);
esp_err_t tsl2591_disable(i2c_port_t i2c_num);

esp_err_t tsl2591_set_config(i2c_port_t i2c_num, tsl2591_gain_t gain, tsl2591_time_t time);
esp_err_t tsl2591_get_config(i2c_port_t i2c_num, tsl2591_gain_t *gain, tsl2591_time_t *time);

esp_err_t tsl2591_get_status_valid(i2c_port_t i2c_num, bool *valid);

esp_err_t tsl2591_get_full_channel_data(i2c_port_t i2c_num, uint16_t *ch0_val, uint16_t *ch1_val);

#endif /* TSL2591_H */
