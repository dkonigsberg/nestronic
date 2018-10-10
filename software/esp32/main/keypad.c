/*
 * Keypad input control functions
 */

#include "keypad.h"

#include <freertos/FreeRTOS.h>
#include <esp_err.h>
#include <esp_log.h>
#include <esp_types.h>
#include <driver/touch_pad.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "board_config.h"
#include "i2c_util.h"
#include "tca8418.h"

static const char *TAG = "keypad";

static xQueueHandle keypad_event_queue = NULL;
static bool keypad_initialized = false;

static void IRAM_ATTR keypad_touch_isr_handler(void *arg);

esp_err_t keypad_init()
{
    esp_err_t ret = ESP_OK;

    ESP_LOGI(TAG, "Initializing keypad controller");

    // Create the queue for key events
    keypad_event_queue = xQueueCreate(10, sizeof(keypad_event_t));
    if (!keypad_event_queue) {
        return ESP_ERR_NO_MEM;
    }

    // Configure the GPIO for INT
    gpio_config_t config = {
        .pin_bit_mask = 1ULL << TCA8418_INT_PIN,
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE, // using external pull-up
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_PIN_INTR_NEGEDGE // interrupt on falling edge
    };
    gpio_config(&config);

    // Initialize the keypad controller
    i2c_mutex_lock(I2C_P1_NUM);

    do {
        if ((ret = tca8418_init(I2C_P1_NUM)) != ESP_OK) {
            break;
        }

        const tca8418_pins_t pins_zero = { 0x00, 0x00, 0x00 };
        const tca8418_pins_t pins_int = { 0x00, 0xFF, 0x00 };

        // Enable pull-ups on all GPIO pins
        if ((ret = tca8418_gpio_pullup_disable(I2C_P1_NUM, &pins_zero)) != ESP_OK) {
            break;
        }

        // Set pins to GPIO mode
        if ((ret = tca8418_kp_gpio_select(I2C_P1_NUM, &pins_zero)) != ESP_OK) {
            break;
        }

        // Set the event FIFO (disabled for now)
        if ((ret = tca8418_gpi_event_mode(I2C_P1_NUM, &pins_int)) != ESP_OK) {
            break;
        }

        // Set GPIO direction to input
        if ((ret = tca8418_gpio_data_direction(I2C_P1_NUM, &pins_zero)) != ESP_OK) {
            break;
        }

        // Enable GPIO interrupts for COL[0..6]
        if ((ret = tca8418_gpio_interrupt_enable(I2C_P1_NUM, &pins_int)) != ESP_OK) {
            break;
        }

        // Set the configuration register to enable GPIO interrupts
        if ((ret = tca8148_set_config(I2C_P1_NUM, TCA8418_CFG_KE_IEN)) != ESP_OK) {
            break;
        }
    } while (0);

    i2c_mutex_unlock(I2C_P1_NUM);

    if (ret != ESP_OK) {
    	ESP_LOGE(TAG, "Keypad setup error: %s", esp_err_to_name(ret));
    	// Disable interrupt GPIO if setup fails
    	config.mode = GPIO_MODE_DISABLE;
    	config.intr_type = GPIO_PIN_INTR_DISABLE;
    	gpio_config(&config);
    	return ret;
    }

    keypad_initialized = true;
    ESP_LOGI(TAG, "Keypad controller configured");

    // Initialize the capacitive touch pad
    do {
        if ((ret = touch_pad_init()) != ESP_OK) {
            break;
        }

        if ((ret = touch_pad_set_fsm_mode(TOUCH_FSM_MODE_TIMER)) != ESP_OK) {
            break;
        }

        if ((ret = touch_pad_config(TOUCH_PAD_PIN, 0)) != ESP_OK) {
            break;
        }

        if ((ret = touch_pad_set_voltage(TOUCH_HVOLT_2V7, TOUCH_LVOLT_0V5, TOUCH_HVOLT_ATTEN_0V)) != ESP_OK) {
            break;
        }

        if ((ret = touch_pad_set_thresh(TOUCH_PAD_PIN, 200)) != ESP_OK) {
            break;
        }

        if ((ret = touch_pad_set_trigger_mode(TOUCH_TRIGGER_BELOW)) != ESP_OK) {
            break;
        }

        if ((ret = touch_pad_isr_register(keypad_touch_isr_handler, NULL)) != ESP_OK) {
            break;
        }

        if ((ret = touch_pad_intr_enable()) != ESP_OK) {
            break;
        }
    } while (0);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Touch pad setup error: %s", esp_err_to_name(ret));
        touch_pad_intr_disable();
        return ret;
    }

    ESP_LOGI(TAG, "Touch pad configured");

    return ret;
}

esp_err_t keypad_inject_event(const keypad_event_t *event)
{
    if (!event) {
        return ESP_ERR_INVALID_ARG;
    }

    xQueueSend(keypad_event_queue, event, 0);
    return ESP_OK;
}

