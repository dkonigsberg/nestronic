#ifndef MAIN_MENU_H
#define MAIN_MENU_H

#include <esp_err.h>
#include <esp_types.h>

typedef enum {
    MENU_OK = 0,
    MENU_CANCEL = 1,
    MENU_TIMEOUT = 99
} menu_result_t;

typedef menu_result_t (*file_picker_cb_t)(const char *filename);

esp_err_t main_menu_start();
void main_menu_brightness_update(uint8_t value);
const char* find_list_option(const char *list, int option, size_t *length);
menu_result_t show_file_picker(const char *title, file_picker_cb_t cb);
menu_result_t main_menu_file_picker_play_vgm(const char *filename);
menu_result_t main_menu_file_picker_play_nsf(const char *filename, uint8_t song);

#endif /* MAIN_MENU_H */
