/*
 * Implementation of the menu system for the UI
 */
#include "main_menu.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>
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
#include "wifi_handler.h"
#include "time_handler.h"
#include "vpool.h"
#include "display.h"
#include "board_rtc.h"
#include "keypad.h"
#include "sdcard_util.h"
#include "vgm_player.h"
#include "vgm.h"
#include "zoneinfo.h"
#include "bsdlib.h"
#include "tsl2591.h"
#include "i2c_util.h"

static const char *TAG = "main_menu";

static TaskHandle_t main_menu_task_handle;
static SemaphoreHandle_t clock_mutex = NULL;
static bool menu_visible = false;
static bool time_twentyfour = false;
static bool menu_timeout = false;
static uint8_t contrast_value = 0x9F;
static bool alarm_set = false;
static uint8_t alarm_hh;
static uint8_t alarm_mm;
static bool alarm_triggered = false;
static bool alarm_running = false;
static TimerHandle_t alarm_complete_timer = 0;
static TimerHandle_t alarm_snooze_timer = 0;

static esp_err_t main_menu_keypad_wait(keypad_event_t *event)
{
    esp_err_t ret = keypad_wait_for_event(event, MENU_TIMEOUT_MS);
    if (ret == ESP_ERR_TIMEOUT) {
        bzero(event, sizeof(keypad_event_t));
        event->key = KEYPAD_BUTTON_B;
        event->pressed = true;
        menu_timeout = true;
        ret = ESP_OK;
    }
    return ret;
}

static const char* find_list_option(const char *list, int option, size_t *length)
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

typedef bool (*file_picker_cb_t)(const char *filename);

static bool show_file_picker_impl(const char *title, const char *path, file_picker_cb_t cb)
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
        return false;
    }

    vpool_init(&vp, 1024, 0);

    int count = 0;
    for (int i = 0; i < n; i++) {
        if (count < UINT8_MAX - 2) {
            if (namelist[i]->d_type == DT_REG) {
                char *dot = strrchr(namelist[i]->d_name, '.');
                if (dot && (!strcmp(dot, ".vgm") || !strcmp(dot, ".vgz"))) {
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
        return true;
    }

    char *list = (char *) vpool_get_buf(&vp);
    size_t len = vpool_get_length(&vp);
    list[len - 1] = '\0';

    uint8_t option = 1;
    bool selected = false;
    do {
        option = display_selection_list(
                title, option,
                list);
        if (option == UINT8_MAX) {
            menu_timeout = true;
            break;
        }

        size_t file_len;
        const char *value = find_list_option(list, option, &file_len);

        if (value) {
            size_t pre_len = strlen(path);
            char *filename = malloc(pre_len + file_len + 2);
            if (!filename) {
                break;
            }
            strcpy(filename, path);
            filename[pre_len] = '/';
            strncpy(filename + pre_len + 1, value, file_len);
            filename[pre_len + file_len + 1] = '\0';

            if (filename[pre_len + file_len] == '/') {
                char *dir_title = strndup(value, file_len);
                if (!dir_title) {
                    break;
                }
                filename[pre_len + file_len] = '\0';
                if (show_file_picker_impl(dir_title, filename, cb)) {
                    selected = true;
                }
            } else if (cb) {
                selected = cb(filename);
            } else {
                selected = true;
            }
            free(filename);
            if (selected) {
                break;
            }
        }
    } while (option > 0 && !menu_timeout);

    vpool_final(&vp);

    return selected;
}

static void show_file_picker(const char *title, file_picker_cb_t cb)
{
    show_file_picker_impl(title, "/sdcard", cb);
}

static void main_menu_demo_playback_cb(vgm_playback_state_t state)
{
    if (state == VGM_PLAYER_FINISHED) {
        xTaskNotifyGive(main_menu_task_handle);
    }
}

static bool main_menu_file_picker_cb(const char *filename)
{
    ESP_LOGI(TAG, "File: \"%s\"", filename);

    // Make this a little less synchronous at some point,
    // and implement some sort of playback UI.

    // The player currently has code to parse the GD3 tags and
    // show them on the display.
    vgm_gd3_tags_t *tags = NULL;
    if (vgm_player_play_file(filename, VGM_REPEAT_NONE, main_menu_demo_playback_cb, &tags) == ESP_OK) {
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
        vgm_free_gd3_tags(tags);
        vpool_final(&vp);

        while (ulTaskNotifyTake(pdTRUE, 100 / portTICK_RATE_MS) == 0) {
            keypad_event_t keypad_event;
            if (keypad_wait_for_event(&keypad_event, 0) == ESP_OK) {
                if (keypad_event.pressed && keypad_event.key == KEYPAD_BUTTON_B) {
                    vgm_player_stop();
                }
            }
        }
    }

    // Remain in the picker
    return false;
}

static void main_menu_demo_playback()
{
    show_file_picker("Demo Playback", main_menu_file_picker_cb);
}

static void main_menu_demo_sound_effects()
{
    uint8_t option = 1;

    do {
        option = display_selection_list(
                "Demo Sound Effects", option,
                "Chime\n"
                "Blip\n"
                "Credit");

        if (option == 1) {
            vgm_player_play_effect(VGM_PLAYER_EFFECT_CHIME, VGM_REPEAT_NONE);
        } else if (option == 2) {
            vgm_player_play_effect(VGM_PLAYER_EFFECT_BLIP, VGM_REPEAT_NONE);
        } else if (option == 3) {
            vgm_player_play_effect(VGM_PLAYER_EFFECT_CREDIT, VGM_REPEAT_NONE);
        } else if (option == UINT8_MAX) {
            menu_timeout = true;
        }
    } while (option > 0 && !menu_timeout);
}

static void diagnostics_display()
{
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
        if (main_menu_keypad_wait(&keypad_event) == ESP_OK) {
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
        }
    }

    display_set_contrast(initial_contrast);
}

static void diagnostics_touch()
{
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
                menu_timeout = true;
                break;
            }
        }
    }
}

