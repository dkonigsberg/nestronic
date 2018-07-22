#ifndef I2C_UTIL_H
#define I2C_UTIL_H

#include <esp_err.h>
#include <driver/i2c.h>

esp_err_t i2c_init_master_port0();
esp_err_t i2c_init_master_port1();

void i2c_mutex_lock(i2c_port_t port);
void i2c_mutex_unlock(i2c_port_t port);

void i2c_bus_scan(i2c_port_t port);

esp_err_t i2c_read_byte(i2c_port_t i2c_num, uint8_t device_id, uint8_t *data);
esp_err_t i2c_write_byte(i2c_port_t i2c_num, uint8_t device_id, uint8_t data);

esp_err_t i2c_read_buffer(i2c_port_t i2c_num, uint8_t device_id, uint8_t *data, size_t data_len);
esp_err_t i2c_write_buffer(i2c_port_t i2c_num, uint8_t device_id, uint8_t *data, size_t data_len);

esp_err_t i2c_read_register(i2c_port_t i2c_num, uint8_t device_id, uint8_t reg, uint8_t *data);
esp_err_t i2c_write_register(i2c_port_t i2c_num, uint8_t device_id, uint8_t reg, uint8_t data);

#endif /* I2C_UTIL_H */
