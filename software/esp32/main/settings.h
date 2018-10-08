#ifndef SETTINGS_H
#define SETTINGS_H

#include <esp_err.h>
#include <stdint.h>
#include <stdbool.h>

esp_err_t settings_set_rtc_trim(bool coarse, uint8_t value);
esp_err_t settings_get_rtc_trim(bool *coarse, uint8_t *value);

esp_err_t settings_set_time_zone(const char *zone_name);
esp_err_t settings_get_time_zone(char **zone_name);

esp_err_t settings_set_time_format(bool twentyfour);
esp_err_t settings_get_time_format(bool *twentyfour);

esp_err_t settings_set_ntp_server(const char *hostname);
esp_err_t settings_get_ntp_server(char **hostname);

esp_err_t settings_set_alarm_time(uint8_t hh, uint8_t mm);
esp_err_t settings_get_alarm_time(uint8_t *hh, uint8_t *mm);

esp_err_t settings_set_alarm_tune(const char *filename, const char *title, const char *subtitle, uint8_t song);
esp_err_t settings_get_alarm_tune(char **filename, char **title, char **subtitle, uint8_t *song);

#endif /* SETTINGS_H */