static void diagnostics_ambient_light()
{
    //TODO disable non-diagnostic ambient light polling
    //TODO add support for adjusting sensor parameters

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
                menu_timeout = true;
                break;
            }
        }
    }
}

static void diagnostics_volume()
{
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
                menu_timeout = true;
                break;
            }
        }
    }
}

static void main_menu_diagnostics()
{
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
            diagnostics_display();
        } else if (option == 2) {
            diagnostics_touch();
        } else if (option == 3) {
            diagnostics_ambient_light();
        } else if (option == 4) {
            diagnostics_volume();
        } else if (option == 5) {
            vgm_player_benchmark_data();
        } else if (option == UINT8_MAX) {
            menu_timeout = true;
        }
    } while (option > 0 && !menu_timeout);
}

static void main_menu_set_alarm_time()
{
    uint8_t hh;
    uint8_t mm;
    if (settings_get_alarm_time(&hh, &mm) != ESP_OK) {
        return;
    }

    if (display_set_time(&hh, &mm, time_twentyfour)) {
        settings_set_alarm_time(hh, mm);
        ESP_LOGI(TAG, "Alarm time set: %02d:%02d", hh, mm);
        alarm_hh = hh;
        alarm_mm = mm;
    }
}

static bool alarm_tune_file_picker_cb(const char *filename)
{
    esp_err_t ret;
    vgm_file_t *vgm_file;
    vgm_gd3_tags_t *tags_result = 0;

    ESP_LOGI(TAG, "File: \"%s\"", filename);

    ret = vgm_open(&vgm_file, filename);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open VGM file");
        display_message("Error", "File could not be opened", NULL, " OK ");
        return false;
    }

    if (vgm_get_header(vgm_file)->nes_apu_fds) {
        ESP_LOGE(TAG, "FDS Add-on is not supported");
        vgm_free(vgm_file);
        display_message("Error", "File is not supported", NULL, " OK ");
        return false;
    }

    if (vgm_read_gd3_tags(&tags_result, vgm_file) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read GD3 tags");
        vgm_free(vgm_file);
        display_message("Error", "File could not be read", NULL, " OK ");
        return false;
    }

    vgm_free(vgm_file);

    bool selected = false;
    do {
        uint8_t option = display_message(
                tags_result->game_name ? tags_result->game_name : "Unknown",
                tags_result->track_name ? tags_result->track_name : "Unknown",
                "",
                " Select \n Play ");
        if (option == 1) {
            if (settings_set_alarm_tune(filename, tags_result->game_name, tags_result->track_name) == ESP_OK) {
                selected = true;
            }
            break;
        } else if (option == 2) {
            main_menu_file_picker_cb(filename);
        } else if (option == UINT8_MAX) {
            menu_timeout = true;
            break;
        } else if (option  == 0) {
            break;
        }
    } while (true);

    vgm_free_gd3_tags(tags_result);

    return selected;
}

