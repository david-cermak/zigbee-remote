#include "ppp_uart.h"

#include <inttypes.h>
#include <stdint.h>

#include "driver/uart.h"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_netif_ppp.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#define PPP_UART_PORT UART_NUM_1
#define PPP_LINK_UP BIT0
#define PPP_PHASE_DEAD BIT1
#define PPP_RX_CHUNK_SIZE 1024
#define PPP_RX_RING_SIZE ((CONFIG_PPP_MTU * 2) + 128)
#define PPP_RECONNECT_DELAY_MS 1000
#define PPP_STOP_TIMEOUT_MS 3000

static const char *TAG = "ppp_uart";

static EventGroupHandle_t s_link_events;
static QueueHandle_t s_uart_events;
static TaskHandle_t s_reconnect_task;
static esp_netif_t *s_netif;

static esp_err_t uart_transmit(void *handle, void *buffer, size_t length)
{
    const uint8_t *bytes = buffer;
    size_t sent = 0;

    (void)handle;
    while (sent < length) {
        int written = uart_write_bytes(PPP_UART_PORT, bytes + sent, length - sent);
        if (written < 0) {
            ESP_LOGE(TAG, "UART write failed");
            return ESP_FAIL;
        }
        sent += (size_t)written;
    }
    return ESP_OK;
}

static esp_netif_driver_ifconfig_t s_driver_config = {
    .handle = (void *)1,
    .transmit = uart_transmit,
};

static bool event_is_for_our_netif(void *event_data)
{
    const ip_event_got_ip_t *event = event_data;
    return event != NULL && event->esp_netif == s_netif;
}

static void request_reconnect(void)
{
    xEventGroupClearBits(s_link_events, PPP_LINK_UP);
    if (s_reconnect_task != NULL) {
        xTaskNotifyGive(s_reconnect_task);
    }
}

static void on_ip_event(void *arg, esp_event_base_t base, int32_t event_id, void *event_data)
{
    (void)arg;
    (void)base;

    if (!event_is_for_our_netif(event_data)) {
        return;
    }

    if (event_id == IP_EVENT_PPP_GOT_IP) {
        const ip_event_got_ip_t *event = event_data;
        ESP_LOGI(TAG, "PPP up: " IPSTR ", peer " IPSTR ", MTU %d",
                 IP2STR(&event->ip_info.ip), IP2STR(&event->ip_info.gw), CONFIG_PPP_MTU);
        xEventGroupClearBits(s_link_events, PPP_PHASE_DEAD);
        xEventGroupSetBits(s_link_events, PPP_LINK_UP);
    } else if (event_id == IP_EVENT_PPP_LOST_IP) {
        ESP_LOGW(TAG, "PPP link lost; scheduling reconnect");
        request_reconnect();
    }
}

static void on_ppp_status(void *arg, esp_event_base_t base, int32_t event_id, void *event_data)
{
    (void)arg;
    (void)base;
    (void)event_data;

    if (event_id == NETIF_PPP_PHASE_DEAD) {
        xEventGroupSetBits(s_link_events, PPP_PHASE_DEAD);
        return;
    }
    if (event_id == NETIF_PPP_ERRORNONE ||
        (event_id >= NETIF_PP_PHASE_OFFSET &&
         event_id < NETIF_PPP_INTERNAL_ERR_OFFSET)) {
        return;
    }

    ESP_LOGW(TAG, "PPP status error %" PRId32 "; scheduling reconnect", event_id);
    request_reconnect();
}

