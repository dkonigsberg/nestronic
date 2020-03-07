#include "time_handler.h"

#include <esp_err.h>
#include <esp_log.h>
#include <esp_sntp.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <errno.h>

#include "settings.h"
#include "zoneinfo.h"
#include "board_rtc.h"

static const char *TAG = "time_handler";

static bool sntp_initialized = false;
static bool sntp_hostname_default = false;

static void time_handler_init_zone()
{
    char *zone_name = NULL;
    esp_err_t ret = settings_get_time_zone(&zone_name);
    if (ret != ESP_OK || !zone_name || strlen(zone_name) == 0) {
        ESP_LOGE(TAG, "Unable to get time zone, defaulting to GMT");
        setenv("TZ", "GMT", 1);
        tzset();
        if (zone_name) {
            free(zone_name);
        }
        return;
    }

    const char *tz = zoneinfo_get_tz(zone_name);
    if (!tz || strlen(tz) == 0) {
        ESP_LOGE(TAG, "Could not find \"%s\" in time zone data, defaulting to GMT", zone_name);
        setenv("TZ", "GMT", 1);
        tzset();
        free(zone_name);
        return;
    }

    ESP_LOGI(TAG, "Setting TZ \"%s\" => \"%s\"", zone_name, tz);

    setenv("TZ", tz, 1);
    tzset();
    free(zone_name);
}

static esp_err_t time_handler_init_rtc()
{
    esp_err_t ret;

    // Get the time from the board RTC
    time_t time;
    ret = board_rtc_get_time(&time);
    if (ret != ESP_OK) {
        return ret;
    }

    // Set the time of the internal RTC
    struct timeval tv = {
            .tv_sec = time,
            .tv_usec = 0
    };
    if (settimeofday(&tv, NULL) < 0) {
        ESP_LOGE(TAG, "settimeofday: %d", errno);
        return ESP_FAIL;
    }

    // Log the current time
    struct tm timeinfo;
    if (localtime_r(&time, &timeinfo)) {
        char strftime_buf[64];
        strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
        ESP_LOGI(TAG, "The current date/time is: %s", strftime_buf);
    }

    do {
        if (board_rtc_has_power_failed()) {
            time_t time_down;
            time_t time_up;
            struct tm timeinfo;

            if (board_rtc_get_power_time_down(&time_down) != ESP_OK) {
                break;
            }
            if (board_rtc_get_power_time_down(&time_up) != ESP_OK) {
                break;
            }

            // Log the power fail timestamps.
            // Note: These do not include the year or seconds.
            if (localtime_r(&time_down, &timeinfo)) {
                ESP_LOGI(TAG, "Power down: %d/%d %02d:%02d",
                        timeinfo.tm_mon, timeinfo.tm_mday,
                        timeinfo.tm_hour, timeinfo.tm_min);
            }
            if (localtime_r(&time_up, &timeinfo)) {
                ESP_LOGI(TAG, "Power up: %d/%d %02d:%02d",
                        timeinfo.tm_mon, timeinfo.tm_mday,
                        timeinfo.tm_hour, timeinfo.tm_min);
            }
        }
    } while (0);

    return ret;
}

esp_err_t time_handler_init()
{
    esp_err_t ret;

    time_handler_init_zone();
    ret = time_handler_init_rtc();

    return ret;
}

static void time_handler_sntp_callback(struct timeval *tv)
{
    int64_t time0 = esp_timer_get_time();
    ESP_LOGI(TAG, "SNTP time updated");

    time_t now = 0;
    time_t prev = 0;
    if (time(&now) < 0) {
        return;
    }
    if (board_rtc_get_time(&prev) == ESP_OK) {
        int64_t time1 = esp_timer_get_time();
        now += (time_t)((time1 - time0) / 1000000LL);

        if (now != prev && board_rtc_set_time(&now) == ESP_OK) {
            ESP_LOGI(TAG, "Board RTC time set from SNTP result (%ld)", (now-prev));
        }
    }
}

esp_err_t time_handler_sntp_init()
{
    if (!sntp_initialized) {
        sntp_setoperatingmode(SNTP_OPMODE_POLL);

        char *hostname;
        if (settings_get_ntp_server(&hostname) == ESP_OK && hostname && strlen(hostname) > 0) {
            ESP_LOGI(TAG, "Setting configured NTP hostname: \"%s\"", hostname);
            sntp_setservername(0, hostname);
            sntp_hostname_default = false;
        } else {
            ESP_LOGI(TAG, "Setting default NTP hostname");
            sntp_setservername(0, (char *)"pool.ntp.org");
            sntp_hostname_default = true;
        }

        sntp_set_time_sync_notification_cb(time_handler_sntp_callback);
        sntp_init();
        sntp_initialized = true;
    }
    return ESP_OK;
}

esp_err_t time_handler_sntp_setservername(const char *hostname)
{
    if (!sntp_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!hostname || strlen(hostname) == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    char *new_server = strdup(hostname);
    if (!new_server) {
        return ESP_ERR_NO_MEM;
    }

    char *old_server = sntp_getservername(0);

    sntp_setservername(0, new_server);

    if (old_server && !sntp_hostname_default) {
        free(old_server);
    }

    sntp_hostname_default = false;

    return ESP_OK;
}

const char* time_handler_sntp_getservername()
{
    if (!sntp_initialized) {
        return NULL;
    }
    return sntp_getservername(0);
}