static void main_menu_set_alarm()
{
    char buf_time[128];
    char buf_tune[128];

    do {
        uint8_t hh;
        uint8_t mm;
        if (settings_get_alarm_time(&hh, &mm) != ESP_OK) {
            return;
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
        esp_err_t ret = settings_get_alarm_tune(&filename, &title, &subtitle);
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
            main_menu_set_alarm_time();
        } else if (option == 2) {
            show_file_picker("Select Alarm Tune", alarm_tune_file_picker_cb);
        } else if (option == UINT8_MAX) {
            menu_timeout = true;
            break;
        } else if (option  == 0) {
            break;
        }
    } while (true);
}

static bool wifi_scan_connect(const wifi_ap_record_t *record)
{
    char bssid[17];
    const char *authmode;
    uint8_t password[64];

    bzero(password, sizeof(password));

    sprintf(bssid, "%02X:%02X:%02X:%02X:%02X:%02X",
            record->bssid[0], record->bssid[1], record->bssid[2],
            record->bssid[3], record->bssid[4], record->bssid[5]);

    switch (record->authmode) {
    case WIFI_AUTH_OPEN:
        authmode = "Open\n";
        break;
    case WIFI_AUTH_WEP:
        authmode = "WEP\n";
        break;
    case WIFI_AUTH_WPA_PSK:
        authmode = "WPA-PSK\n";
        break;
    case WIFI_AUTH_WPA2_PSK:
        authmode = "WPA2-PSK\n";
        break;
    case WIFI_AUTH_WPA_WPA2_PSK:
        authmode = "WPA-WPA2-PSK\n";
        break;
    case WIFI_AUTH_WPA2_ENTERPRISE:
        authmode = "WPA2-Enterprise\n";
        break;
    default:
        authmode = "Unknown\n";
        break;
    }

    uint8_t option = display_message(
            (const char *)record->ssid, bssid, authmode,
            " Connect \n Cancel ");
    if (option == UINT8_MAX) {
        menu_timeout = true;
        return false;
    } else if (option != 1) {
        return false;
    }

    if (record->authmode == WIFI_AUTH_WPA2_ENTERPRISE) {
        display_message(
                (const char *)record->ssid,
                NULL,
                "\nUnsupported authentication!\n", " OK ");
        return false;
    }

    if (record->authmode != WIFI_AUTH_OPEN) {
        char *text = NULL;
        char buf[64];
        sprintf(buf, "Password for %s", (const char *)record->ssid);

        uint8_t n = display_input_text(buf, &text);
        if (n == 0 || !text || n + 1 > sizeof(password)) {
            return false;
        }

        memcpy(password, text, n);
        free(text);
    }

    ESP_LOGI(TAG, "Connecting to: \"%s\"", record->ssid);


    if (wifi_handler_connect(record->ssid, password) != ESP_OK) {
        return false;
    }

    //TODO show connection status

    return true;
}

