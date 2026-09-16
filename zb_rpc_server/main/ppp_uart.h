#pragma once

#include <stdbool.h>

#include "esp_err.h"
#include "freertos/FreeRTOS.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t ppp_uart_start(void);
bool ppp_uart_is_connected(void);
bool ppp_uart_wait_connected(TickType_t timeout);

#ifdef __cplusplus
}
#endif
