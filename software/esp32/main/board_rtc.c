/*
 * External RTC Interface
 */

#include "board_rtc.h"

#include <esp_system.h>
#include <esp_err.h>
#include <esp_log.h>
#include <esp_types.h>
#include <string.h>
#include <time.h>

#include "board_config.h"
#include "i2c_util.h"
#include "mcp7940.h"
#include "settings.h"

static const char *TAG = "board_rtc";

#define RTC_TRIM_COARSE false
#define RTC_TRIM_VALUE  0
#define RTC_SRAM_OFFSET 6

board_rtc_alarm_cb_t board_rtc_alarm_cb = NULL;
static bool power_failed = false;
static struct tm tm_power_down = {0};
static struct tm tm_power_up = {0};

static esp_err_t board_rtc_init_sram();
static time_t board_rtc_timegm(struct tm *tm);

esp_err_t board_rtc_init()
{
    ESP_LOGI(TAG, "Initializing external RTC");

    bool trim_coarse;
    uint8_t trim_value;
    if (settings_get_rtc_trim(&trim_coarse, &trim_value) != ESP_OK) {
        ESP_LOGE(TAG, "Unable to load RTC trim values");
        trim_coarse = false;
        trim_value = 0;
    }

    // Configure the GPIO for RTC MFP
    gpio_config_t config = {
        .pin_bit_mask = 1ULL << MCP7940_MFP_PIN,
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE, // using external pull-up
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_PIN_INTR_NEGEDGE,
    };
    ESP_ERROR_CHECK(gpio_config(&config));

    i2c_mutex_lock(I2C_P0_NUM);

    // Enable the oscillator
    mcp7940_init(I2C_P0_NUM);

    // Check for power failure
    mcp7940_read_power_failure(I2C_P0_NUM, &power_failed, &tm_power_down, &tm_power_up);

    // Enable the battery
    mcp7940_set_battery_enabled(I2C_P0_NUM, true);

    // Set trim value
    mcp7940_set_coarse_trim_enabled(I2C_P0_NUM, trim_coarse);
    mcp7940_set_trim_value(I2C_P0_NUM, (trim_value & 0x80) == 0x80, trim_value & 0x7F);

    // Disable the square wave generator
    mcp7940_set_square_wave(I2C_P0_NUM, false, MCP7940_SW_FREQ_1HZ);

    // Disable the alarms
    mcp7940_set_alarm_enabled(I2C_P0_NUM, MCP7940_ALARM_0, false);
    mcp7940_set_alarm_enabled(I2C_P0_NUM, MCP7940_ALARM_1, false);

    // Set the mask for alarm 0
    mcp7940_set_alarm_mask(I2C_P0_NUM, MCP7940_ALARM_0, MCP7940_ALARM_MASK_SECONDS);

    // Set alarm polarity low
    mcp7940_set_alarm_polarity(I2C_P0_NUM, false);

    // Clear alarm flags
    mcp7940_clear_alarm_occurred(I2C_P0_NUM, MCP7940_ALARM_0);
    mcp7940_clear_alarm_occurred(I2C_P0_NUM, MCP7940_ALARM_1);

    // Set time for alarm 0
    // This should make it tick once per minute
    struct tm tm;
    bzero(&tm, sizeof(struct tm));
    mcp7940_set_alarm_time(I2C_P0_NUM, MCP7940_ALARM_0, &tm);

    // Enable alarm 0
    mcp7940_set_alarm_enabled(I2C_P0_NUM, MCP7940_ALARM_0, true);

    // Initialize the SRAM
    board_rtc_init_sram();

    i2c_mutex_unlock(I2C_P0_NUM);

    return ESP_OK;
}

esp_err_t board_rtc_init_sram()
{
    esp_err_t ret;

    // Get the factory MAC address, which is used to verify whether or not
    // the RTC SRAM needs to be initialized
    uint8_t sta_mac[6];
    ret = esp_efuse_mac_get_default(sta_mac);
    if (ret != ESP_OK) {
        return ret;
    }

    uint8_t sram_mac[6];
    ret = mcp7940_data_read(I2C_P0_NUM, 0, sram_mac, 6);
    if (ret != ESP_OK) {
        return ret;
    }

    if (memcmp(sta_mac, sram_mac, 6) != 0) {
        uint8_t data[64] = {0};
        ESP_LOGI(TAG, "Initializing RTC SRAM");
        memcpy(data, sta_mac, 6);
        ret = mcp7940_data_write(I2C_P0_NUM, 0, data, 64);
        if (ret != ESP_OK) {
            return ret;
        }
    } else {
        ESP_LOGI(TAG, "RTC SRAM has been initialized");
    }

    return ret;
}

esp_err_t board_rtc_calibration()
{
    ESP_LOGI(TAG, "Initializing external RTC calibration mode");

    // Configure the GPIO for RTC MFP
    gpio_config_t config = {
        .pin_bit_mask = 1ULL << MCP7940_MFP_PIN,
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_PIN_INTR_DISABLE
    };
    ESP_ERROR_CHECK(gpio_config(&config));

    i2c_mutex_lock(I2C_P0_NUM);

    // Enable the oscillator
    mcp7940_init(I2C_P0_NUM);

    // Set trim value to 0
    mcp7940_set_trim_value(I2C_P0_NUM, false, 0);

    // Set square wave output to 32kHz
    mcp7940_set_square_wave(I2C_P0_NUM, true, MCP7940_SW_FREQ_32KHZ);

    i2c_mutex_unlock(I2C_P0_NUM);

    return ESP_OK;
}

