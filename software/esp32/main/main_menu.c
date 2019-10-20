/*
 * Implementation of the menu system for the UI
 */
#include "main_menu.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>
#include <esp_system.h>
#include <esp_log.h>
#include <esp_err.h>
#include <esp_wifi.h>
#include <tcpip_adapter.h>
#include <driver/adc.h>
#include <stdbool.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/time.h>
#include <dirent.h>
#include <string.h>

#include "board_config.h"
#include "settings.h"
#include "vpool.h"
#include "display.h"
#include "board_rtc.h"
#include "keypad.h"
#include "sdcard_util.h"
#include "nes_player.h"
#include "vgm.h"
#include "nsf.h"
#include "bsdlib.h"
#include "menu_about.h"
#include "menu_diagnostics.h"
#include "menu_setup.h"
#include "menu_alarm.h"
#include "menu_demo.h"

static const char *TAG = "main_menu";

static TaskHandle_t main_menu_task_handle;
static SemaphoreHandle_t clock_mutex = NULL;
static bool menu_visible = false;
static bool time_twentyfour = false;
static uint8_t contrast_value = 0x9F;
static bool alarm_set = false;
static uint8_t alarm_hh;
static uint8_t alarm_mm;
static bool alarm_triggered = false;
static bool alarm_running = false;
static uint8_t alarm_frame = 0;
static TimerHandle_t alarm_complete_timer = 0;
static TimerHandle_t alarm_snooze_timer = 0;
static struct tm timeinfo_prev = {0};

const char* find_list_option(const char *list, int option, size_t *length)
{
    const char *p = 0;
    const char *q = 0;
    size_t len = strlen(list);
    for (int i = 0; i < option; i++) {
        if (!p && !q) {
            p = list;
        } else {
            p = q + 1;
        }
        q = strchr(p, '\n');
        if (!q && i + 1 == option) {
            q = list + len;
        }
        if (p >= q) {
            p = 0;
            q = 0;
            break;
        }
    }

    if (p && q) {
        if (length) {
            *length = q - p;
        }
    } else {
        p = 0;
        q = 0;
    }
    return p;
}

static menu_result_t show_file_picker_impl(const char *title, const char *path, file_picker_cb_t cb)
{
    struct vpool vp;
    struct dirent **namelist;
    int n;

    n = scandir(path, &namelist, NULL, alphasort);
    if (n < 0) {
        if (!sdcard_is_detected()) {
            display_message("Error", "SD card was not detected", NULL, " OK ");
        } else if (!sdcard_is_mounted()) {
            display_message("Error", "SD card could not be accessed", NULL, " OK ");
        } else {
            display_message("Error", "Could not open the directory", NULL, " OK ");
        }
        return MENU_CANCEL;
    }

    vpool_init(&vp, 1024, 0);

    int count = 0;
    for (int i = 0; i < n; i++) {
        if (count < UINT8_MAX - 2) {
            if (namelist[i]->d_type == DT_REG) {
                char *dot = strrchr(namelist[i]->d_name, '.');
                if (dot && (!strcmp(dot, ".vgm") || !strcmp(dot, ".vgz") || !strcmp(dot, ".nsf"))) {
                    vpool_insert(&vp, vpool_get_length(&vp), namelist[i]->d_name, strlen(namelist[i]->d_name));
                    vpool_insert(&vp, vpool_get_length(&vp), "\n", 1);
                }
            } else if (namelist[i]->d_type == DT_DIR && namelist[i]->d_name[0] != '.') {
                vpool_insert(&vp, vpool_get_length(&vp), namelist[i]->d_name, strlen(namelist[i]->d_name));
                vpool_insert(&vp, vpool_get_length(&vp), "/\n", 2);
            }
        }
        free(namelist[i]);
        count++;
    }
    free(namelist);

    if (vpool_is_empty(&vp)) {
        display_message("Error", "No files found", NULL, " OK ");
        vpool_final(&vp);
        return MENU_OK;
    }

    char *list = (char *) vpool_get_buf(&vp);
    size_t len = vpool_get_length(&vp);
    list[len - 1] = '\0';

    menu_result_t menu_result = MENU_CANCEL;
    uint8_t option = 1;
    do {
        option = display_selection_list(
                title, option,
                list);
        if (option == UINT8_MAX) {
            menu_result = MENU_TIMEOUT;
            break;
        }

        size_t file_len;
        const char *value = find_list_option(list, option, &file_len);

        if (value) {
            size_t pre_len = strlen(path);
            char *filename = malloc(pre_len + file_len + 2);
            if (!filename) {
                menu_result = MENU_CANCEL;
                break;
            }
            strcpy(filename, path);
            filename[pre_len] = '/';
            strncpy(filename + pre_len + 1, value, file_len);
            filename[pre_len + file_len + 1] = '\0';

            if (filename[pre_len + file_len] == '/') {
                char *dir_title = strndup(value, file_len);
                if (!dir_title) {
                    menu_result = MENU_CANCEL;
                    break;
                }
                filename[pre_len + file_len] = '\0';
                menu_result = show_file_picker_impl(dir_title, filename, cb);
            } else if (cb) {
                menu_result = cb(filename);
            } else {
                menu_result = MENU_OK;
            }
            free(filename);
            if (menu_result == MENU_OK) {
                break;
            }
        }
    } while (option > 0 && menu_result != MENU_TIMEOUT);

    vpool_final(&vp);

    return menu_result;
}

