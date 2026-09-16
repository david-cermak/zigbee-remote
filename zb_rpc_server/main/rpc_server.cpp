#include "rpc_server.h"

#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include "ppp_uart.h"
#include "sdkconfig.h"
#include "zigbee_api.h"

#define ZB_RPC_LOG(tag, fmt, ...) ESP_LOGI(tag, fmt, ##__VA_ARGS__)
#include "zb_rpc/engine.hpp"

namespace {

constexpr int k_socket_poll_seconds = 1;
constexpr uint32_t k_rpc_timeout_ms = 15000;
constexpr uint32_t k_annce_timeout_ms = 180000;
constexpr const char *k_tag = "rpc_server";

void set_listener_timeout(int socket_fd)
{
    const timeval timeout{.tv_sec = k_socket_poll_seconds, .tv_usec = 0};
    setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(socket_fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
}

zb_rpc::U16Resp handle_get_short_addr()
{
    return {.value = zb_rpc::to_le16(zigbee_get_short_addr())};
}

zb_rpc::U16Resp handle_get_panid()
{
    return {.value = zb_rpc::to_le16(zigbee_get_panid())};
}

zb_rpc::U8Resp handle_get_channel()
{
    return {.value = zigbee_get_channel()};
}

zb_rpc::I32Resp handle_open_network(zb_rpc::OpenNetworkReq request)
{
    return {.value = zb_rpc::to_le32_s(zigbee_open_network(request.duration))};
}

zb_rpc::U16Resp handle_wait_annce()
{
    return {.value = zb_rpc::to_le16(zigbee_wait_annce(k_annce_timeout_ms))};
}

zb_rpc::zb_addr_t handle_find_sensor(zb_rpc::FindSensorReq request)
{
    zigbee_addr_t found =
        zigbee_find_sensor(zb_rpc::from_le16(request.short_addr), k_rpc_timeout_ms);
    return {
        .err = zb_rpc::to_le32_s(found.err),
        .short_addr = zb_rpc::to_le16(found.short_addr),
        .ep = found.ep,
    };
}

zb_rpc::zb_basic_t handle_read_basic(zb_rpc::zb_addr_t request)
{
    zigbee_basic_t basic = zigbee_read_basic(zb_rpc::from_le16(request.short_addr), request.ep,
                                             k_rpc_timeout_ms);
    zb_rpc::zb_basic_t response{};
    response.err = zb_rpc::to_le32_s(basic.err);
    std::memcpy(response.manufacturer, basic.manufacturer, sizeof(response.manufacturer));
    std::memcpy(response.model, basic.model, sizeof(response.model));
    return response;
}

zb_rpc::I32Resp handle_bind_sensor(zb_rpc::zb_addr_t request)
{
    return {.value = zb_rpc::to_le32_s(zigbee_bind_sensor(zb_rpc::from_le16(request.short_addr),
                                                         request.ep, k_rpc_timeout_ms))};
}

zb_rpc::I32Resp handle_subscribe_sensor(zb_rpc::zb_addr_t request)
{
    return {.value = zb_rpc::to_le32_s(zigbee_subscribe_sensor(
                zb_rpc::from_le16(request.short_addr), request.ep, k_rpc_timeout_ms))};
}

zb_rpc::I32Resp handle_config_report(zb_rpc::zb_addr_t request)
{
    return {.value = zb_rpc::to_le32_s(zigbee_config_report(zb_rpc::from_le16(request.short_addr),
                                                           request.ep, k_rpc_timeout_ms))};
}

zb_rpc::zb_temp_t handle_read_temp(zb_rpc::zb_addr_t request)
{
    zigbee_temp_t temp =
        zigbee_read_temp(zb_rpc::from_le16(request.short_addr), request.ep, k_rpc_timeout_ms);
    return {
        .err = zb_rpc::to_le32_s(temp.err),
        .measured = zb_rpc::to_le16_s(temp.measured),
        .min_measured = zb_rpc::to_le16_s(temp.min_measured),
        .max_measured = zb_rpc::to_le16_s(temp.max_measured),
        .tolerance = zb_rpc::to_le16_s(temp.tolerance),
    };
}

zb_rpc::I16Resp handle_get_last_temp()
{
    return {.value = zb_rpc::to_le16_s(zigbee_get_last_temp())};
}

bool dispatch(zb_rpc::Engine &engine, const zb_rpc::FrameHeader &header)
{
    zb_rpc::invoke_result result =
        zb_rpc::invoke_one<zb_rpc::desc::get_short_addr, ^^handle_get_short_addr>(engine, header);
    if (result == zb_rpc::invoke_result::no_match) {
        result = zb_rpc::invoke_one<zb_rpc::desc::get_panid, ^^handle_get_panid>(engine, header);
    }
    if (result == zb_rpc::invoke_result::no_match) {
        result = zb_rpc::invoke_one<zb_rpc::desc::get_channel, ^^handle_get_channel>(engine, header);
    }
    if (result == zb_rpc::invoke_result::no_match) {
        result =
            zb_rpc::invoke_one<zb_rpc::desc::open_network, ^^handle_open_network>(engine, header);
    }
    if (result == zb_rpc::invoke_result::no_match) {
        result = zb_rpc::invoke_one<zb_rpc::desc::wait_annce, ^^handle_wait_annce>(engine, header);
    }
    if (result == zb_rpc::invoke_result::no_match) {
        result =
            zb_rpc::invoke_one<zb_rpc::desc::find_sensor, ^^handle_find_sensor>(engine, header);
    }
    if (result == zb_rpc::invoke_result::no_match) {
        result = zb_rpc::invoke_one<zb_rpc::desc::read_basic, ^^handle_read_basic>(engine, header);
    }
    if (result == zb_rpc::invoke_result::no_match) {
        result =
            zb_rpc::invoke_one<zb_rpc::desc::bind_sensor, ^^handle_bind_sensor>(engine, header);
    }
    if (result == zb_rpc::invoke_result::no_match) {
        result = zb_rpc::invoke_one<zb_rpc::desc::subscribe_sensor, ^^handle_subscribe_sensor>(
            engine, header);
    }
    if (result == zb_rpc::invoke_result::no_match) {
        result =
            zb_rpc::invoke_one<zb_rpc::desc::config_report, ^^handle_config_report>(engine, header);
    }
    if (result == zb_rpc::invoke_result::no_match) {
        result = zb_rpc::invoke_one<zb_rpc::desc::read_temp, ^^handle_read_temp>(engine, header);
    }
    if (result == zb_rpc::invoke_result::no_match) {
        result =
            zb_rpc::invoke_one<zb_rpc::desc::get_last_temp, ^^handle_get_last_temp>(engine, header);
    }
    if (result == zb_rpc::invoke_result::no_match) {
        ZB_RPC_LOG(k_tag, "unknown API id");
        if (!engine.discard_payload(header.size)) {
            return false;
        }
        engine.send_error(zb_rpc::error_code::unknown_api);
        return false;
    }
    return result == zb_rpc::invoke_result::handled;
}

void serve_client(int client_fd)
{
    zb_rpc::Engine engine(client_fd);
    while (ppp_uart_is_connected()) {
        zb_rpc::FrameHeader header{};
        if (!engine.receive_header(header)) {
            if (engine.last_error() != zb_rpc::error_code::none) {
                engine.send_error(engine.last_error());
            }
            break;
        }
        if (!dispatch(engine, header)) {
            break;
        }
    }
}

int create_listener()
{
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(CONFIG_PPP_TCP_SERVER_PORT);
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    int listener = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (listener < 0) {
        ESP_LOGE(k_tag, "socket failed: errno %d", errno);
        return -1;
    }

    const int enabled = 1;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled));
    set_listener_timeout(listener);
    if (bind(listener, reinterpret_cast<const sockaddr *>(&address), sizeof(address)) != 0 ||
        listen(listener, 1) != 0) {
        ESP_LOGE(k_tag, "bind/listen failed: errno %d", errno);
        close(listener);
        return -1;
    }

