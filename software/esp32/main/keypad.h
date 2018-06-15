#ifndef KEYPAD_H
#define KEYPAD_H

#include <esp_err.h>
#include <stdint.h>
#include <stdbool.h>

/**
 * Represents a key press or release event
 *
 * Key codes 1..80 are from the keypad array
 * Key codes 97..104 are for Row GPI key events
 * Key codes 105..114 are for Column GPI key events
 * Key code 200 is the capacitive touch pad
 */
typedef struct {
    uint8_t key;
    bool pressed;
} keypad_event_t;

esp_err_t keypad_init();

esp_err_t keypad_inject_event(const keypad_event_t *event);
esp_err_t keypad_clear_events();
esp_err_t keypad_flush_events();
esp_err_t keypad_wait_for_event(keypad_event_t *event, int msecs_to_wait);

esp_err_t keypad_touch_pad_test(uint16_t *val);

esp_err_t keypad_int_event_handler();

#endif /* KEYPAD_H */