static void setup_wifi_scan()
{
    display_static_message("Wi-Fi Scan", NULL, "\nPlease wait...");

    char buf[32];
    wifi_ap_record_t *records = NULL;
    int record_count = 0;
    if (wifi_handler_scan(&records, &record_count) != ESP_OK) {
        return;
    }

    if (!records) {
        display_message(
                "Wi-Fi Scan",
                NULL,
                "\nNo networks found!\n", " OK ");
        return;
    }

    // Clamp the maximum list size to deal with UI control limitations
    if (record_count > UINT8_MAX - 2) {
        record_count = UINT8_MAX - 2;
    }

    struct vpool vp;
    vpool_init(&vp, 32 * record_count, 0);

    for (int i = 0; i < record_count; i++) {
        if (strlen((char *)records[i].ssid) == 0) { continue; }
        sprintf(buf, "%22.22s | [% 4d]", records[i].ssid, records[i].rssi);
        vpool_insert(&vp, vpool_get_length(&vp), buf, strlen(buf));
        vpool_insert(&vp, vpool_get_length(&vp), "\n", 1);
    }

    char *list = (char *) vpool_get_buf(&vp);
    size_t len = vpool_get_length(&vp);
    list[len - 1] = '\0';

    uint8_t option = 1;
    do {
        option = display_selection_list(
                "Select Network", option,
                list);
        if (option > 0 && option - 1 < record_count) {
            if (wifi_scan_connect(&records[option - 1])) {
                break;
            }
        } else if (option == UINT8_MAX) {
            menu_timeout = true;
        }
    } while (option > 0 && !menu_timeout);

    vpool_final(&vp);
    free(records);
}

