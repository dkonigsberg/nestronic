#include "mcp7940.h"

#include <esp_err.h>
#include <esp_log.h>
#include <driver/i2c.h>

#include <strings.h>

#include "i2c_util.h"

static const char *TAG = "mcp7940";

/* I2C device address */
#define MCP7940_ADDRESS  0x6F

/*
 * Timekeeping registers
 */
#define MCP7940_RTCSEC     0x00
#define MCP7940_RTCMIN     0x01
#define MCP7940_RTCHOUR    0x02
#define MCP7940_RTCWKDAY   0x03
#define MCP7940_RTCDATE    0x04
#define MCP7940_RTCMTH     0x05
#define MCP7940_RTCYEAR    0x06
#define MCP7940_CONTROL    0x07
#define MCP7940_OSCTRIM    0x08

/*
 * Alarm 0 registers
 */
#define MCP7940_ALM0SEC    0x0A
#define MCP7940_ALM0MIN    0x0B
#define MCP7940_ALM0HOUR   0x0C
#define MCP7940_ALM0WKDAY  0x0D
#define MCP7940_ALM0DATE   0x0E
#define MCP7940_ALM0MTH    0x0F

/*
 * Alarm 1 registers
 */
#define MCP7940_ALM1SEC    0x11
#define MCP7940_ALM1MIN    0x12
#define MCP7940_ALM1HOUR   0x13
#define MCP7940_ALM1WKDAY  0x14
#define MCP7940_ALM1DATE   0x15
#define MCP7940_ALM1MTH    0x16

/*
 * Power-Fail Timestamp registers
 */
#define MCP7940_PWRUPMIN   0x1C
#define MCP7940_PWRUPHOUR  0x1D
#define MCP7940_PWRUPDATE  0x1E
#define MCP7940_PWRUPMTH   0x1F

/*
 * Other
 */
#define MCP7940_SRAM       0x20

static esp_err_t mcp7940_write(i2c_port_t i2c_num, uint8_t *data, size_t data_len);
static esp_err_t mcp7940_read(i2c_port_t i2c_num, uint8_t *data, size_t data_len);
static esp_err_t mcp7940_set_bits(i2c_port_t i2c_num, uint8_t reg, uint8_t mask, uint8_t value);

#define UINT_TO_BCD(v) ((((v) / 10) << 4) | ((v) % 10))

static bool is_leap_year(int year);

esp_err_t mcp7940_write(i2c_port_t i2c_num, uint8_t *data, size_t data_len)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    if (!cmd) {
        ESP_LOGE(TAG, "i2c_cmd_link_create error");
        return ESP_ERR_NO_MEM;
    }

    ESP_ERROR_CHECK(i2c_master_start(cmd));
    ESP_ERROR_CHECK(i2c_master_write_byte(cmd, MCP7940_ADDRESS << 1 | I2C_MASTER_WRITE, true));
    ESP_ERROR_CHECK(i2c_master_write(cmd, data, data_len, true));
    ESP_ERROR_CHECK(i2c_master_stop(cmd));

    esp_err_t ret = i2c_master_cmd_begin(i2c_num, cmd, 1000 / portTICK_RATE_MS);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2c_master_cmd_begin error: %d", ret);
    }

    i2c_cmd_link_delete(cmd);

    return ret;
}

esp_err_t mcp7940_read(i2c_port_t i2c_num, uint8_t *data, size_t data_len)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    if (!cmd) {
        ESP_LOGE(TAG, "i2c_cmd_link_create error");
        return ESP_ERR_NO_MEM;
    }

    ESP_ERROR_CHECK(i2c_master_start(cmd));
    ESP_ERROR_CHECK(i2c_master_write_byte(cmd, MCP7940_ADDRESS << 1 | I2C_MASTER_READ, true));

    if (data_len > 1) {
        ESP_ERROR_CHECK(i2c_master_read(cmd, data, data_len - 1, false));
    }
    if (data_len > 0) {
        ESP_ERROR_CHECK(i2c_master_read_byte(cmd, data + (data_len - 1), true));
    }

    ESP_ERROR_CHECK(i2c_master_stop(cmd));

    esp_err_t ret = i2c_master_cmd_begin(i2c_num, cmd, 1000 / portTICK_RATE_MS);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2c_master_cmd_begin error: %d", ret);
    }

    i2c_cmd_link_delete(cmd);

    return ret;
}

