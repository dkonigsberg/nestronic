#include "menu_about.h"

#include <esp_ota_ops.h>
#include <string.h>

#include "display.h"

menu_result_t menu_about()
{
    menu_result_t menu_result = MENU_OK;
    char buf[512];
    const esp_app_desc_t *app_desc = esp_ota_get_app_description();

    sprintf(buf,
            "VGM Player Alarm Clock\n"
            "\n"
            "%s\n"
            "%s %s\n"
            "ESP-IDF: %s",
            app_desc->version,
            app_desc->date, app_desc->time,
            app_desc->idf_ver);

    uint8_t option = display_message(
            "Nestronic",
            NULL,
            buf, " OK ");
    if (option == UINT8_MAX) {
        menu_result = MENU_TIMEOUT;
    }
    return menu_result;
}