static void uart_receive_task(void *arg)
{
    uint8_t buffer[PPP_RX_CHUNK_SIZE];
    uart_event_t event;

    (void)arg;
    while (true) {
        if (xQueueReceive(s_uart_events, &event, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        if (event.type == UART_DATA) {
            size_t remaining = 0;
            if (uart_get_buffered_data_len(PPP_UART_PORT, &remaining) != ESP_OK) {
                ESP_LOGE(TAG, "Failed to query UART RX length");
                continue;
            }
            while (remaining > 0) {
                size_t wanted = remaining < sizeof(buffer) ? remaining : sizeof(buffer);
                int length = uart_read_bytes(PPP_UART_PORT, buffer, wanted, 0);
                if (length <= 0) {
                    break;
                }
                if (esp_netif_receive(s_netif, buffer, (size_t)length, NULL) != ESP_OK) {
                    ESP_LOGE(TAG, "PPP input rejected %d UART bytes", length);
                }
                remaining -= (size_t)length;
            }
        } else if (event.type == UART_FIFO_OVF || event.type == UART_BUFFER_FULL) {
            ESP_LOGE(TAG, "UART RX overflow; flushing incomplete PPP frame");
            uart_flush_input(PPP_UART_PORT);
            xQueueReset(s_uart_events);
        } else if (event.type == UART_BREAK || event.type == UART_PARITY_ERR ||
                   event.type == UART_FRAME_ERR) {
            ESP_LOGW(TAG, "UART event %d", event.type);
        }
    }
}

static void reconnect_task(void *arg)
{
    (void)arg;
    while (true) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        vTaskDelay(pdMS_TO_TICKS(PPP_RECONNECT_DELAY_MS));
        if (ppp_uart_is_connected()) {
            continue;
        }

        ESP_LOGI(TAG, "Stopping failed PPP session");
        esp_netif_action_disconnected(s_netif, NULL, 0, NULL);
        EventBits_t bits = xEventGroupWaitBits(
            s_link_events, PPP_PHASE_DEAD, pdFALSE, pdTRUE,
            pdMS_TO_TICKS(PPP_STOP_TIMEOUT_MS));
        if ((bits & PPP_PHASE_DEAD) == 0) {
            ESP_LOGW(TAG, "PPP did not reach dead phase; retrying stop");
            xTaskNotifyGive(s_reconnect_task);
            continue;
        }

        esp_netif_action_stop(s_netif, NULL, 0, NULL);
        xEventGroupClearBits(s_link_events, PPP_PHASE_DEAD);
        ESP_LOGI(TAG, "Restarting PPP negotiation from dead phase");
        esp_netif_action_start(s_netif, NULL, 0, NULL);
        esp_netif_action_connected(s_netif, NULL, 0, NULL);
    }
}

esp_err_t ppp_uart_start(void)
{
    const uart_config_t uart_config = {
        .baud_rate = CONFIG_PPP_UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    s_link_events = xEventGroupCreate();
    if (s_link_events == NULL) {
        return ESP_ERR_NO_MEM;
    }

    ESP_RETURN_ON_ERROR(
        uart_driver_install(PPP_UART_PORT, PPP_RX_RING_SIZE, 0, 20, &s_uart_events, 0),
        TAG, "install UART driver");
    ESP_RETURN_ON_ERROR(uart_param_config(PPP_UART_PORT, &uart_config), TAG,
                        "configure UART");
    ESP_RETURN_ON_ERROR(
        uart_set_pin(PPP_UART_PORT, CONFIG_PPP_UART_TX_PIN, CONFIG_PPP_UART_RX_PIN,
                     UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE),
        TAG, "configure UART pins");
    ESP_RETURN_ON_ERROR(uart_set_rx_timeout(PPP_UART_PORT, 1), TAG,
                        "configure UART RX timeout");

    esp_netif_inherent_config_t base_config = ESP_NETIF_INHERENT_DEFAULT_PPP();
    base_config.if_key = "PPP_UART";
    base_config.if_desc = "ppp_uart";
    base_config.mtu = CONFIG_PPP_MTU;
    const esp_netif_config_t netif_config = {
        .base = &base_config,
        .driver = &s_driver_config,
        .stack = ESP_NETIF_NETSTACK_DEFAULT_PPP,
    };

    s_netif = esp_netif_new(&netif_config);
    if (s_netif == NULL) {
        return ESP_ERR_NO_MEM;
    }

    esp_netif_ppp_config_t ppp_config;
    ESP_RETURN_ON_ERROR(esp_netif_ppp_get_params(s_netif, &ppp_config), TAG,
                        "get PPP parameters");
    ppp_config.ppp_error_event_enabled = true;
    ppp_config.ppp_phase_event_enabled = true;
    ESP_RETURN_ON_ERROR(esp_netif_ppp_set_params(s_netif, &ppp_config), TAG,
                        "set PPP parameters");

    ESP_RETURN_ON_ERROR(
        esp_event_handler_register(IP_EVENT, IP_EVENT_PPP_GOT_IP, on_ip_event, NULL),
        TAG, "register PPP got-IP handler");
    ESP_RETURN_ON_ERROR(
        esp_event_handler_register(IP_EVENT, IP_EVENT_PPP_LOST_IP, on_ip_event, NULL),
        TAG, "register PPP lost-IP handler");
    ESP_RETURN_ON_ERROR(
        esp_event_handler_register(NETIF_PPP_STATUS, ESP_EVENT_ANY_ID, on_ppp_status, NULL),
        TAG, "register PPP event handler");

    if (xTaskCreate(uart_receive_task, "ppp_uart_rx", 3072, NULL, 6, NULL) != pdPASS ||
        xTaskCreate(reconnect_task, "ppp_reconnect", 3072, NULL, 5, &s_reconnect_task) !=
            pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Starting PPP client on UART%d, TX GPIO%d, RX GPIO%d, %d baud",
             PPP_UART_PORT, CONFIG_PPP_UART_TX_PIN, CONFIG_PPP_UART_RX_PIN,
             CONFIG_PPP_UART_BAUD_RATE);
    esp_netif_action_start(s_netif, NULL, 0, NULL);
    esp_netif_action_connected(s_netif, NULL, 0, NULL);
    return ESP_OK;
}

bool ppp_uart_is_connected(void)
{
    return s_link_events != NULL &&
           (xEventGroupGetBits(s_link_events) & PPP_LINK_UP) != 0;
}

bool ppp_uart_wait_connected(TickType_t timeout)
{
    if (s_link_events == NULL) {
        return false;
    }
    return (xEventGroupWaitBits(s_link_events, PPP_LINK_UP, pdFALSE, pdTRUE, timeout) &
            PPP_LINK_UP) != 0;
}