menu_result_t show_file_picker(const char *title, file_picker_cb_t cb)
{
    return show_file_picker_impl(title, "/sdcard", cb);
}

static void main_menu_demo_playback_cb(nes_playback_state_t state)
{
    if (state == NES_PLAYER_FINISHED) {
        xTaskNotifyGive(main_menu_task_handle);
    }
}

menu_result_t main_menu_file_picker_play_vgm(const char *filename)
{
    // The player currently has code to parse the GD3 tags and
    // show them on the display.
    const vgm_gd3_tags_t *tags = NULL;
    if (nes_player_play_vgm_file(filename, NES_REPEAT_NONE, main_menu_demo_playback_cb, &tags) == ESP_OK) {
        struct vpool vp;
        vpool_init(&vp, 1024, 0);
        if (tags->game_name) {
            vpool_insert(&vp, vpool_get_length(&vp), tags->game_name, strlen(tags->game_name));
            vpool_insert(&vp, vpool_get_length(&vp), "\n", 1);
        }
        if (tags->track_name) {
            vpool_insert(&vp, vpool_get_length(&vp), tags->track_name, strlen(tags->track_name));
            vpool_insert(&vp, vpool_get_length(&vp), "\n", 1);
        }
        if (tags->track_author) {
            vpool_insert(&vp, vpool_get_length(&vp), tags->track_author, strlen(tags->track_author));
            vpool_insert(&vp, vpool_get_length(&vp), "\n", 1);
        }
        if (tags->game_release) {
            vpool_insert(&vp, vpool_get_length(&vp), tags->game_release, strlen(tags->game_release));
            vpool_insert(&vp, vpool_get_length(&vp), "\n", 1);
        }
        if (tags->vgm_author) {
            vpool_insert(&vp, vpool_get_length(&vp), tags->vgm_author, strlen(tags->vgm_author));
        }
        vpool_insert(&vp, vpool_get_length(&vp), "\0", 1);

        display_clear();
        display_static_list("VGM Player", (char *)vpool_get_buf(&vp));
        vpool_final(&vp);

        while (ulTaskNotifyTake(pdTRUE, 100 / portTICK_RATE_MS) == 0) {
            keypad_event_t keypad_event;
            if (keypad_wait_for_event(&keypad_event, 0) == ESP_OK) {
                if (keypad_event.pressed && keypad_event.key == KEYPAD_BUTTON_B) {
                    nes_player_stop();
                }
            }
        }
    }
    return MENU_OK;
}

