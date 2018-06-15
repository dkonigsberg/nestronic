#include "display.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>
#include <esp_log.h>
#include <esp_err.h>
#include <driver/spi_master.h>
#include <driver/gpio.h>

#include <string.h>
#include <unistd.h>

#include "board_config.h"
#include "u8g2_esp32_hal.h"
#include "u8g2.h"
#include "display_assets.h"
#include "keypad.h"

static const char *TAG = "display";

static u8g2_t u8g2;
static uint8_t display_contrast = 0x9F;
static bool menu_event_timeout = false;

/* Library function declarations */
void u8g2_DrawSelectionList(u8g2_t *u8g2, u8sl_t *u8sl, u8g2_uint_t y, const char *s);

typedef enum {
    seg_a,
    seg_b,
    seg_c,
    seg_d,
    seg_e,
    seg_f,
    seg_g,
    seg_sep
} display_seg_t;

typedef struct {
    int8_t hh;
    int8_t mm;
    uint8_t am_pm;
    uint8_t clock;
} display_time_elements_t;

esp_err_t display_init()
{
    ESP_LOGD(TAG, "display_init");

    // Configure the SPI parameters for the ESP32 HAL
    u8g2_esp32_hal_t u8g2_esp32_hal = U8G2_ESP32_HAL_DEFAULT;
    u8g2_esp32_hal.clk = SSD1322_SCK;
    u8g2_esp32_hal.mosi = SSD1322_MOSI;
    u8g2_esp32_hal.cs = SSD1322_CS;
    u8g2_esp32_hal.reset = SSD1322_RST;
    u8g2_esp32_hal.dc = SSD1322_DC;
    u8g2_esp32_hal.spi_host = SSD1322_SPI_HOST;
    u8g2_esp32_hal.dma_channel = 2;
    u8g2_esp32_hal_init(u8g2_esp32_hal);

    // Initialize the display driver
    u8g2_Setup_ssd1322_nhd_256x64_f(&u8g2, U8G2_R2, u8g2_esp32_spi_byte_cb, u8g2_esp32_gpio_and_delay_cb);
    u8g2_InitDisplay(&u8g2);
    u8g2_SetPowerSave(&u8g2, 0);

    return ESP_OK;
}

void display_clear()
{
    u8g2_ClearBuffer(&u8g2);
}

void display_set_contrast(uint8_t value)
{
    u8g2_SetContrast(&u8g2, value);
    display_contrast = value;
}

uint8_t display_get_contrast()
{
    return display_contrast;
}

void display_draw_test_pattern(bool mode)
{
    u8g2_ClearBuffer(&u8g2);
    u8g2_SetDrawColor(&u8g2, 1);

    bool draw = mode;
    for (int y = 0; y < 64; y += 16) {
        for (int x = 0; x < 256; x += 16) {
            draw = !draw;
            if (draw) {
                u8g2_DrawBox(&u8g2, x, y, 16, 16);
            }
        }
        draw = !draw;
    }

    u8g2_SendBuffer(&u8g2);
}

void display_draw_logo()
{
    u8g2_ClearBuffer(&u8g2);
    u8g2_SetBitmapMode(&u8g2, 1);
    asset_info_t asset;
    display_asset_get(&asset, ASSET_NESTRONIC);
    u8g2_DrawXBM(&u8g2, 0, 0, asset.width, asset.height, asset.bits);
    u8g2_SendBuffer(&u8g2);
}

