#ifndef DISPLAY_H
#define DISPLAY_H

#include <esp_err.h>
#include <esp_types.h>

typedef enum {
    MENU_SELECT = 0,
    MENU_NEXT,
    MENU_PREV,
    MENU_HOME,
    MENU_UP,
    MENU_DOWN,
    MENU_MAX
} display_menu_key_t;

esp_err_t display_init();

void display_clear();
void display_set_contrast(uint8_t value);
uint8_t display_get_contrast();

void display_draw_test_pattern(bool mode);
void display_draw_logo();
void display_draw_time(uint8_t hh, uint8_t mm, bool twentyfour, bool clock);

bool display_set_time(uint8_t *hh, uint8_t *mm, bool twentyfour);
uint8_t display_message(const char *title1, const char *title2, const char *title3, const char *buttons);
void display_static_message(const char *title1, const char *title2, const char *title3);
uint8_t display_selection_list(const char *title, uint8_t start_pos, const char *list);
void display_static_list(const char *title, const char *list);
uint8_t display_input_text(const char *title, char **text);
uint8_t display_input_value(const char *title, const char *prefix, uint8_t *value,
        uint8_t low, uint8_t high, uint8_t digits, const char *postfix);

#endif /* DISPLAY_H */