menu_result_t main_menu_file_picker_play_nsf(const char *filename, uint8_t song)
{
    nsf_header_t header;
    if (nsf_read_header(filename, &header) != ESP_OK) {
        return MENU_OK;
    }

    struct vpool vp;
    vpool_init(&vp, 128, 0);
    if (header.name && strlen(header.name) > 0 && strcmp(header.name, "<?>") != 0) {
        vpool_insert(&vp, vpool_get_length(&vp), (char *)header.name, strlen(header.name));
        vpool_insert(&vp, vpool_get_length(&vp), "\n", 1);
    }
    if (header.artist && strlen(header.artist) > 0 && strcmp(header.artist, "<?>") != 0) {
        vpool_insert(&vp, vpool_get_length(&vp), (char *)header.artist, strlen(header.artist));
        vpool_insert(&vp, vpool_get_length(&vp), "\n", 1);
    }
    if (header.copyright && strlen(header.copyright) > 0 && strcmp(header.copyright, "<?>") != 0) {
        vpool_insert(&vp, vpool_get_length(&vp), (char *)header.copyright, strlen(header.copyright));
        vpool_insert(&vp, vpool_get_length(&vp), "\n", 1);
    }

    char *p = vpool_insert(&vp, vpool_get_length(&vp), "\0", 1);
    char *q = vpool_insert(&vp, vpool_get_length(&vp), "         \0", 10);

    char post[8];
    sprintf(post, "/%d", header.total_songs);

    menu_result_t menu_result = MENU_OK;
    uint8_t result = 0;
    uint8_t value_sel = 1;
    do {
        if (song == 0) {
            *p = '\0';
            result = display_input_value((char *)vpool_get_buf(&vp), "Song: ", &value_sel, 1, header.total_songs, 3, post);
            if (result == UINT8_MAX) {
                menu_result = MENU_TIMEOUT;
                break;
            }
        } else {
            value_sel = song;
        }

        if (result == 1 || song > 0) {
            if (nes_player_play_nsf_file(filename, value_sel, main_menu_demo_playback_cb, 0) == ESP_OK) {
                *p = '\n';
                sprintf(q, "<%d/%d>", value_sel, header.total_songs);
                display_static_list("NSF Player", (char *)vpool_get_buf(&vp));

                while (ulTaskNotifyTake(pdTRUE, 100 / portTICK_RATE_MS) == 0) {
                    keypad_event_t keypad_event;
                    if (keypad_wait_for_event(&keypad_event, 0) == ESP_OK) {
                        if (keypad_event.pressed && keypad_event.key == KEYPAD_BUTTON_B) {
                            nes_player_stop();
                        }
                    }
                }
            }
        }
    } while (result == 1);

    vpool_final(&vp);
    return menu_result;
}

static void reload_settings()
{
    if (settings_get_time_format(&time_twentyfour) != ESP_OK) {
        time_twentyfour = false;
    }

    if (settings_get_alarm_time(&alarm_hh, &alarm_mm) != ESP_OK) {
        alarm_hh = UINT8_MAX;
        alarm_mm = UINT8_MAX;
    }

    board_rtc_get_alarm_enabled(&alarm_set);
}

static void main_menu()
{
    menu_result_t menu_result = MENU_OK;
    uint8_t option = 1;

    do {
        option = display_selection_list(
                "Main Menu", option,
                "Demo Playback\n"
                "Demo Sound Effects\n"
                "Set Alarm\n"
                "Setup\n"
                "Diagnostics\n"
                "About");

        if (option == 1) {
            menu_result = menu_demo_playback();
        } else if (option == 2) {
            menu_result = menu_demo_sound_effects();
        } else if (option == 3) {
            menu_result = menu_set_alarm();
            reload_settings();
        } else if (option == 4) {
            menu_result = menu_setup();
            reload_settings();
        } else if (option == 5) {
            menu_result = menu_diagnostics();
        } else if (option == 6) {
            menu_result = menu_about();
        } else if (option == UINT8_MAX) {
            menu_result = MENU_TIMEOUT;
        }
    } while (option > 0 && menu_result != MENU_TIMEOUT);
}

