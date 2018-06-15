/*
 * Handles interactions with the platform Wi-Fi APIs.
 */

#include "wifi_handler.h"

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/event_groups.h>
#include <esp_log.h>
#include <esp_wifi.h>
#include <esp_system.h>
#include <esp_event.h>
#include <esp_event_loop.h>
#include <stddef.h>
#include <string.h>
#include <sys/time.h>

#include "time_handler.h"

static const char *TAG = "wifi_handler";

static bool scan_lock = false;
static bool allow_reconnect = false;
static wifi_ap_record_t *scan_records = NULL;
static int scan_record_count = 0;

static void wifi_handler_event_scan_done(const system_event_sta_scan_done_t *event_info);

static esp_err_t wifi_event_handler(void *ctx, system_event_t *event)
{
    switch(event->event_id) {
    case SYSTEM_EVENT_WIFI_READY:
        ESP_LOGI(TAG, "Event: Wi-Fi Ready");
        break;
    case SYSTEM_EVENT_STA_START:
        ESP_LOGI(TAG, "Event: STA Start");
        esp_wifi_connect();
        break;
    case SYSTEM_EVENT_STA_STOP:
        ESP_LOGI(TAG, "Event: STA Stop");
        break;
    case SYSTEM_EVENT_STA_GOT_IP:
        ESP_LOGI(TAG, "Event: STA Got IP");
        break;
    case SYSTEM_EVENT_STA_LOST_IP:
        ESP_LOGI(TAG, "Event: STA Lost IP");
        break;
    case SYSTEM_EVENT_STA_CONNECTED:
        ESP_LOGI(TAG, "Event: STA Connected");
        allow_reconnect = true;
        time_handler_sntp_init();
        break;
    case SYSTEM_EVENT_STA_DISCONNECTED:
        ESP_LOGI(TAG, "Event: STA Disconnected");
        if (allow_reconnect) {
            /* This is a workaround as ESP32 WiFi libs don't currently
               auto-reassociate. */
            esp_wifi_connect();
        }
        break;
    case SYSTEM_EVENT_STA_AUTHMODE_CHANGE:
        ESP_LOGI(TAG, "Event: STA Auth Mode Change");
        break;
    case SYSTEM_EVENT_SCAN_DONE:
        ESP_LOGI(TAG, "Event: Scan Done");
        wifi_handler_event_scan_done(&event->event_info.scan_done);
        break;
    case SYSTEM_EVENT_GOT_IP6:
        ESP_LOGI(TAG, "Event: Got IPv6");
        break;
    default:
        ESP_LOGI(TAG, "Event: %d", event->event_id);
        break;
    }
    return ESP_OK;
}

esp_err_t wifi_handler_init()
{
    ESP_ERROR_CHECK(esp_event_loop_init(wifi_event_handler, NULL));

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_FLASH));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi handler initialized");

    return ESP_OK;
}

esp_err_t wifi_handler_scan(wifi_ap_record_t **records, int *count)
{
    if (scan_lock) {
        return ESP_ERR_INVALID_STATE;
    }

    scan_lock = true;
    scan_records = NULL;
    scan_record_count = 0;

    wifi_scan_config_t scan_config = {
       .ssid = NULL,
       .bssid = NULL,
       .channel = 0,
       .show_hidden = true,
       .scan_type = WIFI_SCAN_TYPE_ACTIVE,
       .scan_time.active = { .min = 100, .max = 1500 }
    };

    ESP_LOGI(TAG, "Starting Wi-Fi scan");

    scan_record_count = -1;

    esp_err_t ret = esp_wifi_scan_start(&scan_config, false);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_scan_start error: %d", ret);
        return ret;
    }

    // Wait for scan to complete
    do {
        vTaskDelay(100 / portTICK_PERIOD_MS);
    } while (scan_record_count == -1);

    if (records && count) {
        *records = scan_records;
        *count = scan_record_count;
    } else if (scan_records) {
        free(scan_records);
    }

    scan_record_count = 0;
    scan_records = NULL;
    scan_lock = false;

    return ret;
}

static void wifi_handler_event_scan_done(const system_event_sta_scan_done_t *event_info)
{
    ESP_LOGI(TAG, "Wi-Fi scan complete");

    esp_err_t ret;
    uint16_t ap_num = 0;

    ret = esp_wifi_scan_get_ap_num(&ap_num);
    if (ret != ESP_OK) {
        scan_record_count = 0;
        return;
    }

    ESP_LOGI(TAG, "Found %d access points", event_info->number);
    if (ap_num == 0) {
        scan_record_count = 0;
        return;
    }

    wifi_ap_record_t *list = (wifi_ap_record_t *)malloc(sizeof(wifi_ap_record_t) * ap_num);
    if (!list) {
        scan_record_count = 0;
        return;
    }

    ret = esp_wifi_scan_get_ap_records(&ap_num, list);
    if (ret != ESP_OK) {
        free(list);
        scan_record_count = 0;
        return;
    }

    for (int i = 0; i < ap_num; i++) {
        ESP_LOGI(TAG, "%26.26s  |  % 4d",list[i].ssid, list[i].rssi);
    }

    scan_records = list;
    scan_record_count = ap_num;
}

esp_err_t wifi_handler_connect(const uint8_t ssid[32], const uint8_t password[64])
{
    esp_err_t ret;
    wifi_config_t config;
    bzero(&config, sizeof(wifi_config_t));
    memcpy(&config.sta.ssid, ssid, 32);
    memcpy(&config.sta.password, password, 64);

    allow_reconnect = false;
    ret = esp_wifi_disconnect();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_disconnect error: %d", ret);
        return ret;
    }

    vTaskDelay(100 / portTICK_PERIOD_MS);

    ret = esp_wifi_set_config(WIFI_IF_STA, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_set_config error: %d", ret);
        return ret;
    }

    ret = esp_wifi_connect();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_connect error: %d", ret);
        return ret;
    }

    return ret;
}
