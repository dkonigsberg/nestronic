#ifndef MAIN_MENU_H
#define MAIN_MENU_H

#include <esp_err.h>
#include <esp_types.h>

esp_err_t main_menu_start();
void main_menu_brightness_update(uint8_t value);

#endif /* MAIN_MENU_H */