static bool is_alarm_time(struct tm *timeinfo)
{
    if (!timeinfo) {
        return false;
    }

    if (timeinfo->tm_hour == alarm_hh && timeinfo->tm_min == alarm_mm) {
        return true;
    }

    int minutes_alarm = (alarm_hh) * 60 + alarm_mm;
    int minutes_curr = (timeinfo->tm_hour * 60) + timeinfo->tm_min;
    int minutes_prev = (timeinfo_prev.tm_hour * 60) + timeinfo_prev.tm_min;

    if (minutes_curr == minutes_alarm) {
        // Clock at exact alarm time
        return true;
    }
    else if (timeinfo_prev.tm_mday == 0) {
        // No previous time value for comparison, assume untriggered
        return false;
    }
    else if (timeinfo_prev.tm_year == timeinfo->tm_year && timeinfo_prev.tm_yday == timeinfo->tm_yday) {
        // Same day as previous time
        if (minutes_curr >= minutes_alarm && minutes_prev < minutes_alarm && (minutes_curr - minutes_prev) <= 10) {
            return true;
        }
    }
    else if ((timeinfo_prev.tm_year == timeinfo->tm_year && timeinfo_prev.tm_yday + 1 == timeinfo->tm_yday)
            || (timeinfo_prev.tm_year + 1 == timeinfo->tm_year
                    && timeinfo_prev.tm_mon == 11 && timeinfo_prev.tm_mday == 31
                    && timeinfo->tm_mday == 1)) {
        // Previous time is on the day before the current time
        minutes_prev -= 1440;
        if (minutes_curr >= minutes_alarm && minutes_prev < minutes_alarm && (minutes_curr - minutes_prev) <= 10) {
            return true;
        }
    }

    return false;
}

static esp_err_t board_rtc_alarm_func(bool alarm0, bool alarm1, time_t time)
{
    struct tm timeinfo;
    if (localtime_r(&time, &timeinfo)) {
        xSemaphoreTake(clock_mutex, portMAX_DELAY);
        if (!menu_visible) {
            int clock;
            if (alarm_set && xTimerIsTimerActive(alarm_snooze_timer) == pdTRUE) {
                clock = 2;
            } else if (alarm_set) {
                clock = 1;
            } else {
                clock = 0;
            }
            display_draw_time(timeinfo.tm_hour, timeinfo.tm_min, time_twentyfour, clock,
                    timeinfo.tm_mon + 1, timeinfo.tm_mday);
            if (alarm_triggered) {
                display_draw_clock(alarm_frame);
            }
        }
        if (alarm_set && !alarm_triggered
                && xTimerIsTimerActive(alarm_snooze_timer) == pdFALSE
                && is_alarm_time(&timeinfo)) {
            alarm_triggered = true;
            if (!menu_visible) {
                keypad_event_t keypad_event = {
                        .key = 0,
                        .pressed = true
                };
                keypad_inject_event(&keypad_event);
            }
        }
        memcpy(&timeinfo_prev, &timeinfo, sizeof(struct tm));
        xSemaphoreGive(clock_mutex);
    }

    return ESP_OK;
}

static void start_alarm_sequence()
{
    esp_err_t ret;

    ESP_LOGI(TAG, "Starting alarm sequence");

    display_set_contrast(0xFF);

    char *filename = 0;
    uint8_t song = 0;
    ret = settings_get_alarm_tune(&filename, NULL, NULL, &song);
    if (ret != ESP_OK || !filename || strlen(filename) == 0) {
        // No valid filename
        ret = ESP_FAIL;
    }

    if (ret == ESP_OK) {
        char *dot = strrchr(filename, '.');
        if (dot && (!strcmp(dot, ".vgm") || !strcmp(dot, ".vgz"))) {
            ret = nes_player_play_vgm_file(filename, NES_REPEAT_CONTINUOUS, NULL, NULL);
        } else if (dot && !strcmp(dot, ".nsf")) {
            if (song == 0) { song = 1; }
            ret = nes_player_play_nsf_file(filename, song, NULL, NULL);
        } else {
            // Bad filename extension
            ret = ESP_FAIL;
        }
    }
    free(filename);

    // If a VGM file could not be played, then do a fallback chime
    if (ret != ESP_OK) {
        nes_player_play_effect(NES_PLAYER_EFFECT_CHIME, NES_REPEAT_CONTINUOUS);
    }

    xTimerStart(alarm_complete_timer, portMAX_DELAY);
    alarm_frame = 0;
}

