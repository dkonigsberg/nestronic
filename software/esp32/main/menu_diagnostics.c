#include "menu_diagnostics.h"

#include "board_config.h"
#include "keypad.h"
#include "display.h"
#include "tsl2591.h"
#include "i2c_util.h"
#include "nes_player.h"

static menu_result_t diagnostics_display()
{
    menu_result_t menu_result = MENU_OK;
    uint8_t option = 0;
    uint8_t initial_contrast = display_get_contrast();
    uint8_t initial_brightness = display_get_brightness();
    uint8_t contrast = initial_contrast;
    uint8_t brightness = initial_brightness;

    keypad_clear_events();

    while (1) {
        if (option == 0) {
            display_draw_test_pattern(false);
        } else if (option == 1) {
            display_draw_test_pattern(true);
        } else if (option == 2) {
            display_draw_logo();
        }

        keypad_event_t keypad_event;
        esp_err_t ret = keypad_wait_for_event(&keypad_event, MENU_TIMEOUT_MS);

        if (ret == ESP_OK) {
            if (keypad_event.pressed) {
                if (keypad_event.key == KEYPAD_BUTTON_UP) {
                    contrast += 16;
                    display_set_contrast(contrast);
                } else if (keypad_event.key == KEYPAD_BUTTON_DOWN) {
                    contrast -= 16;
                    display_set_contrast(contrast);
                } else if (keypad_event.key == KEYPAD_BUTTON_LEFT) {
                    if (option == 0) { option = 2; }
                    else { option--; }
                } else if (keypad_event.key == KEYPAD_BUTTON_RIGHT) {
                    if (option == 2) { option = 0; }
                    else { option++; }
                } else if (keypad_event.key == KEYPAD_BUTTON_SELECT) {
                    if (brightness == 0) { brightness = 15; }
                    else { brightness--; }
                    display_set_brightness(brightness);
                } else if (keypad_event.key == KEYPAD_BUTTON_START) {
                    if (brightness >= 15) { brightness = 0; }
                    else { brightness++; }
                    display_set_brightness(brightness);
                } else if (keypad_event.key == KEYPAD_BUTTON_B) {
                    break;
                }
            }
        } else if (ret == ESP_ERR_TIMEOUT) {
            menu_result = MENU_TIMEOUT;
            break;
        }
    }
    display_set_contrast(initial_contrast);
    return menu_result;
}

static menu_result_t diagnostics_touch()
{
    menu_result_t menu_result = MENU_OK;
    char buf[128];
    int msec_elapsed = 0;

    while (1) {
        uint16_t val;
        if (keypad_touch_pad_test(&val) != ESP_OK) {
            break;
        }

        sprintf(buf, "Default time: %5d", val);

        display_static_list("Capacitive Touch", buf);

        keypad_event_t keypad_event;
        esp_err_t ret = keypad_wait_for_event(&keypad_event, 100);
        if (ret == ESP_OK) {
            msec_elapsed = 0;
            if (keypad_event.pressed && keypad_event.key != KEYPAD_TOUCH) {
                break;
            }
        }
        else if (ret == ESP_ERR_TIMEOUT) {
            msec_elapsed += 100;
            if (msec_elapsed >= MENU_TIMEOUT_MS) {
                menu_result = MENU_TIMEOUT;
                break;
            }
        }
    }
    return menu_result;
}

static menu_result_t diagnostics_ambient_light()
{
    //TODO disable non-diagnostic ambient light polling
    //TODO add support for adjusting sensor parameters

    menu_result_t menu_result = MENU_OK;
    char buf[128];
    int msec_elapsed = 0;

    while (1) {
        uint16_t ch0_val = 0;
        uint16_t ch1_val = 0;

        i2c_mutex_lock(I2C_P1_NUM);
        tsl2591_get_full_channel_data(I2C_P1_NUM, &ch0_val, &ch1_val);
        i2c_mutex_unlock(I2C_P1_NUM);

        sprintf(buf,
                "Channel 0: %5d\n"
                "Channel 1: %5d",
                ch0_val,
                ch1_val);

        display_static_list("Ambient Light Sensor", buf);

        keypad_event_t keypad_event;
        esp_err_t ret = keypad_wait_for_event(&keypad_event, 200);
        if (ret == ESP_OK) {
            msec_elapsed = 0;
            if (keypad_event.pressed && keypad_event.key != KEYPAD_TOUCH) {
                break;
            }
        }
        else if (ret == ESP_ERR_TIMEOUT) {
            msec_elapsed += 200;
            if (msec_elapsed >= (MENU_TIMEOUT_MS * 2)) {
                menu_result = MENU_TIMEOUT;
                break;
            }
        }
    }
    return menu_result;
}

static menu_result_t diagnostics_volume()
{
    menu_result_t menu_result = MENU_OK;
    char buf[128];
    int msec_elapsed = 0;

    while (1) {
        int val = adc1_get_raw(ADC1_VOL_PIN);
        int pct = (int)(((val >> 5) / 127.0) * 100);
        sprintf(buf,
                "Value: %4d\n"
                "Level: %3d%%",
                val, pct);

        display_static_list("Volume Adjustment", buf);

        keypad_event_t keypad_event;
        esp_err_t ret = keypad_wait_for_event(&keypad_event, 250);
        if (ret == ESP_OK) {
            msec_elapsed = 0;
            if (keypad_event.pressed) {
                break;
            }
        }
        else if (ret == ESP_ERR_TIMEOUT) {
            msec_elapsed += 250;
            if (msec_elapsed >= MENU_TIMEOUT_MS) {
                menu_result = MENU_TIMEOUT;
                break;
            }
        }
    }
    return menu_result;
}

menu_result_t menu_diagnostics()
{
    menu_result_t menu_result = MENU_OK;
    uint8_t option = 1;

    do {
        option = display_selection_list(
                "Diagnostics", option,
                "Display Test\n"
                "Capacitive Touch\n"
                "Ambient Light Sensor\n"
                "Volume Adjustment\n"
                "NES Test");

        if (option == 1) {
            menu_result = diagnostics_display();
        } else if (option == 2) {
            menu_result = diagnostics_touch();
        } else if (option == 3) {
            menu_result = diagnostics_ambient_light();
        } else if (option == 4) {
            menu_result = diagnostics_volume();
        } else if (option == 5) {
            nes_player_benchmark_data();
            menu_result = MENU_OK;
        } else if (option == UINT8_MAX) {
            menu_result = MENU_TIMEOUT;
        }
    } while (option > 0 && menu_result != MENU_TIMEOUT);
    return menu_result;
}
