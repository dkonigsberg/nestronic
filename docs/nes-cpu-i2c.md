NES CPU I2C Protocol
====================

Communication between the ESP32 and the NES CPU (RP2A03) happens via the I2C
protocol. On the ESP32 side, the built-in I2C controller is used to act as a
master. On the NES CPU side, a PCI9564 controller IC is used to act as a slave.
This document describes the specifics of the I2C protocol implemented in the
firmware on the NES CPU.


Writing to the device
---------------------
The NES CPU listens as a slave at 7-bit address $08.
To write to it, this typical patten is followed:

    +---+---+---+---+---+---+---+---+---+---+
    | S | 0 | 0 | 0 | 1 | 0 | 0 | 0 |R/W| A |
    +---+---+---+---+---+---+---+---+---+---+
Send the start condition, the 7-bit slave address, and the read/write bit,
followed by an ack.

    +---+---+---+---+---+---+---+---+---+
    | B7| B6| B5| B4| B3| B2| B1| B0| A |
    +---+---+---+---+---+---+---+---+---+

Send the 8-bit register address, followed by an ack.

    +---+---+---+---+---+---+---+---+---+---+
    | D7| D6| D5| D4| D3| D2| D1| D0| A | P |
    +---+---+---+---+---+---+---+---+---+---+

Send the 8-bit data byte, followed by an ack and a stop condition.


Register map
------------

    +------+---------------+-----+-----+-----+-----+-----+-----+-----+-----+
    | Addr | Register Name |Bit 7|Bit 6|Bit 5|Bit 4|Bit 3|Bit 2|Bit 1|Bit 0|
    +------+---------------+-----+-----+-----+-----+-----+-----+-----+-----+
    |  $00 |               |                                               |
    |  ... | NES APU   (W) | See below                                     |
    |  $15 |               |                                               |
    +------+---------------+-----+-----+-----+-----+-----+-----+-----+-----+
    |  $16 | OUTPUT  (R/W) |APUI | N/A | N/A | N/A | N/A |OUT2 |OUT1 |OUT0 |
    +------+---------------+-----+-----+-----+-----+-----+-----+-----+-----+
    |  $17 | NES APU   (W) | See below                                     |
    +------+---------------+-----+-----+-----+-----+-----+-----+-----+-----+
    |  $7F | CONFIG  (R/W) | N/A | N/A | N/A | N/A | N/A | N/A | N/A | N/A |
    +------+---------------+-----+-----+-----+-----+-----+-----+-----+-----+
    |  $88 |               |                                               |
    |  ... | DATA    (R/W) | See below                                     |
    |  $FF |               |                                               |
    +------+---------------+-----+-----+-----+-----+-----+-----+-----+-----+

Note: Unsupported registers are simply ignored by the firmware.


### OUTPUT Register ($16)

OUTPUT Register Bits:
    APUI: APU Initialize; 1 to reinitialize the APU (W)
    OUT0: Audio amplifier control; 0 to disable, 1 to enable (R/W)


### NES CPU Registers ($00-$15, $17)

The following APU registers are directly mapped to addresses corresponding to
the lower byte of the APU register address: (e.g. $4010 -> $10)

    APU_PULSE1CTRL  = $4000         ; Pulse #1 Control Register (W)
    APU_PULSE1RAMP  = $4001         ; Pulse #1 Ramp Control Register (W)
    APU_PULSE1FTUNE = $4002         ; Pulse #1 Fine Tune (FT) Register (W)
    APU_PULSE1CTUNE = $4003         ; Pulse #1 Coarse Tune (CT) Register (W)
    APU_PULSE2CTRL  = $4004         ; Pulse #2 Control Register (W)
    APU_PULSE2RAMP  = $4005         ; Pulse #2 Ramp Control Register (W)
    APU_PULSE2FTUNE = $4006         ; Pulse #2 Fine Tune Register (W)
    APU_PULSE2STUNE = $4007         ; Pulse #2 Coarse Tune Register (W)
    APU_TRICTRL1    = $4008         ; Triangle Control Register #1 (W)
    APU_TRICTRL2    = $4009         ; Triangle Control Register #2 (?)
    APU_TRIFREQ1    = $400A         ; Triangle Frequency Register #1 (W)
    APU_TRIFREQ2    = $400B         ; Triangle Frequency Register #2 (W)
    APU_NOISECTRL   = $400C         ; Noise Control Register #1 (W)
    APU_UNUSED      = $400D         ; Unused (???)
    APU_NOISEFREQ1  = $400E         ; Noise Frequency Register #1 (W)
    APU_NOISEFREQ2  = $400F         ; Noise Frequency Register #2 (W)
    APU_MODCTRL     = $4010         ; Delta Modulation Control Register (W)
    APU_MODDA       = $4011         ; Delta Modulation D/A Register (W)
    APU_MODADDR     = $4012         ; Delta Modulation Address Register (W)
    APU_MODLEN      = $4013         ; Delta Modulation Data Length Register (W)
    APU_CHANCTRL    = $4015         ; Sound/Vertical Clock Signal Register (R/W)
    APU_PAD2        = $4017         ; Joypad #2/SOFTCLK (W)

### CONFIG Register

This register is currently an unused region of memory that can be both
read from and written to. It can be used to implement a basic interface test.

### Data Registers ($88-$FF)

These registers are used to write bulk data to a region of RAM that
corresponds to the address range $C200-$DFFF. Each value corresponds to the
start of a 64-byte block within that range.
To write bulk data, first send the start address, then start sending data
bytes. Each byte will increment the address offset, up to a maximum of 256
bytes.
