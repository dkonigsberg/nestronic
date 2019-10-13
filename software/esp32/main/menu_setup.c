#include "menu_setup.h"

#include <esp_log.h>
#include <esp_err.h>
#include <esp_wifi.h>
#include <tcpip_adapter.h>
#include <stdio.h>
#include <string.h>

#include "display.h"
#include "wifi_handler.h"
#include "board_config.h"
#include "settings.h"
#include "keypad.h"
#include "zoneinfo.h"
#include "board_rtc.h"
#include "time_handler.h"
#include "vpool.h"

static const char *TAG = "menu_setup";

static menu_result_t wifi_scan_connect(const wifi_ap_record_t *record)
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
        return MENU_TIMEOUT;
    } else if (option != 1) {
        return MENU_CANCEL;
    }

    if (record->authmode == WIFI_AUTH_WPA2_ENTERPRISE) {
        display_message(
                (const char *)record->ssid,
                NULL,
                "\nUnsupported authentication!\n", " OK ");
        return MENU_CANCEL;
    }

    if (record->authmode != WIFI_AUTH_OPEN) {
        char *text = NULL;
        char buf[64];
        sprintf(buf, "Password for %s", (const char *)record->ssid);

        uint8_t n = display_input_text(buf, &text);
        if (n == 0 || !text || n + 1 > sizeof(password)) {
            return MENU_CANCEL;
        }

        memcpy(password, text, n);
        free(text);
    }

    ESP_LOGI(TAG, "Connecting to: \"%s\"", record->ssid);


    if (wifi_handler_connect(record->ssid, password) != ESP_OK) {
        return MENU_CANCEL;
    }

    //TODO show connection status

    return MENU_OK;
}

static menu_result_t setup_wifi_scan()
{
    menu_result_t menu_result = MENU_OK;

    display_static_message("Wi-Fi Scan", NULL, "\nPlease wait...");

    char buf[32];
    wifi_ap_record_t *records = NULL;
    int record_count = 0;
    if (wifi_handler_scan(&records, &record_count) != ESP_OK) {
        return menu_result;
    }

    if (!records) {
        display_message(
                "Wi-Fi Scan",
                NULL,
                "\nNo networks found!\n", " OK ");
        return menu_result;
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
            menu_result = wifi_scan_connect(&records[option - 1]);
            if (menu_result == MENU_CANCEL) {
                menu_result = MENU_OK;
                break;
            }
        } else if (option == UINT8_MAX) {
            menu_result = MENU_TIMEOUT;
        }
    } while (option > 0 && menu_result != MENU_TIMEOUT);

    vpool_final(&vp);
    free(records);
    return menu_result;
}

static menu_result_t setup_network_info()
{
    menu_result_t menu_result = MENU_OK;
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

        esp_err_t ret = keypad_wait_for_event(&keypad_event, MENU_TIMEOUT_MS);
        if (ret == ESP_OK && keypad_event.pressed) {
            break;
        } else if (ret == ESP_ERR_TIMEOUT) {
            menu_result = MENU_TIMEOUT;
        }
    }

    vpool_final(&vp);
    return menu_result;
}

static menu_result_t setup_time_zone_region(const char *region)
{
    menu_result_t menu_result = MENU_CANCEL;

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
            menu_result = MENU_TIMEOUT;
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
                    menu_result = MENU_OK;
                }
            }

            break;
        }

    } while (option > 0 && menu_result != MENU_TIMEOUT);

    free(zone_list);

    return menu_result;
}

static menu_result_t setup_time_zone()
{
    menu_result_t menu_result = MENU_OK;

    char *region_list = zoneinfo_build_region_list();
    if (!region_list) {
        return menu_result;
    }

    uint8_t option = 1;
    do {
        option = display_selection_list(
                "Select Region", option,
                region_list);
        if (option == UINT8_MAX) {
            menu_result = MENU_TIMEOUT;
            break;
        }

        size_t length;
        const char *value = find_list_option(region_list, option, &length);
        if (value) {
            char region[32];
            strncpy(region, value, length);
            region[length] = '\0';

            menu_result_t zone_result = setup_time_zone_region(region);
            if (zone_result == MENU_OK) {
                break;
            } else if (zone_result == MENU_TIMEOUT) {
                menu_result = MENU_TIMEOUT;
            }
        }

    } while (option > 0 && menu_result != MENU_TIMEOUT);

    free(region_list);
    return menu_result;
}