esp_err_t mcp7940_set_bits(i2c_port_t i2c_num, uint8_t reg, uint8_t mask, uint8_t value)
{
    esp_err_t ret = ESP_OK;
    uint8_t prev_data;
    uint8_t data[2];

    /* Read the previous value of the register */
    ret = i2c_read_register(i2c_num, MCP7940_ADDRESS, reg, &prev_data);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2c_read_register error: %d", ret);
        return ret;
    }


    /* Update the value based on the parameters */
    data[0] = reg;
    data[1] = (prev_data & ~mask) | (value & mask);

    /* Return here if nothing changed */
    if (prev_data == data[1]) {
        return ESP_OK;
    }

    /* Set the new value of the register */
    ret = mcp7940_write(i2c_num, data, sizeof(data));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "mcp7940_write error: %d", ret);
        return ret;
    }

    return ret;
}

esp_err_t mcp7940_init(i2c_port_t i2c_num)
{
    esp_err_t ret = ESP_OK;

    ret = mcp7940_set_oscillator_enabled(i2c_num, true);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = mcp7940_set_external_oscillator_enabled(i2c_num, false);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = mcp7940_set_battery_enabled(i2c_num, true);
    if (ret != ESP_OK) {
        return ret;
    }

    return ret;
}

esp_err_t mcp7940_set_oscillator_enabled(i2c_port_t i2c_num, bool enabled)
{
    return mcp7940_set_bits(i2c_num, MCP7940_RTCSEC, 0x80, enabled << 7);
}

esp_err_t mcp7940_set_external_oscillator_enabled(i2c_port_t i2c_num, bool enabled)
{
    return mcp7940_set_bits(i2c_num, MCP7940_CONTROL, 0x40, enabled << 3);
}

esp_err_t mcp7940_set_battery_enabled(i2c_port_t i2c_num, bool enabled)
{
    return mcp7940_set_bits(i2c_num, MCP7940_RTCWKDAY, 0x08, enabled << 3);
}

esp_err_t mcp7940_is_oscillator_running(i2c_port_t i2c_num, bool *running)
{
    esp_err_t ret = ESP_OK;
    uint8_t data;

    if (!running) {
        return ESP_ERR_INVALID_ARG;
    }

    ret = i2c_read_register(i2c_num, MCP7940_ADDRESS, MCP7940_RTCWKDAY, &data);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2c_read_register error: %d", ret);
        return ret;
    }

    *running = (data & 0x20) == 0x20;

    return ret;
}
esp_err_t mcp7940_has_power_failed(i2c_port_t i2c_num, bool *failed)
{
    esp_err_t ret = ESP_OK;
    uint8_t data;

    if (!failed) {
        return ESP_ERR_INVALID_ARG;
    }

    ret = i2c_read_register(i2c_num, MCP7940_ADDRESS, MCP7940_RTCWKDAY, &data);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2c_read_register error: %d", ret);
        return ret;
    }

    *failed = (data & 0x10) == 0x10;

    return ret;
}

esp_err_t mcp7940_clear_power_failed(i2c_port_t i2c_num)
{
    return mcp7940_set_bits(i2c_num, MCP7940_RTCWKDAY, 0x10, 0);
}

