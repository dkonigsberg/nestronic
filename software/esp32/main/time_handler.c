#include "time_handler.h"

#include <esp_err.h>
#include <esp_log.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <errno.h>

#include "settings.h"
#include "zoneinfo.h"
#include "board_rtc.h"
#include "my_sntp.h"

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

    return ret;
}

esp_err_t time_handler_init()
{
    esp_err_t ret;

    time_handler_init_zone();
    ret = time_handler_init_rtc();

    return ret;
}

static void time_handler_sntp_callback()
{
    ESP_LOGI(TAG, "SNTP time updated");

    time_t now = 0;
    if (time(&now) < 0) {
        return;
    }

    if (board_rtc_set_time(&now) == ESP_OK) {
        ESP_LOGI(TAG, "Board RTC time set from SNTP result");
    }
}

esp_err_t time_handler_sntp_init()
{
    if (!sntp_initialized) {
        my_sntp_setoperatingmode(SNTP_OPMODE_POLL);

        char *hostname;
        if (settings_get_ntp_server(&hostname) == ESP_OK) {
            ESP_LOGI(TAG, "Setting configured NTP hostname: \"%s\"", hostname);
            my_sntp_setservername(0, hostname);
            sntp_hostname_default = false;
        } else {
            ESP_LOGI(TAG, "Setting default NTP hostname");
            my_sntp_setservername(0, (char *)"pool.ntp.org");
            sntp_hostname_default = true;
        }

        my_sntp_init(time_handler_sntp_callback);
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

    char *old_server = my_sntp_getservername(0);

    my_sntp_setservername(0, new_server);

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
    return my_sntp_getservername(0);
}
