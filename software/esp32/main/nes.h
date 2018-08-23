/*
 * NES CPU (RP2A03) connected via I2C
 */

#ifndef NES_H
#define NES_H

#include <esp_err.h>
#include <driver/i2c.h>

typedef enum {
    NES_APU_PULSE1CTRL  = 0x4000, /**< Pulse #1 Control Register (W) */
    NES_APU_PULSE1RAMP  = 0x4001, /**< Pulse #1 Ramp Control Register (W) */
    NES_APU_PULSE1FTUNE = 0x4002, /**< Pulse #1 Fine Tune (FT) Register (W) */
    NES_APU_PULSE1CTUNE = 0x4003, /**< Pulse #1 Coarse Tune (CT) Register (W) */
    NES_APU_PULSE2CTRL  = 0x4004, /**< Pulse #2 Control Register (W) */
    NES_APU_PULSE2RAMP  = 0x4005, /**< Pulse #2 Ramp Control Register (W) */
    NES_APU_PULSE2FTUNE = 0x4006, /**< Pulse #2 Fine Tune Register (W) */
    NES_APU_PULSE2STUNE = 0x4007, /**< Pulse #2 Coarse Tune Register (W) */
    NES_APU_TRICTRL1    = 0x4008, /**< Triangle Control Register #1 (W) */
    NES_APU_TRICTRL2    = 0x4009, /**< Triangle Control Register #2 (?) */
    NES_APU_TRIFREQ1    = 0x400A, /**< Triangle Frequency Register #1 (W) */
    NES_APU_TRIFREQ2    = 0x400B, /**< Triangle Frequency Register #2 (W) */
    NES_APU_NOISECTRL   = 0x400C, /**< Noise Control Register #1 (W) */
    NES_APU_UNUSED      = 0x400D, /**< Unused (???) */
    NES_APU_NOISEFREQ1  = 0x400E, /**< Noise Frequency Register #1 (W) */
    NES_APU_NOISEFREQ2  = 0x400F, /**< Noise Frequency Register #2 (W) */
    NES_APU_MODCTRL     = 0x4010, /**< Delta Modulation Control Register (W) */
    NES_APU_MODDA       = 0x4011, /**< Delta Modulation D/A Register (W) */
    NES_APU_MODADDR     = 0x4012, /**< Delta Modulation Address Register (W) */
    NES_APU_MODLEN      = 0x4013, /**< Delta Modulation Data Length Register (W) */
    NES_APU_CHANCTRL    = 0x4015, /**< Sound/Vertical Clock Signal Register (R/W) */
    NES_APU_PAD2        = 0x4017  /**< Joypad #2/SOFTCLK (W) */
} nes_apu_register_t;

esp_err_t nes_init(i2c_port_t i2c_num);

esp_err_t nes_set_config(i2c_port_t i2c_num, uint8_t value);
esp_err_t nes_get_config(i2c_port_t i2c_num, uint8_t *value);

esp_err_t nes_set_amplifier_enabled(i2c_port_t i2c_num, bool enabled);
esp_err_t nes_get_amplifier_enabled(i2c_port_t i2c_num, bool *enabled);

esp_err_t nes_apu_init(i2c_port_t i2c_num);
esp_err_t nes_apu_write(i2c_port_t i2c_num, nes_apu_register_t reg, uint8_t dat);

esp_err_t nes_data_write(i2c_port_t i2c_num, uint8_t block, uint8_t *data, size_t data_len);
esp_err_t nes_data_read(i2c_port_t i2c_num, uint8_t block, uint8_t *data, size_t data_len);

/**
 * Convert a 16-bit NES memory address into an APU sample block
 *
 * @param addr An address value within the APU range ($8000-$FFFF)
 * @return A block identifier (0-511)
 */
uint16_t nes_addr_to_apu_block(uint16_t addr);

/**
 * Convert a byte length into an equivalent number of NES APU sample
 * blocks, rounded up.
 *
 * @param len Data length value in bytes
 * @return A block count
 */
uint16_t nes_len_to_apu_blocks(uint32_t len);

#endif /* NES_H */
