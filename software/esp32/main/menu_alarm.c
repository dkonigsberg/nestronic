#include "menu_alarm.h"

#include <esp_log.h>
#include <esp_err.h>
#include <string.h>

#include "settings.h"
#include "board_config.h"
#include "display.h"
#include "vgm.h"
#include "nsf.h"
#include "vpool.h"

static const char *TAG = "menu_alarm";

static void menu_set_alarm_time()
{
    uint8_t hh;
    uint8_t mm;
    bool time_twentyfour;

    if (settings_get_alarm_time(&hh, &mm) != ESP_OK) {
        return;
    }

    if (settings_get_time_format(&time_twentyfour) != ESP_OK) {
        time_twentyfour = false;
    }

    if (display_set_time(&hh, &mm, time_twentyfour)) {
        settings_set_alarm_time(hh, mm);
        ESP_LOGI(TAG, "Alarm time set: %02d:%02d", hh, mm);
    }
}

static menu_result_t alarm_tune_file_picker_vgm(const char *filename)
{
    menu_result_t menu_result = MENU_CANCEL;
    esp_err_t ret;
    vgm_file_t *vgm_file;
    vgm_gd3_tags_t *tags_result = 0;

    ret = vgm_open(&vgm_file, filename);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open VGM file");
        display_message("Error", "File could not be opened", NULL, " OK ");
        return MENU_CANCEL;
    }

    if (vgm_get_header(vgm_file)->nes_apu_fds) {
        ESP_LOGE(TAG, "FDS Add-on is not supported");
        vgm_free(vgm_file);
        display_message("Error", "File is not supported", NULL, " OK ");
        return MENU_CANCEL;
    }

    if (vgm_read_gd3_tags(&tags_result, vgm_file) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read GD3 tags");
        vgm_free(vgm_file);
        display_message("Error", "File could not be read", NULL, " OK ");
        return MENU_CANCEL;
    }

    vgm_free(vgm_file);

    do {
        uint8_t option = display_message(
                tags_result->game_name ? tags_result->game_name : "Unknown",
                tags_result->track_name ? tags_result->track_name : "Unknown",
                "",
                " Select \n Play ");
        if (option == 1) {
            if (settings_set_alarm_tune(filename, tags_result->game_name, tags_result->track_name, 0) == ESP_OK) {
                menu_result = MENU_OK;
            }
            break;
        } else if (option == 2) {
            main_menu_file_picker_play_vgm(filename);
        } else if (option == UINT8_MAX) {
            menu_result = MENU_TIMEOUT;
            break;
        } else if (option  == 0) {
            break;
        }
    } while (menu_result != MENU_TIMEOUT);

    vgm_free_gd3_tags(tags_result);

    return menu_result;
}

