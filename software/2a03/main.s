;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
; Nestronic Code for the NES CPU (RP2A03) ;
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

.import __STACK_START__, __STACK_SIZE__
.include "zeropage.inc"
.include "nes.inc"

;
; Definitions
;
PCA_SLAVE_ADDR  = $08   ; I2C Slave Address

; PCA9564 Registers
PCA_STA         = $6000 ; Status (R)
PCA_TO          = $6000 ; Time-out (W)
PCA_DAT         = $6001 ; Data (R/W)
PCA_ADR         = $6002 ; Own address (R/W)
PCA_CON         = $6003 ; Control (R/W)

; PCA9564 Clock speeds
PCA_CON_330kHz  = $00
PCA_CON_288kHz  = $01
PCA_CON_217kHz  = $02
PCA_CON_146kHz  = $03
PCA_CON_88kHz   = $04
PCA_CON_59kHz   = $05
PCA_CON_44kHz   = $06
PCA_CON_36kHz   = $07

; PCA9564 Control flags
PCA_CON_AA      = $80 ; Assert Acknowledge
PCA_CON_ENSIO   = $40 ; Enable
PCA_CON_STA     = $20 ; Start
PCA_CON_STO     = $10 ; Stop
PCA_CON_SI      = $08 ; Serial Interrupt
PCA_CON_CR      = $07 ; Clock Rate (MASK)

; I2C Registers
REG_OUTPUT      = $16 ; Output control register
REG_CONFIG      = $80 ; Device configuration register

.segment "ZEROPAGE"
; Variables go here
cmd_register:   .res 1 ; I2C selected register
cmd_value:      .res 1
cmd_recv_state: .res 1 ; I2C receive state
output_value:   .res 1 ; Value of the OUTPUT register
config_value:   .res 1 ; Value of the CONFIG register

.segment "STARTUP"

.segment "CODE"
start:
    sei     ; Ignore IRQs
    cld     ; Disable decimal mode

    ; Setup the stack
    ldx #$FF
    txs

    ; Initialize the amplifier as disabled
    lda #$00
    sta APU_PAD1

    ; Initialize the APU
    jsr init_apu

    ; Enable the I2C controller
    lda #$02
    sta APU_PAD1

    ; Wait a little bit
    ldy #20
    jsr delay_y_frames

    ; Initialize variables
    lda #$00
    sta cmd_register
    sta cmd_value
    sta cmd_recv_state
    sta output_value
    sta config_value

    ; Initialize the I2C controller
    jsr i2c_init

@cmd_loop:
    jsr i2c_slave_cmd

    lda output_value    ; Load the value of OUTPUT into A
    and #$80            ; Mask the APU Initialize bit
    cmp #$80            ; Check if the bit is set
    bne @cmd_loop       ; Restart the loop if the bit was not set

    jsr init_apu        ; Initialize the APU
    lda output_value    ; Clear the bit in the OUTPUT register
    and #$7F
    sta output_value
    jmp @cmd_loop       ; Restart the loop

forever:
    jmp forever    ; loop forever to halt

;
; Initializes APU registers and silences all channels
;
.proc init_apu
    ; Init $4000-4013
    ldy #$13
@loop:
    lda @regs,y
    sta $4000,y
    dey
    bpl @loop

    ; We have to skip over $4014 (OAMDMA)
    lda #$0f
    sta $4015
    lda #$40
    sta $4017

    rts
@regs:
    .byte $30,$08,$00,$00
    .byte $30,$08,$00,$00
    .byte $80,$00,$00,$00
    .byte $30,$00,$00,$00
    .byte $00,$00,$00,$00
.endproc

;
; Initialize the PCA9564 I2C Controller
;
.proc i2c_init
    lda #$FF
    sta PCA_TO          ; Set timeout
    lda #(PCA_SLAVE_ADDR << 1)
    sta PCA_ADR         ; Set own address to $08
    lda #(PCA_CON_ENSIO | PCA_CON_330kHz)
    sta PCA_CON         ; Enable serial I/O
    nop                 ; Wait for oscillator startup
    nop
    lda #(PCA_CON_AA | PCA_CON_ENSIO | PCA_CON_330kHz)
    sta PCA_CON         ; Start as slave
    rts
.endproc

;
; Wait for the I2C interrupt flag to be set
;
.proc i2c_wait_busy
    ; TODO: store a timeout counter somewhere
@loop:
    lda PCA_CON         ; Load the current value of I2CCON
    and #PCA_CON_SI     ; Mask the SI bit
    cmp #PCA_CON_SI     ; Check if the SI bit is set
    ; TODO: decrement a timer counter. and fail if zero
    bne @loop           ; If not set, then loop
    rts
.endproc

;
; Wait as an I2C slave for the next command
;
.proc i2c_slave_cmd
    lda #$00
    sta cmd_recv_state  ; Reset the receive state

    jsr i2c_wait_busy   ; Wait for SI

    lda PCA_STA
    cmp #$60            ; Own SLA+W has been received
    beq @receiver
    cmp #$A8            ; Own SLA+R has been received
    beq @transmitter
    jmp @fault