static void stop_alarm_sequence()
{
    ESP_LOGI(TAG, "Stopping alarm sequence");
    xTimerStop(alarm_complete_timer, portMAX_DELAY);
    nes_player_stop();
    display_set_contrast(contrast_value);
    alarm_frame = 0;
}

static void alarm_complete_timer_callback(TimerHandle_t xTimer)
{
    ESP_LOGI(TAG, "Alarm running too long, stopping");
    xSemaphoreTake(clock_mutex, portMAX_DELAY);
    alarm_triggered = false;
    keypad_event_t keypad_event = {
            .key = 0,
            .pressed = true
    };
    keypad_inject_event(&keypad_event);
    xSemaphoreGive(clock_mutex);
}

static void alarm_snooze_timer_callback(TimerHandle_t xTimer)
{
    ESP_LOGI(TAG, "Alarm snooze expired, restarting");
    xSemaphoreTake(clock_mutex, portMAX_DELAY);
    if (alarm_set && !alarm_triggered) {
        alarm_triggered = true;
        if (!menu_visible) {
            keypad_event_t keypad_event = {
                    .key = 0,
                    .pressed = true
            };
            keypad_inject_event(&keypad_event);
        }
    }
    xSemaphoreGive(clock_mutex);
}

void main_menu_brightness_update(uint8_t value)
{
    xSemaphoreTake(clock_mutex, portMAX_DELAY);
    contrast_value = value;
    if (!menu_visible && !alarm_triggered) {
        display_set_contrast(contrast_value);
    }
    xSemaphoreGive(clock_mutex);
}

