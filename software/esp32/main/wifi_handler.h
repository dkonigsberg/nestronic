#ifndef WIFI_HANDLER_H
#define WIFI_HANDLER_H

#include <esp_err.h>
#include <esp_types.h>
#include <esp_wifi_types.h>

esp_err_t wifi_handler_init();

esp_err_t wifi_handler_scan(wifi_ap_record_t **records, int *count);

esp_err_t wifi_handler_connect(const uint8_t ssid[32], const uint8_t password[64]);

#endif /* WIFI_HANDLER_H */