static void display_draw_segment(u8g2_uint_t x, u8g2_uint_t y, display_seg_t segment)
{
    asset_info_t asset;
    int x_offset;
    int y_offset;
    uint8_t res;
    switch(segment) {
    case seg_a:
        res = display_asset_get(&asset, ASSET_SEG_A);
        x_offset = 0; y_offset = 0;
        break;
    case seg_b:
        res = display_asset_get(&asset, ASSET_SEG_B);
        x_offset = 28; y_offset = 0;
        break;
    case seg_c:
        res = display_asset_get(&asset, ASSET_SEG_C);
        x_offset = 28; y_offset = 32;
        break;
    case seg_d:
        res = display_asset_get(&asset, ASSET_SEG_D);
        x_offset = 0; y_offset = 56;
        break;
    case seg_e:
        res = display_asset_get(&asset, ASSET_SEG_E);
        x_offset = 0; y_offset = 32;
        break;
    case seg_f:
        res = display_asset_get(&asset, ASSET_SEG_F);
        x_offset = 0; y_offset = 0;
        break;
    case seg_g:
        res = display_asset_get(&asset, ASSET_SEG_G);
        x_offset = 0; y_offset = 28;
        break;
    case seg_sep:
        res = display_asset_get(&asset, ASSET_SEG_SEP);
        x_offset = 5; y_offset = 17;
        break;
    default:
        res = 0;
        x_offset = 0; y_offset = 0;
        break;
    }

    if (res != 0) {
        u8g2_DrawXBM(&u8g2, x + x_offset, y + y_offset,
                asset.width, asset.height, asset.bits);
    }
}

static void display_draw_digit(u8g2_uint_t x, u8g2_uint_t y, uint8_t digit)
{
    switch(digit) {
    case 0:
        display_draw_segment(x, y, seg_a);
        display_draw_segment(x, y, seg_b);
        display_draw_segment(x, y, seg_c);
        display_draw_segment(x, y, seg_d);
        display_draw_segment(x, y, seg_e);
        display_draw_segment(x, y, seg_f);
        break;
    case 1:
        display_draw_segment(x, y, seg_b);
        display_draw_segment(x, y, seg_c);
        break;
    case 2:
        display_draw_segment(x, y, seg_a);
        display_draw_segment(x, y, seg_b);
        display_draw_segment(x, y, seg_d);
        display_draw_segment(x, y, seg_e);
        display_draw_segment(x, y, seg_g);
        break;
    case 3:
        display_draw_segment(x, y, seg_a);
        display_draw_segment(x, y, seg_b);
        display_draw_segment(x, y, seg_c);
        display_draw_segment(x, y, seg_d);
        display_draw_segment(x, y, seg_g);
        break;
    case 4:
        display_draw_segment(x, y, seg_b);
        display_draw_segment(x, y, seg_c);
        display_draw_segment(x, y, seg_f);
        display_draw_segment(x, y, seg_g);
        break;
    case 5:
        display_draw_segment(x, y, seg_a);
        display_draw_segment(x, y, seg_c);
        display_draw_segment(x, y, seg_d);
        display_draw_segment(x, y, seg_f);
        display_draw_segment(x, y, seg_g);
        break;
    case 6:
        display_draw_segment(x, y, seg_a);
        display_draw_segment(x, y, seg_c);
        display_draw_segment(x, y, seg_d);
        display_draw_segment(x, y, seg_e);
        display_draw_segment(x, y, seg_f);
        display_draw_segment(x, y, seg_g);
        break;
    case 7:
        display_draw_segment(x, y, seg_a);
        display_draw_segment(x, y, seg_b);
        display_draw_segment(x, y, seg_c);
        break;
    case 8:
        display_draw_segment(x, y, seg_a);
        display_draw_segment(x, y, seg_b);
        display_draw_segment(x, y, seg_c);
        display_draw_segment(x, y, seg_d);
        display_draw_segment(x, y, seg_e);
        display_draw_segment(x, y, seg_f);
        display_draw_segment(x, y, seg_g);
        break;
    case 9:
        display_draw_segment(x, y, seg_a);
        display_draw_segment(x, y, seg_b);
        display_draw_segment(x, y, seg_c);
        display_draw_segment(x, y, seg_d);
        display_draw_segment(x, y, seg_f);
        display_draw_segment(x, y, seg_g);
        break;
    default:
        break;
    }
}