esp_err_t mcp7940_set_time(i2c_port_t i2c_num, const struct tm *tm)
{
    esp_err_t ret = ESP_OK;
    uint8_t data[8];
    uint8_t val;

    if (!tm) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Select the base of the timekeeping registers */
    ret = i2c_write_byte(i2c_num, MCP7940_ADDRESS, MCP7940_RTCSEC);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2c_write_byte error: %d", ret);
        return ret;
    }

    /* Read all 7 relevant registers in one operation */
    ret = mcp7940_read(i2c_num, &data[1], sizeof(data) - 1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "mcp7940_read error: %d", ret);
        return ret;
    }

    /* Timekeeping register base */
    data[0] = MCP7940_RTCSEC;

    /* Clear ST and EXTOSC to avoid rollover issues */
    ret = mcp7940_set_bits(i2c_num, MCP7940_RTCSEC, 0x80, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "mcp7940_set_bits error: %d", ret);
        return ret;
    }
    ret = mcp7940_set_bits(i2c_num, MCP7940_CONTROL, 0x40, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "mcp7940_set_bits error: %d", ret);
        return ret;
    }

    /* Wait for the OSCRUN bit to clear */
    do {
        ret = i2c_read_register(i2c_num, MCP7940_ADDRESS, MCP7940_RTCWKDAY, &val);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "i2c_read_register error: %d", ret);
            return ret;
        }
    } while ((val & 0x20) == 0x20);

    /* Clear all non-flag fields */
    data[1] &= 0x80;
    data[2] &= 0x80;
    data[3] &= 0x80; /* Make sure we are in 24-hour mode */
    data[4] &= 0xF8;
    data[5] &= 0xC0;
    data[6] &= 0xE0;
    data[7] &= 0x00;

    /* Seconds (0-60) */
    data[1] |= UINT_TO_BCD(tm->tm_sec) & 0x7F;

    /* Minutes (0-59) */
    data[2] |= UINT_TO_BCD(tm->tm_min) & 0x7F;

    /* Hours (0-23), always set in 24-hour format */
    data[3] |= UINT_TO_BCD(tm->tm_hour) & 0x3F;

    /* Day of the week (0-6, Sunday = 0) */
    data[4] |= UINT_TO_BCD(tm->tm_wday + 1) & 0x07;

    /* Day of the month (1-31) */
    data[5] |= UINT_TO_BCD(tm->tm_mday) & 0x3F;

    /* Month (0-11) */
    data[6] |= UINT_TO_BCD(tm->tm_mon + 1) & 0x1F;

    /* Leap year flag */
    data[6] |= is_leap_year(tm->tm_year) ? 0x20 : 0x00;

    /* Year - 1900 */
    data[7] |= UINT_TO_BCD(tm->tm_year - 100);

    ret = mcp7940_write(i2c_num, data, sizeof(data));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "mcp7940_write error: %d", ret);
        return ret;
    }

    return ret;
}

esp_err_t mcp7940_get_time(i2c_port_t i2c_num, struct tm *tm)
{
    esp_err_t ret = ESP_OK;
    uint8_t data[7];

    if (!tm) {
        return ESP_ERR_INVALID_ARG;
    }

    /* Select the base of the timekeeping registers */
    ret = i2c_write_byte(i2c_num, MCP7940_ADDRESS, MCP7940_RTCSEC);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "mcp7940_write_byte error: %d", ret);
        return ret;
    }

    /* Read all 7 relevant registers in one operation */
    ret = mcp7940_read(i2c_num, data, sizeof(data));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "mcp7940_read error: %d", ret);
        return ret;
    }

    /* Populate the time structure based on the results */
    bzero(tm, sizeof(struct tm));

    /* Seconds (0-60) */
    tm->tm_sec = (((data[0] & 0x70) >> 4) * 10) + (data[0] & 0x0F);

    /* Minutes (0-59) */
    tm->tm_min = (((data[1] & 0x70) >> 4) * 10) + (data[1] & 0x0F);

    /* Hours (0-23) */
    if ((data[2] & 0x40) == 0x40) {
        /* 12 hour time */
        tm->tm_hour = (((data[2] & 0x10) >> 4) * 10) + (data[2] & 0x0F);
        if ((data[2] & 0x20) == 0x20) {
            /* PM */
            if (tm->tm_hour < 12) {
                tm->tm_hour += 12;
            }
        } else {
            /* AM */
            if (tm->tm_hour == 12) {
                tm->tm_hour = 0;
            }
        }
    } else {
        /* 24 hour time */
        tm->tm_hour = (((data[2] & 0x30) >> 4) * 10) + (data[2] & 0x0F);
    }

    /* Day of the week (0-6, Sunday = 0) */
    tm->tm_wday = (data[3] & 0x07) - 1;

    /* Day of the month (1-31) */
    tm->tm_mday = (((data[4] & 0x30) >> 4) * 10) + (data[4] & 0x0F);

    /* Month (0-11) */
    tm->tm_mon = (((data[5] & 0x10) >> 4) * 10) + (data[5] & 0x0F) - 1;

    /* Year - 1900 */
    tm->tm_year = (((data[6] & 0xF0) >> 4) * 10) + (data[6] & 0x0F) + 100;

    return ret;
}