    ESP_LOGI(k_tag, "Zigbee RPC listening on PPP IPv4 port %d", CONFIG_PPP_TCP_SERVER_PORT);
    return listener;
}

void server_task(void *)
{
    while (true) {
        ppp_uart_wait_connected(portMAX_DELAY);
        int listener = create_listener();
        if (listener < 0) {
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        while (ppp_uart_is_connected()) {
            sockaddr_in source{};
            socklen_t source_size = sizeof(source);
            int client =
                accept(listener, reinterpret_cast<sockaddr *>(&source), &source_size);
            if (client < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    continue;
                }
                ESP_LOGW(k_tag, "accept failed: errno %d", errno);
                break;
            }

            char address[INET_ADDRSTRLEN]{};
            inet_ntoa_r(source.sin_addr, address, sizeof(address));
            ESP_LOGI(k_tag, "Accepted RPC client %s", address);
            serve_client(client);
            shutdown(client, SHUT_RDWR);
            close(client);
        }

        close(listener);
        ESP_LOGW(k_tag, "RPC listener stopped; waiting for PPP");
    }
}

} // namespace

extern "C" void rpc_server_start(void)
{
    if (xTaskCreate(server_task, "rpc_server", 12288, nullptr, 5, nullptr) != pdPASS) {
        ESP_LOGE(k_tag, "Failed to create RPC server task");
        std::abort();
    }
}