static void display_draw_time_elements(const display_time_elements_t *elements)
{
    int offset = 0;
    asset_info_t asset;

    u8g2_SetDrawColor(&u8g2, 0);
    u8g2_ClearBuffer(&u8g2);
    u8g2_SetDrawColor(&u8g2, 1);
    u8g2_SetBitmapMode(&u8g2, 1);

    if (elements->hh >= 10) {
        display_draw_digit(offset, 0, elements->hh / 10);
    }
    offset += 36 + 8;

    if (elements->hh >= 0) {
        display_draw_digit(offset, 0, elements->hh % 10);
    }
    offset += 36 + 8;

    display_draw_segment(offset, 0, seg_sep);
    offset += 18 + 8;

    if (elements->mm >= 0) {
        display_draw_digit(offset, 0, elements->mm / 10);
    }
    offset += 36 + 8;

    if (elements->mm >= 0) {
        display_draw_digit(offset, 0, elements->mm % 10);
    }
    offset += 36 + 6;

    u8g2_SetBitmapMode(&u8g2, 0);
    if (elements->am_pm == 1) {
        if (display_asset_get(&asset, ASSET_TSEG_A)) {
            u8g2_DrawXBM(&u8g2, offset, 0, asset.width, asset.height, asset.bits);
            offset += asset.width + 2;
        }
    } else if (elements->am_pm == 2) {
        if (display_asset_get(&asset, ASSET_TSEG_P)) {
            u8g2_DrawXBM(&u8g2, offset, 0, asset.width, asset.height, asset.bits);
            offset += asset.width + 2;
        }
    }
    if (elements->am_pm == 1 || elements->am_pm == 2) {
        if (display_asset_get(&asset, ASSET_TSEG_M)) {
            u8g2_DrawXBM(&u8g2, offset, 0, asset.width, asset.height, asset.bits);
        }
    }

    if (elements->clock == 1) {
        if (display_asset_get(&asset, ASSET_CLOCK_ICON)) {
            u8g2_DrawXBM(&u8g2,
                    u8g2_GetDisplayWidth(&u8g2) - asset.width,
                    u8g2_GetDisplayHeight(&u8g2) - asset.height,
                    asset.width, asset.height, asset.bits);
        }
    }
}

static int display_convert_from_twentyfour(int8_t *hh, int8_t *mm)
{
    int am_pm;
    if (*hh == 0) {
        *hh = 12;
        am_pm = 1;
    } else if (*hh == 12) {
        am_pm = 2;
    } else if (*hh > 12) {
        *hh -= 12;
        am_pm = 2;
    } else {
        am_pm = 1;
    }
    return am_pm;
}

void display_draw_time(uint8_t hh, uint8_t mm, bool twentyfour, bool clock)
{
    int show_ampm;
    int8_t hour = hh;
    int8_t minute = mm;

    if (twentyfour) {
        show_ampm = 0;
    } else {
        show_ampm = display_convert_from_twentyfour(&hour, &minute);
    }

    display_time_elements_t elements = {
            .hh = hour,
            .mm = minute,
            .am_pm = show_ampm,
            .clock = clock ? 1 : 0
    };
    display_draw_time_elements(&elements);
    u8g2_SendBuffer(&u8g2);
}

