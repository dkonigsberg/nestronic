/*
 * Hardware configuration constants
 */

#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

#include <driver/gpio.h>
#include <driver/i2c.h>
#include <driver/adc.h>
#include <driver/spi_common.h>

/* Pin mapping for SD card in SD/MMC mode */
#define SDMMC_CMD                  GPIO_NUM_15
#define SDMMC_CLK                  GPIO_NUM_14
#define SDMMC_D0                   GPIO_NUM_2
#define SDMMC_D1                   GPIO_NUM_4
#define SDMMC_D2                   GPIO_NUM_12
#define SDMMC_D3                   GPIO_NUM_13
#define SDMMC_CD                   GPIO_NUM_35

/* Pin mapping for display module */
#define SSD1322_MOSI               GPIO_NUM_23
#define SSD1322_SCK                GPIO_NUM_18
#define SSD1322_CS                 GPIO_NUM_5
#define SSD1322_DC                 GPIO_NUM_22
#define SSD1322_RST                GPIO_NUM_21
#define SSD1322_SPI_HOST           VSPI_HOST

/* Pin mapping and parameters for I2C port 0 (on-board devices) */
#define I2C_P0_SCL_IO              GPIO_NUM_17
#define I2C_P0_SDA_IO              GPIO_NUM_16
#define I2C_P0_NUM                 I2C_NUM_0
#define I2C_P0_TX_BUF_DISABLE      0
#define I2C_P0_RX_BUF_DISABLE      0
#define I2C_P0_FREQ_HZ             400000

/* Pin mapping and parameters for I2C port 1 (input board devices) */
#define I2C_P1_SCL_IO              GPIO_NUM_25
#define I2C_P1_SDA_IO              GPIO_NUM_26
#define I2C_P1_NUM                 I2C_NUM_1
#define I2C_P1_TX_BUF_DISABLE      0
#define I2C_P1_RX_BUF_DISABLE      0
#define I2C_P1_FREQ_HZ             400000

/* MCP7940 RTC Interrupt Pin */
#define MCP7940_MFP_PIN            GPIO_NUM_19

/* TCA8418 Keypad Controller Interrupt Pin */
#define TCA8418_INT_PIN            GPIO_NUM_27

/* ADC2 Voltage Reference Pin for calibration */
#define ADC2_VREF_PIN              GPIO_NUM_27

/* ADC1 Volume Control Pin */
#define ADC1_VOL_PIN               ADC1_CHANNEL_5 /* GPIO33 */

/* Input Board Touch Sensor Pin */
#define TOUCH_PAD_PIN              TOUCH_PAD_NUM9 /* GPIO32 */

/* Input Board button codes */
#define KEYPAD_BUTTON_UP           105
#define KEYPAD_BUTTON_DOWN         106
#define KEYPAD_BUTTON_LEFT         107
#define KEYPAD_BUTTON_RIGHT        108
#define KEYPAD_BUTTON_SELECT       109
#define KEYPAD_BUTTON_START        110
#define KEYPAD_BUTTON_B            111
#define KEYPAD_BUTTON_A            112
#define KEYPAD_TOUCH               200

/* Misc constants */
#define MENU_TIMEOUT_MS            30000

#endif /* BOARD_CONFIG_H */