esp_err_t mcp7940_set_alarm_enabled(i2c_port_t i2c_num, mcp7940_alarm_t alarm, bool enabled)
{
    esp_err_t ret = ESP_OK;

    if (alarm < 0 || alarm >= MCP7940_ALARM_MAX) {
        return ESP_ERR_INVALID_ARG;
    }

    if (alarm == MCP7940_ALARM_0) {
        ret = mcp7940_set_bits(i2c_num, MCP7940_CONTROL, 0x10, enabled << 4);
    } else {
        ret = mcp7940_set_bits(i2c_num, MCP7940_CONTROL, 0x20, enabled << 5);
    }

    return ret;
}

esp_err_t mcp7940_get_alarm_enabled(i2c_port_t i2c_num, mcp7940_alarm_t alarm, bool *enabled)
{
    if (alarm < 0 || alarm >= MCP7940_ALARM_MAX || !enabled) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = ESP_OK;
    uint8_t data;

    const uint8_t mask = (alarm == MCP7940_ALARM_0) ? 0x10 : 0x20;

    ret = i2c_read_register(i2c_num, MCP7940_ADDRESS, MCP7940_CONTROL, &data);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2c_read_register error: %d", ret);
        return ret;
    }

    *enabled = (data & mask) == mask;

    return ret;
}

esp_err_t mcp7940_set_alarm_time(i2c_port_t i2c_num, mcp7940_alarm_t alarm, const struct tm *tm)
{
    esp_err_t ret = ESP_OK;
    uint8_t data[7];

    if (alarm < 0 || alarm >= MCP7940_ALARM_MAX || !tm) {
        return ESP_ERR_INVALID_ARG;
    }

    const uint8_t reg = (alarm == MCP7940_ALARM_0) ? MCP7940_ALM0SEC : MCP7940_ALM1SEC;

    /* Select the base of the alarm registers */
    ret = i2c_write_byte(i2c_num, MCP7940_ADDRESS, reg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2c_write_byte error: %d", ret);
        return ret;
    }

    /* Read all 6 relevant registers in one operation */
    ret = mcp7940_read(i2c_num, &data[1], sizeof(data) - 1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "mcp7940_read error: %d", ret);
        return ret;
    }

    /* Clear all non-flag fields */
    data[1] &= 0x80;
    data[2] &= 0x80;
    data[3] &= 0x80;
    data[4] &= 0xF8;
    data[5] &= 0xC0;
    data[6] &= 0xC0;

    /* Alarm register base */
    data[0] = reg;

    /* Seconds (0-60) */
    data[1] |= UINT_TO_BCD(tm->tm_sec) & 0x7F;

    /* Minutes (0-59) */
    data[2] |= UINT_TO_BCD(tm->tm_min) & 0x7F;

    /* Hours (0-23), always set in 24-hour format */
    data[3] |= UINT_TO_BCD(tm->tm_hour) & 0x3F;

    /* Day of the week (0-6, Sunday = 0) */
    data[4] |= UINT_TO_BCD(tm->tm_wday + 1) & 0x07;

    /* Day of the month (1-31) */
    data[5] |= UINT_TO_BCD(tm->tm_mday) & 0x3F;

    /* Month (0-11) */
    data[6] |= UINT_TO_BCD(tm->tm_mon + 1) & 0x1F;

    ret = mcp7940_write(i2c_num, data, sizeof(data));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "mcp7940_write error: %d", ret);
        return ret;
    }

    return ret;
}

