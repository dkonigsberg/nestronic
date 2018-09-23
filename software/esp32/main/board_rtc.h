#ifndef BOARD_RTC_H
#define BOARD_RTC_H

#include <esp_err.h>
#include <time.h>
#include <stdbool.h>

typedef esp_err_t (*board_rtc_alarm_cb_t)(bool alarm0, bool alarm1, time_t time);

esp_err_t board_rtc_init();
esp_err_t board_rtc_calibration();

esp_err_t board_rtc_set_alarm_cb(board_rtc_alarm_cb_t cb);

bool board_rtc_has_power_failed();
esp_err_t board_rtc_get_power_time_down(time_t *time);
esp_err_t board_rtc_get_power_time_up(time_t *time);

esp_err_t board_rtc_get_time(time_t *time);
esp_err_t board_rtc_set_time(const time_t *time);

esp_err_t board_rtc_int_event_handler();

#endif /* BOARD_RTC_H */