static void main_menu_task(void *pvParameters)
{
    ESP_LOGD(TAG, "main_menu_task");

    // Prepare a timer for a maximum alarm duration of 5 minutes
    alarm_complete_timer = xTimerCreate("alarm_complete_timer", 300000 / portTICK_RATE_MS,
            pdFALSE, NULL, alarm_complete_timer_callback);

    // Prepare a timer for a maximum alarm snooze duration of 9 minutes
    alarm_snooze_timer = xTimerCreate("alarm_snooze_timer", 540000 / portTICK_RATE_MS,
            pdFALSE, NULL, alarm_snooze_timer_callback);

    while (1) {
        // Show the current time on the display
        xSemaphoreTake(clock_mutex, portMAX_DELAY);
        if (!alarm_triggered) {
            display_set_contrast(contrast_value);
        }
        menu_visible = false;
        display_clear();
        time_t time;
        if (board_rtc_get_time(&time) == ESP_OK) {
            struct tm timeinfo;
            if (localtime_r(&time, &timeinfo)) {
                int clock;
                if (alarm_set && xTimerIsTimerActive(alarm_snooze_timer) == pdTRUE) {
                    clock = 2;
                } else if (alarm_set) {
                    clock = 1;
                } else {
                    clock = 0;
                }
                display_draw_time(timeinfo.tm_hour, timeinfo.tm_min, time_twentyfour, clock,
                        timeinfo.tm_mon + 1, timeinfo.tm_mday);
                if (alarm_triggered) {
                    display_draw_clock(alarm_frame);
                }
            }
        }
        xSemaphoreGive(clock_mutex);

        // Block until a key press is detected
        while (1) {
            keypad_event_t keypad_event;
            TickType_t tick0 = 0;
            TickType_t tick1 = 0;
            bool break_loop = true;

            tick0 = xTaskGetTickCount();
            if (keypad_wait_for_event(&keypad_event, alarm_triggered ? 250 : -1) == ESP_OK) {
                tick1 = xTaskGetTickCount();
                /* Enforce a consistent delay during alarms so that key events don't cause inconsistent animation speed */
                if (alarm_triggered && (tick1 - tick0) < 250) {
                    vTaskDelay(250 - (tick1 - tick0));
                }

                if (keypad_event.pressed) {
                    if (keypad_event.key == KEYPAD_BUTTON_START) {
                        xSemaphoreTake(clock_mutex, portMAX_DELAY);
                        if (!alarm_set) {
                            alarm_set = true;
                            board_rtc_set_alarm_enabled(alarm_set);
                            alarm_triggered = false;
                            if (xTimerIsTimerActive(alarm_snooze_timer) == pdTRUE) {
                                xTimerStop(alarm_snooze_timer, portMAX_DELAY);
                            }
                        } else {
                            break_loop = false;
                        }
                        xSemaphoreGive(clock_mutex);
                    }
                    else if (keypad_event.key == KEYPAD_BUTTON_SELECT) {
                        xSemaphoreTake(clock_mutex, portMAX_DELAY);
                        if (alarm_set) {
                            alarm_set = false;
                            board_rtc_set_alarm_enabled(alarm_set);
                            alarm_triggered = false;
                            if (xTimerIsTimerActive(alarm_snooze_timer) == pdTRUE) {
                                xTimerStop(alarm_snooze_timer, portMAX_DELAY);
                            }
                        } else {
                            break_loop = false;
                        }
                        xSemaphoreGive(clock_mutex);
                    }
                    else if (keypad_event.key == KEYPAD_BUTTON_A || keypad_event.key == KEYPAD_BUTTON_B) {
                        xSemaphoreTake(clock_mutex, portMAX_DELAY);
                        if (!alarm_triggered) {
                            display_set_contrast(0x9F);
                            menu_visible = true;
                            if (xTimerIsTimerActive(alarm_snooze_timer) == pdTRUE) {
                                xTimerStop(alarm_snooze_timer, portMAX_DELAY);
                            }
                        } else {
                            break_loop = false;
                        }
                        xSemaphoreGive(clock_mutex);
                    }
                    else if (keypad_event.key == KEYPAD_TOUCH) {
                        xSemaphoreTake(clock_mutex, portMAX_DELAY);
                        if (alarm_triggered && xTimerIsTimerActive(alarm_snooze_timer) == pdFALSE) {
                            ESP_LOGI(TAG, "Snooze button pressed");
                            xTimerStart(alarm_snooze_timer, portMAX_DELAY);
                            alarm_triggered = false;
                        } else {
                            break_loop = false;
                        }
                        xSemaphoreGive(clock_mutex);
                    }
#if 0
                    /* Key actions useful for development and testing */
                    else if (keypad_event.key == KEYPAD_BUTTON_UP) {
                        /* Manually trigger the alarm */
                        xSemaphoreTake(clock_mutex, portMAX_DELAY);
                        if (!alarm_triggered && !alarm_running && xTimerIsTimerActive(alarm_snooze_timer) == pdFALSE) {
                            alarm_triggered = true;
                        }
                        xSemaphoreGive(clock_mutex);
                        break_loop = true;
                    }
                    else if (keypad_event.key == KEYPAD_BUTTON_DOWN) {
                        /* Dump a PBM format screenshot to the logger */
                        xSemaphoreTake(clock_mutex, portMAX_DELAY);
                        display_get_screenshot();
                        xSemaphoreGive(clock_mutex);
                        break_loop = true;
                    }
#endif
                    if (break_loop) {
                        break;
                    }
                }
            }

            xSemaphoreTake(clock_mutex, portMAX_DELAY);
            if (!menu_visible && alarm_triggered) {
                display_draw_clock(alarm_frame++);
                if (alarm_frame > 7) { alarm_frame = 1; }
            }
            xSemaphoreGive(clock_mutex);
        }

        if (menu_visible) {
            main_menu();
        }

        xSemaphoreTake(clock_mutex, portMAX_DELAY);
        if (alarm_triggered && !alarm_running) {
            start_alarm_sequence();
            alarm_running = true;
        } else if (!alarm_triggered && alarm_running) {
            stop_alarm_sequence();
            alarm_running = false;
        }
        xSemaphoreGive(clock_mutex);
    };

    vTaskDelete(NULL);
}

esp_err_t main_menu_start()
{
    ESP_LOGD(TAG, "main_menu_start");

    clock_mutex = xSemaphoreCreateMutex();
    if (!clock_mutex) {
        ESP_LOGE(TAG, "xSemaphoreCreateMutex error");
        return ESP_ERR_NO_MEM;
    }

    reload_settings();

    board_rtc_set_alarm_cb(board_rtc_alarm_func);

    xTaskCreate(main_menu_task, "main_menu_task", 4096, NULL, 5, &main_menu_task_handle);

    return ESP_OK;
}
