#include "menu_demo.h"

#include <esp_log.h>
#include <esp_err.h>
#include <string.h>

#include "display.h"
#include "nes_player.h"

static const char *TAG = "menu_demo";

static menu_result_t main_menu_file_picker_cb(const char *filename)
{
    ESP_LOGI(TAG, "File: \"%s\"", filename);

    // Make this a little less synchronous at some point,
    // and implement some sort of playback UI.

    char *dot = strrchr(filename, '.');
    if (dot && (!strcmp(dot, ".vgm") || !strcmp(dot, ".vgz"))) {
        main_menu_file_picker_play_vgm(filename);
    } else if (dot && !strcmp(dot, ".nsf")) {
        main_menu_file_picker_play_nsf(filename, 0);
    }

    // Remain in the picker
    return MENU_CANCEL;
}

menu_result_t menu_demo_playback()
{
    if (show_file_picker("Demo Playback", main_menu_file_picker_cb) == MENU_TIMEOUT) {
        return MENU_TIMEOUT;
    } else {
        return MENU_OK;
    }
}

menu_result_t menu_demo_sound_effects()
{
    menu_result_t menu_result = MENU_OK;
    uint8_t option = 1;

    do {
        option = display_selection_list(
                "Demo Sound Effects", option,
                "Chime\n"
                "Blip\n"
                "Credit");

        if (option == 1) {
            nes_player_play_effect(NES_PLAYER_EFFECT_CHIME, NES_REPEAT_NONE);
        } else if (option == 2) {
            nes_player_play_effect(NES_PLAYER_EFFECT_BLIP, NES_REPEAT_NONE);
        } else if (option == 3) {
            nes_player_play_effect(NES_PLAYER_EFFECT_CREDIT, NES_REPEAT_NONE);
        } else if (option == UINT8_MAX) {
            menu_result = MENU_TIMEOUT;
        }
    } while (option > 0 && menu_result != MENU_TIMEOUT);
    return menu_result;
}