esp_err_t keypad_clear_events()
{
    xQueueReset(keypad_event_queue);
    return ESP_OK;
}

esp_err_t keypad_flush_events()
{
    keypad_event_t event;
    bzero(&event, sizeof(keypad_event_t));
    xQueueReset(keypad_event_queue);
    xQueueSend(keypad_event_queue, &event, 0);
    return ESP_OK;
}

esp_err_t keypad_wait_for_event(keypad_event_t *event, int msecs_to_wait)
{
    TickType_t ticks = msecs_to_wait < 0 ? portMAX_DELAY : (msecs_to_wait / portTICK_RATE_MS);
    if (!xQueueReceive(keypad_event_queue, event, ticks)) {
        if (msecs_to_wait > 0) {
            return ESP_ERR_TIMEOUT;
        } else {
            bzero(event, sizeof(keypad_event_t));
        }
    }
    return ESP_OK;
}

esp_err_t keypad_touch_pad_test(uint16_t *val)
{
    esp_err_t ret = ESP_OK;

    ret = touch_pad_read(TOUCH_PAD_PIN, val);
    if (ret != ESP_OK) {
        return ret;
    }

    return ret;
}

static void IRAM_ATTR keypad_touch_isr_handler(void *arg)
{
    static TickType_t last_touch = 0;

    uint32_t pad_intr = touch_pad_get_status();
    touch_pad_clear_status();

    if ((pad_intr >> TOUCH_PAD_PIN) & 0x01) {
        TickType_t this_touch = xTaskGetTickCountFromISR();
        if (this_touch - last_touch > (100 / portTICK_RATE_MS)) {
            keypad_event_t keypad_event = {
                    .key = KEYPAD_TOUCH,
                    .pressed = true
            };
            xQueueSendFromISR(keypad_event_queue, &keypad_event, 0);
        }
        last_touch = this_touch;
    }
}

esp_err_t keypad_int_event_handler()
{
    esp_err_t ret = ESP_OK;

    if (!keypad_initialized) {
    	return ret;
    }

    i2c_mutex_lock(I2C_P1_NUM);

    do {
        uint8_t int_status;

        // Read the INT_STAT (0x02) register to determine what asserted the
        // INT line. If GPI_INT or K_INT is set, then  a key event has
        // occurred, and the event is stored in the FIFO.
        ret = tca8148_get_interrupt_status(I2C_P1_NUM, &int_status);
        if (ret != ESP_OK) {
            break;
        }
        ESP_LOGD(TAG, "INT_STAT: %02X (GPI=%d, K=%d)", int_status,
                (int_status & TCA8418_INT_STAT_GPI_INT) != 0,
                (int_status & TCA8418_INT_STAT_K_INT) != 0);

        // Read the KEY_LCK_EC (0x03) register, bits [3:0] to see how many
        // events are stored in FIFO.
        uint8_t count;
        ret = tca8148_get_key_event_count(I2C_P1_NUM, &count);
        if (ret != ESP_OK) {
            break;
        }
        ESP_LOGD(TAG, "Key event count: %d", count);

        bool key_error = false;
        bool cb_error = false;
        do {
            uint8_t key;
            bool pressed;
            ret = tca8148_get_next_key_event(I2C_P1_NUM, &key, &pressed);
            if (ret != ESP_OK) {
                key_error = true;
                break;
            }

            if (key == 0 && pressed == 0) {
                // Last key has been read, break the loop
                break;
            }

            ESP_LOGD(TAG, "Key event: key=%d, pressed=%d", key, pressed);

            // Temporarily release the mutex and add the event to the queue
            i2c_mutex_unlock(I2C_P1_NUM);
            keypad_event_t keypad_event = {
                .key = key,
                .pressed = pressed
            };
            xQueueSend(keypad_event_queue, &keypad_event, 0);
            i2c_mutex_lock(I2C_P1_NUM);
        } while (!key_error && !cb_error);

        if (key_error) {
            break;
        }

        // Read the GPIO INT STAT registers
        tca8418_pins_t int_pins;
        ret = tca8148_get_gpio_interrupt_status(I2C_P1_NUM, &int_pins);
        if (ret != ESP_OK) {
            break;
        }
        ESP_LOGD(TAG, "GPIO_INT_STAT: %02X %02X %02X", int_pins.rows, int_pins.cols_l, int_pins.cols_h);

        // Reset the INT_STAT interrupt flag which was causing the interrupt
        // by writing a 1 to the specific bit.
        ret = tca8148_set_interrupt_status(I2C_P1_NUM, int_status);
        if (ret != ESP_OK) {
            break;
        }
    } while (0);

    i2c_mutex_unlock(I2C_P1_NUM);

    return ret;
}
