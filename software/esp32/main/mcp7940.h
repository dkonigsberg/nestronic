/*
 * MCP7940N - Battery-Backed I2C Real-Time Clock/Calendar with SRAM
 */

#ifndef MCP7940_H
#define MCP7940_H

#include <esp_err.h>
#include <driver/i2c.h>
#include <time.h>

typedef enum {
    MCP7940_ALARM_0 = 0,
    MCP7940_ALARM_1,
    MCP7940_ALARM_MAX
} mcp7940_alarm_t;

typedef enum {
    MCP7940_ALARM_MASK_SECONDS = 0,
    MCP7940_ALARM_MASK_MINUTES,
    MCP7940_ALARM_MASK_HOURS,
    MCP7940_ALARM_MASK_WDAY,
    MCP7940_ALARM_MASK_DATE,
    MCP7940_ALARM_MASK_RESERVED1,
    MCP7940_ALARM_MASK_RESERVED2,
    MCP7940_ALARM_MASK_ALL
} mcp7940_alarm_mask_t;

typedef enum {
    MCP7940_SW_FREQ_1HZ = 0,
    MCP7940_SW_FREQ_4KHZ,
    MCP7940_SW_FREQ_8KHZ,
    MCP7940_SW_FREQ_32KHZ,
    MCP7940_SW_FREQ_MAX
} mcp7940_sw_freq_t;

esp_err_t mcp7940_init(i2c_port_t i2c_num);

esp_err_t mcp7940_set_oscillator_enabled(i2c_port_t i2c_num, bool enabled);
esp_err_t mcp7940_set_external_oscillator_enabled(i2c_port_t i2c_num, bool enabled);
esp_err_t mcp7940_set_battery_enabled(i2c_port_t i2c_num, bool enabled);
esp_err_t mcp7940_is_oscillator_running(i2c_port_t i2c_num, bool *running);

esp_err_t mcp7940_read_power_failure(i2c_port_t i2c_num, bool *failed, struct tm *tm_down, struct tm *tm_up);

esp_err_t mcp7940_set_time(i2c_port_t i2c_num, const struct tm *tm);
esp_err_t mcp7940_get_time(i2c_port_t i2c_num, struct tm *tm);

esp_err_t mcp7940_set_alarm_enabled(i2c_port_t i2c_num, mcp7940_alarm_t alarm, bool enabled);
esp_err_t mcp7940_get_alarm_enabled(i2c_port_t i2c_num, mcp7940_alarm_t alarm, bool *enabled);

esp_err_t mcp7940_set_alarm_time(i2c_port_t i2c_num, mcp7940_alarm_t alarm, const struct tm *tm);
esp_err_t mcp7940_get_alarm_time(i2c_port_t i2c_num, mcp7940_alarm_t alarm, struct tm *tm);

esp_err_t mcp7940_set_alarm_mask(i2c_port_t i2c_num, mcp7940_alarm_t alarm, mcp7940_alarm_mask_t mask);
esp_err_t mcp7940_get_alarm_mask(i2c_port_t i2c_num, mcp7940_alarm_t alarm, mcp7940_alarm_mask_t *mask);

esp_err_t mcp7940_has_alarm_occurred(i2c_port_t i2c_num, mcp7940_alarm_t alarm, bool *alarm_occurred);
esp_err_t mcp7940_clear_alarm_occurred(i2c_port_t i2c_num, mcp7940_alarm_t alarm);

esp_err_t mcp7940_set_alarm_polarity(i2c_port_t i2c_num, bool high);
esp_err_t mcp7940_get_alarm_polarity(i2c_port_t i2c_num, bool *high);

esp_err_t mcp7940_set_square_wave(i2c_port_t i2c_num, bool enabled, mcp7940_sw_freq_t freq);
esp_err_t mcp7940_get_square_wave(i2c_port_t i2c_num, bool *enabled, mcp7940_sw_freq_t *freq);

esp_err_t mcp7940_set_coarse_trim_enabled(i2c_port_t i2c_num, bool enabled);
esp_err_t mcp7940_get_coarse_trim_enabled(i2c_port_t i2c_num, bool *enabled);

esp_err_t mcp7940_set_trim_value(i2c_port_t i2c_num, bool sign, uint8_t value);
esp_err_t mcp7940_get_trim_value(i2c_port_t i2c_num, bool *sign, uint8_t *value);

#endif /* MCP7940_H */
