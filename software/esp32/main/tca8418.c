#include "tca8418.h"

#include <esp_err.h>
#include <esp_log.h>
#include <driver/i2c.h>

#include <strings.h>

#include "i2c_util.h"

static const char *TAG = "tca8418";

/* I2C device address */
#define TCA8418_ADDRESS  0x34

/* Registers */
#define TCA8418_CFG            0x01
#define TCA8418_INT_STAT       0x02
#define TCA8418_KEY_LCK_EC     0x03
#define TCA8418_KEY_EVENT_A    0x04
#define TCA8418_KEY_EVENT_B    0x05
#define TCA8418_KEY_EVENT_C    0x06
#define TCA8418_KEY_EVENT_D    0x07
#define TCA8418_KEY_EVENT_E    0x08
#define TCA8418_KEY_EVENT_F    0x09
#define TCA8418_KEY_EVENT_G    0x0A
#define TCA8418_KEY_EVENT_H    0x0B
#define TCA8418_KEY_EVENT_I    0x0C
#define TCA8418_KEY_EVENT_J    0x0D
#define TCA8418_KP_LCK_TIMER   0x0E
#define TCA8418_UNLOCK1        0x0F
#define TCA8418_UNLOCK2        0x10
#define TCA8418_GPIO_INT_STAT1 0x11
#define TCA8418_GPIO_INT_STAT2 0x12
#define TCA8418_GPIO_INT_STAT3 0x13
#define TCA8418_GPIO_DAT_STAT1 0x14
#define TCA8418_GPIO_DAT_STAT2 0x15
#define TCA8418_GPIO_DAT_STAT3 0x16
#define TCA8418_GPIO_DAT_OUT1  0x17
#define TCA8418_GPIO_DAT_OUT2  0x18
#define TCA8418_GPIO_DAT_OUT3  0x19
#define TCA8418_GPIO_INT_EN1   0x1A
#define TCA8418_GPIO_INT_EN2   0x1B
#define TCA8418_GPIO_INT_EN3   0x1C
#define TCA8418_KP_GPIO1       0x1D
#define TCA8418_KP_GPIO2       0x1E
#define TCA8418_KP_GPIO3       0x1F
#define TCA8418_GPI_EM1        0x20
#define TCA8418_GPI_EM2        0x21
#define TCA8418_GPI_EM3        0x22
#define TCA8418_GPIO_DIR1      0x23
#define TCA8418_GPIO_DIR2      0x24
#define TCA8418_GPIO_DIR3      0x25
#define TCA8418_GPIO_INT_LVL1  0x26
#define TCA8418_GPIO_INT_LVL2  0x27
#define TCA8418_GPIO_INT_LVL3  0x28
#define TCA8418_DEBOUNCE_DIS1  0x29
#define TCA8418_DEBOUNCE_DIS2  0x2A
#define TCA8418_DEBOUNCE_DIS3  0x2B
#define TCA8418_GPIO_PULL1     0x2C
#define TCA8418_GPIO_PULL2     0x2D
#define TCA8418_GPIO_PULL3     0x2E

esp_err_t tca8418_init(i2c_port_t i2c_num)
{
    esp_err_t ret;
    uint8_t data;

    // Disable interrupts in case we didn't come up clean, so we're forced
    // to explicitly enable them.
    ret = i2c_write_register(i2c_num, TCA8418_ADDRESS, TCA8418_CFG, 0x00);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = i2c_write_register(i2c_num, TCA8418_ADDRESS, TCA8418_GPIO_INT_EN1, 0x00);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = i2c_write_register(i2c_num, TCA8418_ADDRESS, TCA8418_GPIO_INT_EN2, 0x00);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = i2c_write_register(i2c_num, TCA8418_ADDRESS, TCA8418_GPIO_INT_EN3, 0x00);
    if (ret != ESP_OK) {
        return ret;
    }

    // Read key events until the FIFO is clear
    do {
        ret = i2c_read_register(i2c_num, TCA8418_ADDRESS, TCA8418_KEY_EVENT_A, &data);
        if (ret != ESP_OK) {
            return ret;
        }
    } while (data != 0);

    // Clear any leftover interrupt flags
    ret = i2c_write_register(i2c_num, TCA8418_ADDRESS, TCA8418_INT_STAT, 0x0F);
    if (ret != ESP_OK) {
        return ret;
    }

    // Read the initial state of the controller
    ret = i2c_read_register(i2c_num, TCA8418_ADDRESS, TCA8418_CFG, &data);
    if (ret != ESP_OK) {
        return ret;
    }
    ESP_LOGI(TAG, "CONFIG: %02X", data);

    ret = i2c_read_register(i2c_num, TCA8418_ADDRESS, TCA8418_INT_STAT, &data);
    if (ret != ESP_OK) {
        return ret;
    }
    ESP_LOGI(TAG, "INT_STAT: %02X", data);

    ret = i2c_read_register(i2c_num, TCA8418_ADDRESS, TCA8418_KEY_LCK_EC, &data);
    if (ret != ESP_OK) {
        return ret;
    }
    ESP_LOGI(TAG, "KEY_LCK_EC: %02X", data);

    return ESP_OK;
}

esp_err_t tca8148_set_config(i2c_port_t i2c_num, uint8_t value)
{
    return i2c_write_register(i2c_num, TCA8418_ADDRESS, TCA8418_CFG, value);
}

esp_err_t tca8148_get_interrupt_status(i2c_port_t i2c_num, uint8_t *status)
{
    return i2c_read_register(i2c_num, TCA8418_ADDRESS, TCA8418_INT_STAT, status);
}

