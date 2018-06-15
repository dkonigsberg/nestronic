#ifndef TIME_HANDLER_H
#define TIME_HANDLER_H

#include <esp_err.h>

esp_err_t time_handler_init();
esp_err_t time_handler_sntp_init();
esp_err_t time_handler_sntp_setservername(const char *hostname);
const char* time_handler_sntp_getservername();

#endif /* TIME_HANDLER_H */