esp_err_t mcp7940_get_alarm_time(i2c_port_t i2c_num, mcp7940_alarm_t alarm, struct tm *tm)
{
    esp_err_t ret = ESP_OK;
    uint8_t data[6];

    if (alarm < 0 || alarm >= MCP7940_ALARM_MAX || !tm) {
        return ESP_ERR_INVALID_ARG;
    }

    const uint8_t reg = (alarm == MCP7940_ALARM_0) ? MCP7940_ALM0SEC : MCP7940_ALM1SEC;

    /* Select the base of the alarm registers */
    ret = i2c_write_byte(i2c_num, MCP7940_ADDRESS, reg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "mcp7940_write_byte error: %d", ret);
        return ret;
    }

    /* Read all 6 relevant registers in one operation */
    ret = mcp7940_read(i2c_num, data, sizeof(data));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "mcp7940_read error: %d", ret);
        return ret;
    }

    /* Populate the time structure based on the results */
    bzero(tm, sizeof(struct tm));

    /* Seconds (0-60) */
    tm->tm_sec = (((data[0] & 0x70) >> 4) * 10) + (data[0] & 0x0F);

    /* Minutes (0-59) */
    tm->tm_min = (((data[1] & 0x70) >> 4) * 10) + (data[1] & 0x0F);

    /* Hours (0-23) */
    if ((data[2] & 0x40) == 0x40) {
        /* 12 hour time */
        tm->tm_hour = (((data[2] & 0x10) >> 4) * 10) + (data[2] & 0x0F);
        if ((data[2] & 0x20) == 0x20) {
            /* PM */
            if (tm->tm_hour < 12) {
                tm->tm_hour += 12;
            }
        } else {
            /* AM */
            if (tm->tm_hour == 12) {
                tm->tm_hour = 0;
            }
        }
    } else {
        /* 24 hour time */
        tm->tm_hour = (((data[2] & 0x30) >> 4) * 10) + (data[2] & 0x0F);
    }

    /* Day of the week (0-6, Sunday = 0) */
    tm->tm_wday = (data[3] & 0x07) - 1;

    /* Day of the month (1-31) */
    tm->tm_mday = (((data[4] & 0x30) >> 4) * 10) + (data[4] & 0x0F);

    /* Month (0-11) */
    tm->tm_mon = (((data[5] & 0x10) >> 4) * 10) + (data[5] & 0x0F) - 1;

    return ret;
}

esp_err_t mcp7940_set_alarm_mask(i2c_port_t i2c_num, mcp7940_alarm_t alarm, mcp7940_alarm_mask_t mask)
{
    if (alarm < 0 || alarm >= MCP7940_ALARM_MAX) {
        return ESP_ERR_INVALID_ARG;
    }

    const uint8_t reg = (alarm == MCP7940_ALARM_0) ? MCP7940_ALM0WKDAY : MCP7940_ALM1WKDAY;
    return mcp7940_set_bits(i2c_num, reg, 0x70, (uint8_t)mask << 4);
}

esp_err_t mcp7940_get_alarm_mask(i2c_port_t i2c_num, mcp7940_alarm_t alarm, mcp7940_alarm_mask_t *mask)
{
    esp_err_t ret = ESP_OK;
    uint8_t data;

    if (alarm < 0 || alarm >= MCP7940_ALARM_MAX || !mask) {
        return ESP_ERR_INVALID_ARG;
    }

    const uint8_t reg = (alarm == MCP7940_ALARM_0) ? MCP7940_ALM0WKDAY : MCP7940_ALM1WKDAY;
    ret = i2c_read_register(i2c_num, MCP7940_ADDRESS, reg, &data);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2c_read_register error: %d", ret);
        return ret;
    }

    *mask = (data & 0x70) >> 4;

    return ret;
}

esp_err_t mcp7940_has_alarm_occurred(i2c_port_t i2c_num, mcp7940_alarm_t alarm, bool *alarm_occurred)
{
    esp_err_t ret = ESP_OK;
    uint8_t data;

    if (alarm < 0 || alarm >= MCP7940_ALARM_MAX || !alarm_occurred) {
        return ESP_ERR_INVALID_ARG;
    }

    const uint8_t reg = (alarm == MCP7940_ALARM_0) ? MCP7940_ALM0WKDAY : MCP7940_ALM1WKDAY;
    ret = i2c_read_register(i2c_num, MCP7940_ADDRESS, reg, &data);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2c_read_register error: %d", ret);
        return ret;
    }

    *alarm_occurred = (data & 0x08) == 0x08;

    return ret;
}