static void setup_network_info()
{
    esp_err_t ret;
    char buf[128];
    uint8_t mac[6];
    tcpip_adapter_ip_info_t ip_info;
    struct vpool vp;
    vpool_init(&vp, 1024, 0);

    ret = esp_wifi_get_mac(ESP_IF_WIFI_STA, mac);
    if (ret == ESP_OK) {
        sprintf(buf, "MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
                mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        vpool_insert(&vp, vpool_get_length(&vp), buf, strlen(buf));
    } else {
        ESP_LOGE(TAG, "esp_wifi_get_mac error: %X", ret);
    }

    ret = tcpip_adapter_get_ip_info(TCPIP_ADAPTER_IF_STA, &ip_info);
    if (ret == ESP_OK) {
        sprintf(buf,
                "IP: " IPSTR "\n"
                "Netmask: " IPSTR "\n"
                "Gateway: " IPSTR "\n",
                IP2STR(&ip_info.ip), IP2STR(&ip_info.netmask), IP2STR(&ip_info.gw));
        vpool_insert(&vp, vpool_get_length(&vp), buf, strlen(buf));
    } else {
        ESP_LOGE(TAG, "tcpip_adapter_get_ip_info error: %d", ret);
    }

    char *list = (char *) vpool_get_buf(&vp);
    size_t len = vpool_get_length(&vp);
    list[len - 1] = '\0';

    display_static_list("Network Info", list);

    while (1) {
        keypad_event_t keypad_event;
        if (main_menu_keypad_wait(&keypad_event) == ESP_OK) {
            if (keypad_event.pressed) {
                break;
            }
        }
    }

    vpool_final(&vp);
}

static bool setup_time_zone_region(const char *region)
{
    bool result = false;
    char *zone_list = zoneinfo_build_region_zone_list(region);
    if (!zone_list) {
        return false;
    }

    uint8_t option = 1;
    do {
        option = display_selection_list(
                "Select Zone", option,
                zone_list);
        if (option == UINT8_MAX) {
            menu_timeout = true;
            break;
        }

        size_t length;
        const char *value = find_list_option(zone_list, option, &length);
        if (value) {
            char zone[128];
            strcpy(zone, region);
            strcat(zone, "/");
            strncat(zone, value, length);

            const char *tz = zoneinfo_get_tz(zone);
            if (tz) {
                ESP_LOGI(TAG, "Selected time zone: \"%s\" -> \"%s\"", zone, tz);
                if (settings_set_time_zone(zone) == ESP_OK) {
                    setenv("TZ", tz, 1);
                    tzset();
                    result = true;
                }
            }

            break;
        }

    } while (option > 0 && !menu_timeout);

    free(zone_list);

    return result;
}

static void setup_time_zone()
{
    char *region_list = zoneinfo_build_region_list();
    if (!region_list) {
        return;
    }

    uint8_t option = 1;
    do {
        option = display_selection_list(
                "Select Region", option,
                region_list);
        if (option == UINT8_MAX) {
            menu_timeout = true;
            break;
        }

        size_t length;
        const char *value = find_list_option(region_list, option, &length);
        if (value) {
            char region[32];
            strncpy(region, value, length);
            region[length] = '\0';
            if (setup_time_zone_region(region)) {
                break;
            }
        }

    } while (option > 0 && !menu_timeout);

    free(region_list);
}

static void setup_time_format()
{
    uint8_t option = display_message(
            "Time Format", NULL, "\n",
            " 12-hour \n 24-hour ");
    if (option == UINT8_MAX) {
        menu_timeout = true;
    } else if (option == 1) {
        if (settings_set_time_format(false) == ESP_OK) {
            time_twentyfour = false;
        }
    } else if (option == 2) {
        if (settings_set_time_format(true) == ESP_OK) {
            time_twentyfour = true;
        }
    }
}

static void setup_ntp_server()
{
    esp_err_t ret;
    char *hostname = NULL;
    ret = settings_get_ntp_server(&hostname);
    if (ret != ESP_OK || !hostname || strlen(hostname) == 0) {
        const char *sntp_servername = time_handler_sntp_getservername();
        if (!sntp_servername || strlen(sntp_servername) == 0) {
            return;
        }
        hostname = strdup(sntp_servername);
        if (!hostname) {
            return;
        }
    }

    //TODO Create a text input screen that restricts text to valid hostname characters
    uint8_t n = display_input_text("NTP Server", &hostname);
    if (n == 0 || !hostname) {
        if (hostname) {
            free(hostname);
        }
        return;
    }

    if (settings_set_ntp_server(hostname) == ESP_OK) {
        //TODO Figure out how to make this trigger a time refresh
        time_handler_sntp_setservername(hostname);
    }
    free(hostname);
}

static void setup_rtc_calibration_measure()
{
    if (board_rtc_calibration() != ESP_OK) {
        board_rtc_init();
        return;
    }

    uint8_t option = 0;
    uint8_t count = 0;
    do {
        option = display_message(
                "RTC Measure",
                NULL,
                "\nMeasure frequency at test point\n", " Done ");
        if (option == UINT8_MAX) {
            count++;
        }
    } while(option > 1 && count < 5);

    if (option == UINT8_MAX) {
        menu_timeout = true;
    }

    board_rtc_init();
}

static void setup_rtc_calibration_trim(bool *coarse, uint8_t *value)
{
    char buf[128];
    bool coarse_sel = *coarse;
    bool add_sel = (*value & 0x80) == 0x80;
    uint8_t value_sel = *value & 0x7F;

    uint8_t option = 1;
    do {
        sprintf(buf, "%s\n%s\nValue=%d\nAccept",
                (coarse_sel ? "Coarse" : "Fine"),
                (add_sel ? "Add" : "Subtract"),
                value_sel);

        option = display_selection_list("RTC Trim", option, buf);

        if (option == 1) {
            coarse_sel = !coarse_sel;
        } else if (option == 2) {
            add_sel = !add_sel;
        } else if (option == 3) {
            if (display_input_value("Trim Value\n", "", &value_sel, 0, 127, 3, "") == UINT8_MAX) {
                menu_timeout = true;
            }
        } else if (option == 4) {
            *coarse = coarse_sel;
            *value = (add_sel ? 0x80 : 0x00) | (value_sel & 0x7F);
            break;
        } else if (option == UINT8_MAX) {
            menu_timeout = true;
        }
    } while (option > 0 && !menu_timeout);
}

static void setup_rtc_calibration()
{
    char buf[128];
    char buf2[128];
    bool coarse;
    uint8_t value;
    if (settings_get_rtc_trim(&coarse, &value) != ESP_OK) {
        return;
    }

    do {
        sprintf(buf, "[%s] %c%d",
                (coarse ? "Coarse" : "Fine"),
                (((value & 0x80) == 0x80) ? '+' : '-'),
                value & 0x7F);

        if ((value & 0x7F) == 0) {
            sprintf(buf2, "Digital trimming disabled\n");
        }
        else if (coarse) {
            sprintf(buf2, "%s %d clock cycles\n128 times per second\n",
                    (((value & 0x80) == 0x80) ? "Add" : "Subtract"),
                    (value & 0x7F) * 2);
        } else {
            sprintf(buf2, "%s %d clock cycles\nevery minute\n",
                    (((value & 0x80) == 0x80) ? "Add" : "Subtract"),
                    (value & 0x7F) * 2);
        }

        uint8_t option = display_message(
                "RTC Calibration\n", buf, buf2,
                " Measure \n Trim \n OK \n Cancel ");
        if (option == 1) {
            setup_rtc_calibration_measure();
        } else if (option == 2) {
            setup_rtc_calibration_trim(&coarse, &value);
        } else if (option == 3) {
            if ((value & 0x7F) == 0) {
                // Use a common default for trimming disabled
                settings_set_rtc_trim(false, 0);
            } else {
                settings_set_rtc_trim(coarse, value);
            }
            // Reinitialize RTC to use new value
            board_rtc_init();
            break;
        } else if (option == UINT8_MAX) {
            menu_timeout = true;
            break;
        } else if (option  == 0 || option == 4) {
            break;
        }
    } while(true);
}

static void main_menu_setup()
{
    uint8_t option = 1;

    do {
        option = display_selection_list(
                "Setup", option,
                "Wi-Fi Setup\n"
                "Network Info\n"
                "Time Zone\n"
                "Time Format\n"
                "NTP Server\n"
                "RTC Calibration");

        if (option == 1) {
            setup_wifi_scan();
        } else if (option == 2) {
            setup_network_info();
        } else if (option == 3) {
            setup_time_zone();
        } else if (option == 4) {
            setup_time_format();
        } else if (option == 5) {
            setup_ntp_server();
        } else if (option == 6) {
            setup_rtc_calibration();
        } else if (option == UINT8_MAX) {
            menu_timeout = true;
        }

    } while (option > 0 && !menu_timeout);
}

static void main_menu_about()
{
    uint8_t option = display_message(
            "Nestronic",
            NULL,
            "\nVideo Game Music Player\nAlarm Clock\n", " OK ");
    if (option == UINT8_MAX) {
        menu_timeout = true;
    }
}

static void main_menu()
{
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
            main_menu_demo_playback();
        } else if (option == 2) {
            main_menu_demo_sound_effects();
        } else if (option == 3) {
            main_menu_set_alarm();
        } else if (option == 4) {
            main_menu_setup();
        } else if (option == 5) {
            main_menu_diagnostics();
        } else if (option == 6) {
            main_menu_about();
        } else if (option == UINT8_MAX) {
            menu_timeout = true;
        }
    } while (option > 0 && !menu_timeout);
}