static menu_result_t setup_time_format()
{
    menu_result_t menu_result = MENU_OK;

    uint8_t option = display_message(
            "Time Format", NULL, "\n",
            " 12-hour \n 24-hour ");
    if (option == UINT8_MAX) {
        menu_result = MENU_TIMEOUT;
    } else if (option == 1) {
        settings_set_time_format(false);
    } else if (option == 2) {
        settings_set_time_format(true);
    }
    return menu_result;
}

static menu_result_t setup_ntp_server()
{
    menu_result_t menu_result = MENU_OK;
    esp_err_t ret;
    char *hostname = NULL;

    ret = settings_get_ntp_server(&hostname);
    if (ret != ESP_OK || !hostname || strlen(hostname) == 0) {
        const char *sntp_servername = time_handler_sntp_getservername();
        if (!sntp_servername || strlen(sntp_servername) == 0) {
            return menu_result;
        }
        hostname = strdup(sntp_servername);
        if (!hostname) {
            return menu_result;
        }
    }

    //TODO Create a text input screen that restricts text to valid hostname characters
    uint8_t n = display_input_text("NTP Server", &hostname);
    if (n == 0 || !hostname) {
        if (hostname) {
            free(hostname);
        }
        return menu_result;
    }

    if (settings_set_ntp_server(hostname) == ESP_OK) {
        //TODO Figure out how to make this trigger a time refresh
        time_handler_sntp_setservername(hostname);
    }
    free(hostname);
    return menu_result;
}

static menu_result_t setup_rtc_calibration_measure()
{
    menu_result_t menu_result = MENU_OK;

    if (board_rtc_calibration() != ESP_OK) {
        board_rtc_init();
        return menu_result;
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
        menu_result = MENU_TIMEOUT;
    }

    board_rtc_init();
    return menu_result;
}

static menu_result_t setup_rtc_calibration_trim(bool *coarse, uint8_t *value)
{
    menu_result_t menu_result = MENU_OK;
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
                menu_result = MENU_TIMEOUT;
            }
        } else if (option == 4) {
            *coarse = coarse_sel;
            *value = (add_sel ? 0x80 : 0x00) | (value_sel & 0x7F);
            break;
        } else if (option == UINT8_MAX) {
            menu_result = MENU_TIMEOUT;
        }
    } while (option > 0 && menu_result != MENU_TIMEOUT);
    return menu_result;
}

static menu_result_t setup_rtc_calibration()
{
    menu_result_t menu_result = MENU_OK;
    char buf[128];
    char buf2[128];
    bool coarse;
    uint8_t value;

    if (settings_get_rtc_trim(&coarse, &value) != ESP_OK) {
        return menu_result;
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
            menu_result = setup_rtc_calibration_measure();
        } else if (option == 2) {
            menu_result = setup_rtc_calibration_trim(&coarse, &value);
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
            menu_result = MENU_TIMEOUT;
            break;
        } else if (option  == 0 || option == 4 || menu_result == MENU_TIMEOUT) {
            break;
        }
    } while(true);

    return menu_result;
}

menu_result_t menu_setup()
{
    menu_result_t menu_result = MENU_OK;
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
            menu_result = setup_wifi_scan();
        } else if (option == 2) {
            menu_result = setup_network_info();
        } else if (option == 3) {
            menu_result = setup_time_zone();
        } else if (option == 4) {
            menu_result = setup_time_format();
        } else if (option == 5) {
            menu_result = setup_ntp_server();
        } else if (option == 6) {
            menu_result = setup_rtc_calibration();
        } else if (option == UINT8_MAX) {
            menu_result = MENU_TIMEOUT;
        }

    } while (option > 0 && menu_result != MENU_TIMEOUT);
    return menu_result;
}