bool display_set_time(uint8_t *hh, uint8_t *mm, bool twentyfour)
{
    keypad_event_t event;
    display_time_elements_t elements;
    int cursor = 0;
    bool accepted = false;
    bool toggle = false;
    int am_pm;
    int8_t hour = *hh;
    int8_t minute = *mm;

    if (twentyfour) {
        am_pm = 0;
    } else {
        am_pm = display_convert_from_twentyfour(&hour, &minute);
    }

    do {
        elements.hh = (cursor == 0) ? (toggle ? hour : -1) : hour;
        elements.mm = (cursor == 1) ? (toggle ? minute : -1) : minute;
        elements.am_pm = (cursor == 2) ? (toggle ? am_pm : 0) : am_pm;
        display_draw_time_elements(&elements);
        u8g2_SendBuffer(&u8g2);
        toggle = !toggle;

        if (keypad_wait_for_event(&event, 400) == ESP_OK) {
            if (event.pressed) {
                if (event.key == KEYPAD_BUTTON_LEFT) {
                    if (cursor > 0) { cursor--; }
                    else { cursor = twentyfour ? 1 : 2; }
                }
                else if (event.key == KEYPAD_BUTTON_RIGHT) {
                    if (cursor < (twentyfour ? 1 : 2)) { cursor++; }
                    else { cursor = 0; }
                }
                else if (event.key == KEYPAD_BUTTON_UP) {
                    if (cursor == 0) {
                        if (twentyfour) {
                            if (hour == 23) { hour = 0;}
                            else { hour++; }
                        } else {
                            if (hour == 12) { hour = 1; }
                            else { hour++; }
                        }
                    } else if (cursor == 1) {
                        if (minute == 59) { minute = 0;}
                        else { minute++; }
                    } else if (cursor == 2) {
                        if (am_pm == 1) { am_pm = 2; }
                        else if (am_pm == 2) { am_pm = 1; }
                    }
                    toggle = true;
                }
                else if (event.key == KEYPAD_BUTTON_DOWN) {
                    if (cursor == 0) {
                        if (twentyfour) {
                            if (hour == 0) { hour = 23;}
                            else { hour--; }
                        } else {
                            if (hour == 1) { hour = 12; }
                            else { hour--; }
                        }
                    } else if (cursor == 1) {
                        if (minute == 0) { minute = 59;}
                        else { minute--; }
                    } else if (cursor == 2) {
                        if (am_pm == 1) { am_pm = 2; }
                        else if (am_pm == 2) { am_pm = 1; }
                    }
                    toggle = true;
                }
                else if (event.key == KEYPAD_BUTTON_A) {
                    // Input accepted
                    accepted = true;
                    break;
                }
                else if (event.key == KEYPAD_BUTTON_B) {
                    // Input canceled
                    break;
                }
            }
        }
    } while (1);

    if (accepted) {
        if (twentyfour) {
            *hh = hour;
        } else {
            if (am_pm == 1) {
                if (hour == 12) {
                    *hh = 0;
                } else {
                    *hh = hour;
                }
            }
            else {
                if (hour == 12) {
                    *hh = hour;
                } else {
                    *hh = hour + 12;
                }
            }
        }
        *mm = minute;
    }
    return accepted;
}

uint8_t u8x8_GetMenuEvent(u8x8_t *u8x8)
{
    // This function should override a u8g2 framework function with the
    // same name, due to its declaration with the "weak" pragma.
    menu_event_timeout = false;
    keypad_event_t event;
    esp_err_t ret = keypad_wait_for_event(&event, MENU_TIMEOUT_MS);
    if (ret == ESP_OK) {
        if (event.pressed) {
            switch (event.key) {
            case KEYPAD_BUTTON_LEFT:
                return U8X8_MSG_GPIO_MENU_PREV;
            case KEYPAD_BUTTON_UP:
                return U8X8_MSG_GPIO_MENU_UP;
            case KEYPAD_BUTTON_DOWN:
                return U8X8_MSG_GPIO_MENU_DOWN;
            case KEYPAD_BUTTON_RIGHT:
                return U8X8_MSG_GPIO_MENU_NEXT;
            case KEYPAD_BUTTON_A:
                return U8X8_MSG_GPIO_MENU_SELECT;
            case KEYPAD_BUTTON_B:
                return U8X8_MSG_GPIO_MENU_HOME;
            default:
                break;
            }
        }
    } else if (ret == ESP_ERR_TIMEOUT) {
        menu_event_timeout = true;
        return U8X8_MSG_GPIO_MENU_HOME;
    }
    return 0;
}

static void display_prepare_menu_font()
{
    u8g2_SetFont(&u8g2, u8g2_font_pxplusibmcga_8f);
    u8g2_SetFontMode(&u8g2, 0);
    u8g2_SetDrawColor(&u8g2, 1);
}

uint8_t display_message(const char *title1, const char *title2, const char *title3, const char *buttons)
{
    display_prepare_menu_font();
    keypad_clear_events();
    uint8_t option = u8g2_UserInterfaceMessage(&u8g2, title1, title2, title3, buttons);
    return menu_event_timeout ? UINT8_MAX : option;
}