@receiver:
    lda #(PCA_CON_AA | PCA_CON_ENSIO | PCA_CON_330kHz)
    sta PCA_CON         ; Reset SI bit

    jsr i2c_wait_busy

    lda PCA_STA
    cmp #$80            ; Data has been received
    bne @receiver_done

    lda cmd_recv_state ; Check if we are in the register or value state
    cmp #$01
    beq @receiver_data

@receiver_register:
    lda PCA_DAT
    sta cmd_register    ; Store the register byte
    lda #$01
    sta cmd_recv_state  ; Register received, switch to value state
    jmp @receiver

@receiver_data:
    lda PCA_DAT
    sta cmd_value        ; Store the value byte
    jsr register_write   ; Handle the register write
    jmp @receiver

@receiver_done:
    lda PCA_STA
    cmp #$A0            ; A STOP condition has been received
    bne @fault
    jmp @done

@transmitter:
    jsr register_read   ; Handle the register read
    lda cmd_value
    sta PCA_DAT

    lda #(PCA_CON_AA | PCA_CON_ENSIO | PCA_CON_330kHz)
    sta PCA_CON         ; Reset SI bit

    jsr i2c_wait_busy

    lda PCA_STA
    cmp #$B8            ; Data byte in I2CDAT has been transmitted (ACK)
    beq @transmitter
    cmp #$C0            ; Data byte in I2CDAT has been transmitted (NACK)
    bne @fault

@transmitter_done:
    jmp @done

@fault:
    ; figure out what to do in a fault condition
    ; PCA_STA in A
    ;
    ; Fault cases: 
    ; Initial SI
    ;     Not $60 (SLA+W) or $A8 (SLA+R)
    ; Receiver done
    ;     Not $A0 (STOP condition)
    ; Transmitter almost done
    ;     Not $B8 (ACK) or $C0 (NACK)

@done:
    lda #(PCA_CON_AA | PCA_CON_ENSIO | PCA_CON_330kHz)
    sta PCA_CON         ; Reset SI bit
    rts
.endproc

;
; Write to the selected register
;
.proc register_write
    lda cmd_register

    ; Check if the command register maps to a writable NES APU register
    cmp #$14            ; Check if in range $00-$13
    bmi @apu_write
    cmp #$15            ; Check if equal to $15
    beq @apu_write
    cmp #$17            ; Check if equal to $17
    beq @apu_write

    ; Check if the command register maps to the OUTPUT register
    cmp #REG_OUTPUT
    beq @output_write

    ; Check if the command register maps to the CONFIG register
    cmp #REG_CONFIG
    beq @config_write

    ; If nothing matched, ignore and return
    jmp @done

@apu_write:
    tay                 ; Transfer APU register offset into Y
    lda cmd_value       ; Load the value into A
    sta $4000, Y        ; Store A in $4000 + Y
    jmp @done

@output_write:
    lda cmd_value       ; Load the value into A
    sta output_value    ; Store the value so it is readable
    and #$07            ; Mask so only the lower 3 bits are used
    eor #$02            ; Make sure we don't reset the I2C controller
    sta APU_PAD1        ; Store in the output control register
    jmp @done

@config_write:
    ; Config register is currently N/A, handle as a simple
    ; readable / writable byte of memory.
    lda cmd_value
    sta config_value

@done:
    ; TODO: Check if increment mode is enabled, and increment cmd_register
    rts
.endproc

;
; Read from the selected register
;
.proc register_read
    ; TODO: Read from location pointed to by cmd_register and store in cmd_value
    lda cmd_register

    ; Check if the command register maps to a readable NES APU register
    cmp #$15            ; Check if equal to $15
    beq @apu_read

    ; Check if the command register maps to the OUTPUT register
    cmp #REG_OUTPUT
    beq @output_read

    ; Check if the command register maps to the CONFIG register
    cmp #REG_CONFIG
    beq @config_read

    ; Handle the default case of an unknown register
    jmp @unknown_read

@apu_read:
    lda APU_CHANCTRL    ; Load the value from $4015 into A
    sta cmd_value       ; Store the value
    jmp @done

@output_read:
    lda output_value    ; Load the OUTPUT value into A
    sta cmd_value       ; Store the value
    jmp @done

@config_read:
    lda config_value    ; Load the CONFIG value into A
    and #$07            ; Mask the readable bits
    sta cmd_value       ; Store the value
    jmp @done

@unknown_read:
    lda #$00
    sta cmd_value

@done:
    ; TODO: Check if increment mode is enabled, and increment cmd_register
    rts
.endproc

;
; Delays Y/60 second
;
.proc delay_y_frames
:   jsr delay_frame
    dey
    bne :-
    rts
.endproc

;
; Delays 1/60 second
;
.proc delay_frame
    ; delay 29816
    lda #67
:   pha
    lda #86
    sec
:   sbc #1
    bne :-
    pla
    sbc #1
    bne :--
    rts
.endproc

;
; Handle interrupts by doing nothing
;
nmi:
irq:
    rti

.segment "RODATA"
; Static data goes here

.segment "VECTORS"
.word nmi   ;$FFFA NMI
.word start ;$FFFC Reset
.word irq   ;$FFFE IRQ