esp_err_t board_rtc_set_alarm_cb(board_rtc_alarm_cb_t cb)
{
    board_rtc_alarm_cb = cb;
    return ESP_OK;
}

bool board_rtc_has_power_failed()
{
    return power_failed;
}

esp_err_t board_rtc_get_power_time_down(time_t *time)
{
    if (!power_failed) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!time) {
        return ESP_ERR_INVALID_ARG;
    }

    time_t result = board_rtc_timegm(&tm_power_down);
    if (result < 0) {
        ESP_LOGE(TAG, "timegm error");
        return ESP_FAIL;
    }

    *time = result;

    return ESP_OK;
}

esp_err_t board_rtc_get_power_time_up(time_t *time)
{
    if (!power_failed) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!time) {
        return ESP_ERR_INVALID_ARG;
    }

    time_t result = board_rtc_timegm(&tm_power_up);
    if (result < 0) {
        ESP_LOGE(TAG, "timegm error");
        return ESP_FAIL;
    }

    *time = result;

    return ESP_OK;
}

esp_err_t board_rtc_get_time(time_t *time)
{
    esp_err_t ret;
    struct tm timeinfo;

    if (!time) {
        return ESP_ERR_INVALID_ARG;
    }

    i2c_mutex_lock(I2C_P0_NUM);
    ret = mcp7940_get_time(I2C_P0_NUM, &timeinfo);
    i2c_mutex_unlock(I2C_P0_NUM);

    if (ret != ESP_OK) {
       return ret;
    }

    time_t result = board_rtc_timegm(&timeinfo);
    if (result < 0) {
        ESP_LOGE(TAG, "timegm error");
        return ESP_FAIL;
    }

    *time = result;

    return ret;
}

esp_err_t board_rtc_set_time(const time_t *time)
{
    esp_err_t ret;
    struct tm timeinfo;

    if (!gmtime_r(time, &timeinfo)) {
        ESP_LOGE(TAG, "gmtime_r error");
        return ESP_FAIL;
    }

    i2c_mutex_lock(I2C_P0_NUM);
    ret = mcp7940_set_time(I2C_P0_NUM, &timeinfo);
    i2c_mutex_unlock(I2C_P0_NUM);

    if (board_rtc_alarm_cb) {
        board_rtc_alarm_cb(false, false, *time);
    }

    return ret;
}

esp_err_t board_rtc_get_alarm_enabled(bool *enabled)
{
    esp_err_t ret;

    if (!enabled) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t data = 0;
    ret = mcp7940_data_read(I2C_P0_NUM, RTC_SRAM_OFFSET, &data, 1);
    if (ret != ESP_OK) {
        return ret;
    }

    *enabled = (data == 1);

    return ret;
}

esp_err_t board_rtc_set_alarm_enabled(bool enabled)
{
    uint8_t data = enabled ? 1 : 0;
    return mcp7940_data_write(I2C_P0_NUM, RTC_SRAM_OFFSET, &data, 1);
}

esp_err_t board_rtc_int_event_handler()
{
    esp_err_t ret = ESP_OK;
    struct tm timeinfo;
    bool alarm0_occurred = false;
    bool alarm1_occurred = false;

    bzero(&timeinfo, sizeof(struct tm));

    i2c_mutex_lock(I2C_P0_NUM);
    do {
        ret = mcp7940_has_alarm_occurred(I2C_P0_NUM, MCP7940_ALARM_0, &alarm0_occurred);
        if (ret != ESP_OK) {
            break;
        }

        ret = mcp7940_has_alarm_occurred(I2C_P0_NUM, MCP7940_ALARM_1, &alarm1_occurred);
        if (ret != ESP_OK) {
            break;
        }

        ret = mcp7940_get_time(I2C_P0_NUM, &timeinfo);
        if (ret != ESP_OK) {
            break;
        }

        if (alarm0_occurred) {
            ret = mcp7940_clear_alarm_occurred(I2C_P0_NUM, MCP7940_ALARM_0);
            if (ret != ESP_OK) {
                break;
            }
        }

        if (alarm1_occurred) {
            ret = mcp7940_clear_alarm_occurred(I2C_P0_NUM, MCP7940_ALARM_1);
            if (ret != ESP_OK) {
                break;
            }
        }
    } while (0);
    i2c_mutex_unlock(I2C_P0_NUM);

    if (ret == ESP_OK && board_rtc_alarm_cb) {
        time_t time = board_rtc_timegm(&timeinfo);
        if (time >= 0) {
            ret = board_rtc_alarm_cb(alarm0_occurred, alarm1_occurred, time);
        }
    }

    return ret;
}

/*
 * This function is from mruby's time.c
 */
static unsigned int is_leapyear(unsigned int y)
{
    return (y % 4) == 0 && ((y % 100) != 0 || (y % 400) == 0);
}

/*
 * This function is from mruby's time.c
 */
static time_t board_rtc_timegm(struct tm *tm)
{
    static const unsigned int ndays[2][12] = {
            {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31},
            {31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31}
    };
    time_t r = 0;
    int i;
    unsigned int *nday = (unsigned int*) ndays[is_leapyear(tm->tm_year+1900)];

    for (i = 70; i < tm->tm_year; ++i)
        r += is_leapyear(i+1900) ? 366*24*60*60 : 365*24*60*60;
    for (i = 0; i < tm->tm_mon; ++i)
        r += nday[i] * 24 * 60 * 60;
    r += (tm->tm_mday - 1) * 24 * 60 * 60;
    r += tm->tm_hour * 60 * 60;
    r += tm->tm_min * 60;
    r += tm->tm_sec;
    return r;
}