void display_static_message(const char *title1, const char *title2, const char *title3)
{
    // Based off u8g2_UserInterfaceMessage() with button code removed.

    uint8_t height;
    uint8_t line_height;
    u8g2_uint_t pixel_height;
    u8g2_uint_t y;

    display_prepare_menu_font();
    display_clear();

    /* only horizontal strings are supported, so force this here */
    u8g2_SetFontDirection(&u8g2, 0);

    /* force baseline position */
    u8g2_SetFontPosBaseline(&u8g2);

    /* calculate line height */
    line_height = u8g2_GetAscent(&u8g2);
    line_height -= u8g2_GetDescent(&u8g2);

    /* calculate overall height of the message box in lines*/
    height = 1;   /* button line */
    height += u8x8_GetStringLineCnt(title1);
    if (title2) {
        height++;
    }
    height += u8x8_GetStringLineCnt(title3);

    /* calculate the height in pixel */
    pixel_height = height;
    pixel_height *= line_height;

    /* calculate offset from top */
    y = 0;
    if (pixel_height < u8g2_GetDisplayHeight(&u8g2)) {
        y = u8g2_GetDisplayHeight(&u8g2);
        y -= pixel_height;
        y /= 2;
    }
    y += u8g2_GetAscent(&u8g2);

    y += u8g2_DrawUTF8Lines(&u8g2, 0, y, u8g2_GetDisplayWidth(&u8g2), line_height, title1);

    if (title2) {
        u8g2_DrawUTF8Line(&u8g2, 0, y, u8g2_GetDisplayWidth(&u8g2), title2, 0, 0);
        y += line_height;
    }

    u8g2_DrawUTF8Lines(&u8g2, 0, y, u8g2_GetDisplayWidth(&u8g2), line_height, title3);

    u8g2_SendBuffer(&u8g2);
}

uint8_t display_selection_list(const char *title, uint8_t start_pos, const char *list)
{
    display_prepare_menu_font();
    keypad_clear_events();
    uint8_t option = u8g2_UserInterfaceSelectionList(&u8g2, title, start_pos, list);
    return menu_event_timeout ? UINT8_MAX : option;
}

void display_static_list(const char *title, const char *list)
{
    // Based off u8g2_UserInterfaceSelectionList() with changes to use
    // full frame buffer mode and to remove actual menu functionality.

    display_prepare_menu_font();
    display_clear();

    u8sl_t u8sl;
    u8g2_uint_t yy;

    u8g2_uint_t line_height = u8g2_GetAscent(&u8g2) - u8g2_GetDescent(&u8g2) + 1;

    uint8_t title_lines = u8x8_GetStringLineCnt(title);
    uint8_t display_lines;

    if (title_lines > 0) {
        display_lines = (u8g2_GetDisplayHeight(&u8g2) - 3) / line_height;
        u8sl.visible = display_lines;
        u8sl.visible -= title_lines;
    }
    else {
        display_lines = u8g2_GetDisplayHeight(&u8g2) / line_height;
        u8sl.visible = display_lines;
    }

    u8sl.total = u8x8_GetStringLineCnt(list);
    u8sl.first_pos = 0;
    u8sl.current_pos = -1;

    u8g2_SetFontPosBaseline(&u8g2);

    yy = u8g2_GetAscent(&u8g2);
    if (title_lines > 0) {
        yy += u8g2_DrawUTF8Lines(&u8g2, 0, yy, u8g2_GetDisplayWidth(&u8g2), line_height, title);
        u8g2_DrawHLine(&u8g2, 0, yy - line_height - u8g2_GetDescent(&u8g2) + 1, u8g2_GetDisplayWidth(&u8g2));
        yy += 3;
    }
    u8g2_DrawSelectionList(&u8g2, &u8sl, yy, list);

    u8g2_SendBuffer(&u8g2);
}