esp_err_t tca8148_set_interrupt_status(i2c_port_t i2c_num, uint8_t status)
{
    return i2c_write_register(i2c_num, TCA8418_ADDRESS, TCA8418_INT_STAT, status);
}

esp_err_t tca8148_get_key_event_count(i2c_port_t i2c_num, uint8_t *count)
{
    esp_err_t ret;
    uint8_t data;

    ret = i2c_read_register(i2c_num, TCA8418_ADDRESS, TCA8418_KEY_LCK_EC, &data);
    if (ret != ESP_OK) {
        return ret;
    }

    *count = (data & 0x0F);

    return ESP_OK;
}

esp_err_t tca8148_get_next_key_event(i2c_port_t i2c_num, uint8_t *key, bool *pressed)
{
    esp_err_t ret;
    uint8_t data;

    ret = i2c_read_register(i2c_num, TCA8418_ADDRESS, TCA8418_KEY_EVENT_A, &data);
    if (ret != ESP_OK) {
        return ret;
    }

    *key = (data & 0x7F);
    *pressed = (data & 0x80) != 0;

    return ESP_OK;
}

esp_err_t tca8148_get_gpio_interrupt_status(i2c_port_t i2c_num, tca8418_pins_t *pins)
{
    esp_err_t ret;
    uint8_t data1;
    uint8_t data2;
    uint8_t data3;

    ret = i2c_read_register(i2c_num, TCA8418_ADDRESS, TCA8418_GPIO_INT_STAT1, &data1);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = i2c_read_register(i2c_num, TCA8418_ADDRESS, TCA8418_GPIO_INT_STAT2, &data2);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = i2c_read_register(i2c_num, TCA8418_ADDRESS, TCA8418_GPIO_INT_STAT3, &data3);
    if (ret != ESP_OK) {
        return ret;
    }

    pins->rows = data1;
    pins->cols_l = data2;
    pins->cols_h = data3 & 0x03;

    return ESP_OK;
}

esp_err_t tca8418_gpio_interrupt_enable(i2c_port_t i2c_num, const tca8418_pins_t *pins)
{
    esp_err_t ret;

    ret = i2c_write_register(i2c_num, TCA8418_ADDRESS, TCA8418_GPIO_INT_EN1, pins->rows);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = i2c_write_register(i2c_num, TCA8418_ADDRESS, TCA8418_GPIO_INT_EN2, pins->cols_l);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = i2c_write_register(i2c_num, TCA8418_ADDRESS, TCA8418_GPIO_INT_EN3, pins->cols_h);
    if (ret != ESP_OK) {
        return ret;
    }

    return ESP_OK;
}

esp_err_t tca8418_kp_gpio_select(i2c_port_t i2c_num, const tca8418_pins_t *pins)
{
    esp_err_t ret;

    ret = i2c_write_register(i2c_num, TCA8418_ADDRESS, TCA8418_KP_GPIO1, pins->rows);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = i2c_write_register(i2c_num, TCA8418_ADDRESS, TCA8418_KP_GPIO2, pins->cols_l);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = i2c_write_register(i2c_num, TCA8418_ADDRESS, TCA8418_KP_GPIO3, pins->cols_h);
    if (ret != ESP_OK) {
        return ret;
    }

    return ESP_OK;
}

esp_err_t tca8418_gpi_event_mode(i2c_port_t i2c_num, const tca8418_pins_t *pins)
{
    esp_err_t ret;

    ret = i2c_write_register(i2c_num, TCA8418_ADDRESS, TCA8418_GPI_EM1, pins->rows);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = i2c_write_register(i2c_num, TCA8418_ADDRESS, TCA8418_GPI_EM2, pins->cols_l);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = i2c_write_register(i2c_num, TCA8418_ADDRESS, TCA8418_GPI_EM3, pins->cols_h);
    if (ret != ESP_OK) {
        return ret;
    }

    return ESP_OK;
}

esp_err_t tca8418_gpio_data_direction(i2c_port_t i2c_num, const tca8418_pins_t *pins)
{
    esp_err_t ret;

    ret = i2c_write_register(i2c_num, TCA8418_ADDRESS, TCA8418_GPIO_DIR1, pins->rows);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = i2c_write_register(i2c_num, TCA8418_ADDRESS, TCA8418_GPIO_DIR2, pins->cols_l);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = i2c_write_register(i2c_num, TCA8418_ADDRESS, TCA8418_GPIO_DIR3, pins->cols_h);
    if (ret != ESP_OK) {
        return ret;
    }

    return ESP_OK;

}

esp_err_t tca8418_gpio_pullup_disable(i2c_port_t i2c_num, const tca8418_pins_t *pins)
{
    esp_err_t ret;

    ret = i2c_write_register(i2c_num, TCA8418_ADDRESS, TCA8418_GPIO_PULL1, pins->rows);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = i2c_write_register(i2c_num, TCA8418_ADDRESS, TCA8418_GPIO_PULL2, pins->cols_l);
    if (ret != ESP_OK) {
        return ret;
    }
    ret = i2c_write_register(i2c_num, TCA8418_ADDRESS, TCA8418_GPIO_PULL3, pins->cols_h);
    if (ret != ESP_OK) {
        return ret;
    }

    return ESP_OK;
}

esp_err_t tca8418_clear_interrupt_status(i2c_port_t i2c_num)
{
    return i2c_write_register(i2c_num, TCA8418_ADDRESS, TCA8418_INT_STAT, 0x0F);
}
