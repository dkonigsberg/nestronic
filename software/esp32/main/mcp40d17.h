/*
 * MCP40D17 - 7-Bit Single I2C Digital Rheostat
 */

#ifndef MCP40D17
#define MCP40D17

#include <esp_err.h>
#include <driver/i2c.h>

esp_err_t mcp40d17_init(i2c_port_t i2c_num);

esp_err_t mcp40d17_set_wiper(i2c_port_t i2c_num, uint8_t value);
esp_err_t mcp40d17_get_wiper(i2c_port_t i2c_num, uint8_t *value);

#endif /* MCP40D17_H */