static const char* const INPUT_CHARS[] = {
        "ABCDEFGHIJKLM0123456789-=",
        "NOPQRSTUVWXYZ!@#$%^&*()_+",
        "abcdefghijklm[]{}\\|;:'\",.",
        "nopqrstuvwxyz/<>?`~\xFA\xFB\xFC \xFD\xFE"
};

static void display_draw_input_char(u8g2_uint_t x, u8g2_uint_t y, uint16_t ch, uint8_t char_width, u8g2_uint_t line_height)
{
    if (ch == ' ') {
        u8g2_DrawLine(&u8g2, x, y - 1, (x + char_width) - 1, y - 1);
        u8g2_DrawLine(&u8g2, x, y - 2, x, y - 4);
        u8g2_DrawLine(&u8g2, (x + char_width) - 1, y - 2, (x + char_width) - 1, y - 4);
    } else if (ch == 0xFD) {
        u8g2_DrawLine(&u8g2, x + 1, y - 5, (x + char_width) - 1, y - 5);
        u8g2_DrawLine(&u8g2, x + 1, y - 4, (x + char_width) - 1, y - 4);
        u8g2_DrawPixel(&u8g2, x + 2, y - 6);
        u8g2_DrawPixel(&u8g2, x + 3, y - 6);
        u8g2_DrawPixel(&u8g2, x + 3, y - 7);
        u8g2_DrawPixel(&u8g2, x + 2, y - 3);
        u8g2_DrawPixel(&u8g2, x + 3, y - 3);
        u8g2_DrawPixel(&u8g2, x + 3, y - 2);
    } else if (ch == 0xFE) {
        u8g2_DrawLine(&u8g2, x + 1, y - 1, x + 1, y - 4);
        u8g2_DrawBox(&u8g2, x + 3, y - 4, 4, 4);
        u8g2_DrawBox(&u8g2, x + 4, y - 6, 2, 2);
        u8g2_DrawPixel(&u8g2, x + 5, y - 7);
        u8g2_DrawPixel(&u8g2, x + 7, y - 4);
        u8g2_DrawPixel(&u8g2, x + 7, y - 3);
    } else if (ch >= 0xFA) {
        u8g2_DrawGlyph(&u8g2, x, y, ' ');
    } else {
        u8g2_DrawGlyph(&u8g2, x, y, ch);
    }
}

static void display_draw_input_grid(u8g2_uint_t y, char selected)
{
    uint8_t char_width = u8g2_GetMaxCharWidth(&u8g2);
    u8g2_uint_t line_height = u8g2_GetAscent(&u8g2) - u8g2_GetDescent(&u8g2) + 1;

    uint16_t ch;
    for (uint8_t row = 0; row < 4; row++) {
        u8g2_uint_t x = 3;
        for (uint8_t i = 0; i < 25; i++) {
            ch = INPUT_CHARS[row][i];
            if (ch == '\0') { break; }

            if (ch == selected) {
                u8g2_SetDrawColor(&u8g2, 1);
                u8g2_DrawBox(&u8g2, x - 1, y - line_height, char_width + 2, line_height + 1);
                u8g2_SetDrawColor(&u8g2, 0);
                display_draw_input_char(x, y, ch, char_width, line_height);
                u8g2_SetDrawColor(&u8g2, 1);
            } else {
                u8g2_SetDrawColor(&u8g2, 0);
                u8g2_DrawBox(&u8g2, x - 1, y - line_height, char_width + 2, line_height + 1);
                u8g2_SetDrawColor(&u8g2, 1);
                display_draw_input_char(x, y, ch, char_width, line_height);
            }

            x += char_width + 2;
        }
        y += line_height + 1;
    }
}