esp_err_t mcp7940_clear_alarm_occurred(i2c_port_t i2c_num, mcp7940_alarm_t alarm)
{
    if (alarm < 0 || alarm >= MCP7940_ALARM_MAX) {
        return ESP_ERR_INVALID_ARG;
    }

    const uint8_t reg = (alarm == MCP7940_ALARM_0) ? MCP7940_ALM0WKDAY : MCP7940_ALM1WKDAY;
    return mcp7940_set_bits(i2c_num, reg, 0x08, 0);
}

esp_err_t mcp7940_set_alarm_polarity(i2c_port_t i2c_num, bool high)
{
    return mcp7940_set_bits(i2c_num, MCP7940_ALM0WKDAY, 0x80, high << 7);
}

esp_err_t mcp7940_get_alarm_polarity(i2c_port_t i2c_num, bool *high)
{
    esp_err_t ret = ESP_OK;
    uint8_t data;

    if (!high) {
        return ESP_ERR_INVALID_ARG;
    }

    ret = i2c_read_register(i2c_num, MCP7940_ADDRESS, MCP7940_ALM0WKDAY, &data);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2c_read_register error: %d", ret);
        return ret;
    }

    *high = (data & 0x80) == 0x80;

    return ret;
}

esp_err_t mcp7940_set_square_wave(i2c_port_t i2c_num, bool enabled, mcp7940_sw_freq_t freq)
{
    if (freq < 0 || freq >= MCP7940_SW_FREQ_MAX) {
        return ESP_ERR_INVALID_ARG;
    }

    return mcp7940_set_bits(i2c_num, MCP7940_CONTROL, 0x43, enabled << 6 | (freq & 0x03));
}

esp_err_t mcp7940_get_square_wave(i2c_port_t i2c_num, bool *enabled, mcp7940_sw_freq_t *freq)
{
    esp_err_t ret = ESP_OK;
    uint8_t data;

    if (!enabled || !freq) {
        return ESP_ERR_INVALID_ARG;
    }

    ret = i2c_read_register(i2c_num, MCP7940_ADDRESS, MCP7940_CONTROL, &data);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2c_read_register error: %d", ret);
        return ret;
    }

    *enabled = (data & 0x40) == 0x40;
    *freq = data & 0x03;

    return ret;
}

esp_err_t mcp7940_set_coarse_trim_enabled(i2c_port_t i2c_num, bool enabled)
{
    return mcp7940_set_bits(i2c_num, MCP7940_CONTROL, 0x04, enabled << 2);
}

esp_err_t mcp7940_get_coarse_trim_enabled(i2c_port_t i2c_num, bool *enabled)
{
    esp_err_t ret = ESP_OK;
    uint8_t data;

    if (!enabled) {
        return ESP_ERR_INVALID_ARG;
    }

    ret = i2c_read_register(i2c_num, MCP7940_ADDRESS, MCP7940_CONTROL, &data);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2c_read_register error: %d", ret);
        return ret;
    }

    *enabled = (data & 0x04) == 0x04;

    return ret;
}

esp_err_t mcp7940_set_trim_value(i2c_port_t i2c_num, bool sign, uint8_t value)
{
    if (value > 0x7F) {
        return ESP_ERR_INVALID_ARG;
    }

    return mcp7940_set_bits(i2c_num, MCP7940_OSCTRIM, 0xFF, sign << 7 | (value & 0x7F));
}

esp_err_t mcp7940_get_trim_value(i2c_port_t i2c_num, bool *sign, uint8_t *value)
{
    esp_err_t ret = ESP_OK;
    uint8_t data;

    if (!sign || !value) {
        return ESP_ERR_INVALID_ARG;
    }

    ret = i2c_read_register(i2c_num, MCP7940_ADDRESS, MCP7940_OSCTRIM, &data);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "i2c_read_register error: %d", ret);
        return ret;
    }

    *sign = (data & 0x80) == 0x80;
    *value = data & 0x7F;

    return ret;
}

bool is_leap_year(int year)
{
    if ((year % 400) == 0) {
        return true;
    } else if ((year % 100) == 0) {
        return false;
    } else if ((year % 4) == 0) {
        return true;
    } else {
        return false;
    }
}