static esp_err_t board_rtc_alarm_func(bool alarm0, bool alarm1, time_t time)
{
    struct tm timeinfo;
    if (localtime_r(&time, &timeinfo)) {
        xSemaphoreTake(clock_mutex, portMAX_DELAY);
        if (!menu_visible) {
            display_draw_time(timeinfo.tm_hour, timeinfo.tm_min, time_twentyfour, alarm_set);
        }
        if (alarm_set
                && xTimerIsTimerActive(alarm_snooze_timer) == pdFALSE
                && timeinfo.tm_hour == alarm_hh && timeinfo.tm_min == alarm_mm) {
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

    return ESP_OK;
}

static void start_alarm_sequence()
{
    esp_err_t ret;

    ESP_LOGI(TAG, "Starting alarm sequence");

    display_set_contrast(0xFF);

    char *filename = 0;
    ret = settings_get_alarm_tune(&filename, NULL, NULL);
    if (ret != ESP_OK || !filename || strlen(filename) == 0) {
        // No valid filename
        ret = ESP_FAIL;
    }

    if (ret == ESP_OK) {
        ret = vgm_player_play_file(filename, VGM_REPEAT_CONTINUOUS, NULL, NULL);
    }
    free(filename);

    // If a VGM file could not be played, then do a fallback chime
    if (ret != ESP_OK) {
        vgm_player_play_effect(VGM_PLAYER_EFFECT_CHIME, VGM_REPEAT_CONTINUOUS);
    }

    xTimerStart(alarm_complete_timer, portMAX_DELAY);
}

static void stop_alarm_sequence()
{
    ESP_LOGI(TAG, "Stopping alarm sequence");
    xTimerStop(alarm_complete_timer, portMAX_DELAY);
    vgm_player_stop();
    display_set_contrast(contrast_value);
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
                display_draw_time(timeinfo.tm_hour, timeinfo.tm_min, time_twentyfour, alarm_set);
            }
        }
        xSemaphoreGive(clock_mutex);

        // Block until a key press is detected
        while (1) {
            keypad_event_t keypad_event;
            if (keypad_wait_for_event(&keypad_event, -1) == ESP_OK) {
                if (keypad_event.pressed) {
                    if (keypad_event.key == KEYPAD_BUTTON_START) {
                        xSemaphoreTake(clock_mutex, portMAX_DELAY);
                        alarm_set = true;
                        board_rtc_set_alarm_enabled(alarm_set);
                        alarm_triggered = false;
                        if (xTimerIsTimerActive(alarm_snooze_timer) == pdTRUE) {
                            xTimerStop(alarm_snooze_timer, portMAX_DELAY);
                        }
                        xSemaphoreGive(clock_mutex);
                    }
                    else if (keypad_event.key == KEYPAD_BUTTON_SELECT) {
                        xSemaphoreTake(clock_mutex, portMAX_DELAY);
                        alarm_set = false;
                        board_rtc_set_alarm_enabled(alarm_set);
                        alarm_triggered = false;
                        if (xTimerIsTimerActive(alarm_snooze_timer) == pdTRUE) {
                            xTimerStop(alarm_snooze_timer, portMAX_DELAY);
                        }
                        xSemaphoreGive(clock_mutex);
                    }
                    else if (keypad_event.key == KEYPAD_BUTTON_A || keypad_event.key == KEYPAD_BUTTON_B) {
                        xSemaphoreTake(clock_mutex, portMAX_DELAY);
                        display_set_contrast(0x9F);
                        menu_visible = true;
                        alarm_triggered = false;
                        if (xTimerIsTimerActive(alarm_snooze_timer) == pdTRUE) {
                            xTimerStop(alarm_snooze_timer, portMAX_DELAY);
                        }
                        xSemaphoreGive(clock_mutex);
                    }
                    else if (keypad_event.key == KEYPAD_TOUCH) {
                        xSemaphoreTake(clock_mutex, portMAX_DELAY);
                        if (alarm_triggered && xTimerIsTimerActive(alarm_snooze_timer) == pdFALSE) {
                            ESP_LOGI(TAG, "Snooze button pressed");
                            xTimerStart(alarm_snooze_timer, portMAX_DELAY);
                            alarm_triggered = false;
                        }
                        xSemaphoreGive(clock_mutex);
                    }
                    break;
                }
            }
        }

        if (menu_visible) {
            menu_timeout = false;
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

    if (settings_get_time_format(&time_twentyfour) != ESP_OK) {
        time_twentyfour = false;
    }

    if (settings_get_alarm_time(&alarm_hh, &alarm_mm) != ESP_OK) {
        alarm_hh = UINT8_MAX;
        alarm_mm = UINT8_MAX;
    }

    board_rtc_get_alarm_enabled(&alarm_set);

    board_rtc_set_alarm_cb(board_rtc_alarm_func);

    xTaskCreate(main_menu_task, "main_menu_task", 4096, NULL, 5, &main_menu_task_handle);

    return ESP_OK;
}