uint8_t display_input_text(const char *title, char **text)
{

    display_prepare_menu_font();
    display_clear();

    char str[31];
    uint8_t event;
    u8g2_uint_t yy;
    u8g2_uint_t grid_y;
    u8g2_uint_t line_height = u8g2_GetAscent(&u8g2) - u8g2_GetDescent(&u8g2) + 1;
    uint8_t char_width = u8g2_GetMaxCharWidth(&u8g2);

    bzero(str, sizeof(str));

    u8g2_SetFontPosBaseline(&u8g2);

    yy = u8g2_GetAscent(&u8g2);
    yy += u8g2_DrawUTF8Lines(&u8g2, 0, yy, u8g2_GetDisplayWidth(&u8g2), line_height, title);
    u8g2_DrawHLine(&u8g2, 0, yy - line_height - u8g2_GetDescent(&u8g2) + 1, u8g2_GetDisplayWidth(&u8g2));
    yy += 3;

    grid_y = u8g2_GetDisplayHeight(&u8g2) - (line_height + 1) * 3 - 1;

    u8g2_DrawGlyph(&u8g2, 0, yy, ']');

    uint8_t cursor = 0;
    uint8_t row = 0;
    uint8_t col = 0;
    bool accepted = false;

    if (text && *text && strlen(*text) > 0) {
        strncpy(str, *text, sizeof(str));
        cursor = strlen(str);
    }

    for(;;) {
        char ch = INPUT_CHARS[row][col];

        uint8_t cursor_x = (cursor + 1) * char_width;

        u8g2_SetDrawColor(&u8g2, 0);
        u8g2_DrawBox(&u8g2, char_width, yy - line_height, u8g2_GetDisplayWidth(&u8g2) - char_width, line_height);

        u8g2_SetDrawColor(&u8g2, 1);
        u8g2_DrawUTF8(&u8g2, char_width, yy, str);
        u8g2_DrawLine(&u8g2, cursor_x, yy - 1, (cursor_x + char_width) - 1, yy - 1);
        u8g2_DrawLine(&u8g2, cursor_x, yy - 2, (cursor_x + char_width) - 1, yy - 2);

        display_draw_input_grid(grid_y, ch);
        u8g2_SendBuffer(&u8g2);

        event = u8x8_GetMenuEvent(u8g2_GetU8x8(&u8g2));

        if (event == U8X8_MSG_GPIO_MENU_SELECT) {
            if (ch == 0xFD) {
                if (cursor > 0) {
                    cursor--;
                    str[cursor] = '\0';
                }
            } else if (ch == 0xFE) {
                accepted = true;
                break;
            } else if (cursor < sizeof(str) - 1) {
                str[cursor++] = ch;
            }
        }
        else if (event == U8X8_MSG_GPIO_MENU_HOME) {
            break;
        }
        else if (event == U8X8_MSG_GPIO_MENU_NEXT) {
            col = (col >= 24) ? 0 : col + 1;
            if (row == 3 && col >= 19 && col < 22) {
                col = 22;
            }
        }
        else if (event == U8X8_MSG_GPIO_MENU_PREV) {
            col = (col == 0) ? 24 : col - 1;
            if (row == 3 && col >= 19 && col < 22) {
                col = 18;
            }
        }
        else if (event == U8X8_MSG_GPIO_MENU_DOWN) {
            row = (row >= 3) ? 0 : row + 1;
            if (row == 3 && col >= 19 && col < 22) {
                row = 0;
            }
        }
        else if (event == U8X8_MSG_GPIO_MENU_UP) {
            row = (row == 0) ? 3 : row - 1;
            if (row == 3 && col >= 19 && col < 22) {
                row = 2;
            }
        }
    }

    uint8_t result;

    if (accepted) {
        result = strlen(str);
        if (text && result > 0) {
            char *output = malloc(result + 1);
            if (!output) {
                return 0;
            }
            strcpy(output, str);
            if (*text) {
                free(*text);
            }
            *text = output;
        }
    } else {
        result = 0;
    }

    return result;
}

uint8_t display_input_value(const char *title, const char *prefix, uint8_t *value,
        uint8_t low, uint8_t high, uint8_t digits, const char *postfix)
{
    display_prepare_menu_font();
    keypad_clear_events();
    uint8_t option = u8g2_UserInterfaceInputValue(&u8g2, title, prefix, value, low, high, digits, postfix);
    return menu_event_timeout ? UINT8_MAX : option;
}