static menu_result_t alarm_tune_file_picker_nsf(const char *filename)
{
    menu_result_t menu_result = MENU_CANCEL;
    nsf_header_t header;
    if (nsf_read_header(filename, &header) != ESP_OK) {
        return MENU_CANCEL;
    }

    bool has_name = header.name && strlen(header.name) > 0 && strcmp(header.name, "<?>") != 0;
    bool has_artist = header.artist && strlen(header.artist) > 0 && strcmp(header.artist, "<?>") != 0;
    bool has_copyright = header.copyright && strlen(header.copyright) > 0 && strcmp(header.copyright, "<?>") != 0;

    struct vpool vp;
    vpool_init(&vp, 128, 0);
    if (has_name) {
        vpool_insert(&vp, vpool_get_length(&vp), (char *)header.name, strlen(header.name));
        vpool_insert(&vp, vpool_get_length(&vp), "\n", 1);
    }
    if (has_artist) {
        vpool_insert(&vp, vpool_get_length(&vp), (char *)header.artist, strlen(header.artist));
        vpool_insert(&vp, vpool_get_length(&vp), "\n", 1);
    }
    if (has_copyright) {
        vpool_insert(&vp, vpool_get_length(&vp), (char *)header.copyright, strlen(header.copyright));
        vpool_insert(&vp, vpool_get_length(&vp), "\n", 1);
    }
    vpool_insert(&vp, vpool_get_length(&vp), "\0", 1);
    char post[8];
    sprintf(post, "/%d", header.total_songs);

    uint8_t result = 0;
    uint8_t value_sel = 1;
    do {
        result = display_input_value((char *)vpool_get_buf(&vp), "Song: ", &value_sel, 1, header.total_songs, 3, post);
        if (result == UINT8_MAX) {
            menu_result = MENU_TIMEOUT;
            break;
        }

        if (result == 1) {
            do {
                char song_sel[10];
                sprintf(song_sel, "<%d/%d>", value_sel, header.total_songs);
                uint8_t option = display_message(
                        has_name ? header.name : "Unknown",
                        has_artist ? header.artist : "",
                        song_sel,
                        " Select \n Play ");
                if (option == 1) {
                    if (settings_set_alarm_tune(filename, header.name, song_sel, value_sel) == ESP_OK) {
                        menu_result = MENU_OK;
                    }
                    break;
                } else if (option == 2) {
                    main_menu_file_picker_play_nsf(filename, value_sel);
                } else if (option == UINT8_MAX) {
                    menu_result = MENU_TIMEOUT;
                    break;
                } else if (option  == 0) {
                    break;
                }
            } while (true);
        }
    } while (result == 1 && menu_result != MENU_OK && menu_result != MENU_TIMEOUT);

    vpool_final(&vp);
    return menu_result;
}

static menu_result_t alarm_tune_file_picker_cb(const char *filename)
{
    ESP_LOGI(TAG, "File: \"%s\"", filename);

    char *dot = strrchr(filename, '.');
    if (dot && (!strcmp(dot, ".vgm") || !strcmp(dot, ".vgz"))) {
        return alarm_tune_file_picker_vgm(filename);
    } else if (dot && !strcmp(dot, ".nsf")) {
        return alarm_tune_file_picker_nsf(filename);
    } else {
        return MENU_CANCEL;
    }
}

menu_result_t menu_set_alarm()
{
    menu_result_t menu_result = MENU_OK;
    bool time_twentyfour;
    char buf_time[128];
    char buf_tune[128];

    if (settings_get_time_format(&time_twentyfour) != ESP_OK) {
        time_twentyfour = false;
    }

    do {
        uint8_t hh;
        uint8_t mm;
        if (settings_get_alarm_time(&hh, &mm) != ESP_OK) {
            return menu_result;
        }

        if (time_twentyfour) {
            sprintf(buf_time, "%02d:%02d", hh, mm);
        } else {
            int8_t hour = hh;
            int8_t minute = mm;
            int ampm = display_convert_from_twentyfour(&hour, &minute);
            sprintf(buf_time, "%d:%02d %s", hour, minute, (ampm == 1) ? "AM" : "PM");
        }

        char *filename = 0;
        char *title = 0;
        char *subtitle = 0;
        esp_err_t ret = settings_get_alarm_tune(&filename, &title, &subtitle, NULL);
        if (ret != ESP_OK || !filename || strlen(filename) == 0) {
            snprintf(buf_tune, 128, "\n%s\n%s", "[Unset]", "");
        } else if (title && strlen(title) > 0) {
            snprintf(buf_tune, 128, "\n%s\n%s",
                    title,
                    (subtitle && strlen(subtitle) > 0) ? subtitle : "");
        } else {
            snprintf(buf_tune, 128, "\n%s\n%s", filename, "");
        }
        free(filename);
        free(title);
        free(subtitle);

        uint8_t option = display_message(
                "Set Alarm\n",
                buf_time, buf_tune,
                " Set Time \n Select Tune ");

        if (option == 1) {
            menu_set_alarm_time();
        } else if (option == 2) {
            menu_result = show_file_picker("Select Alarm Tune", alarm_tune_file_picker_cb);
        } else if (option == UINT8_MAX) {
            menu_result = MENU_TIMEOUT;
            break;
        } else if (option == 0) {
            break;
        }
    } while (menu_result != MENU_TIMEOUT);
    return menu_result;
}
