#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "ppp_uart.h"
#include "rpc_server.h"
#include "zigbee_api.h"

static const char *TAG = "app";

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(ppp_uart_start());
    ESP_ERROR_CHECK(zigbee_stack_start());
    rpc_server_start();
    ESP_LOGI(TAG, "Zigbee RPC server started");
}
