#include "zb_rpc/catalog.hpp"
#include "zb_rpc/engine.hpp"
#include "zb_rpc/log.hpp"
#include "zb_rpc/sock.hpp"

#include <cstdlib>
#include <unistd.h>

using namespace zb_rpc;

static constexpr const char *k_tag = "THERMOSTAT";

static float to_celsius(int16_t hundredths) { return hundredths / 100.0f; }

int main(int argc, char **argv)
{
    const char *host = argc > 1 ? argv[1] : k_default_host;
    uint16_t port = argc > 2 ? static_cast<uint16_t>(std::atoi(argv[2])) : k_default_port;

    ZB_RPC_LOG(k_tag, "Start ESP Zigbee Stack");

    int fd = rpc_connect(host, port);
    if (fd < 0) {
        return 1;
    }
    Engine engine(fd);

    ZB_RPC_LOG(k_tag, "Initialize Zigbee stack");
    ZB_RPC_LOG(k_tag, "Deferred driver initialization successful");
    ZB_RPC_LOG(k_tag, "Device started up in non factory-reset mode");
    ZB_RPC_LOG(k_tag, "Device reboot");

    U16Resp short_addr_resp{};
    U16Resp panid_resp{};
    U8Resp channel_resp{};
    if (!api::get_short_addr(engine, short_addr_resp) || !api::get_panid(engine, panid_resp) ||
        !api::get_channel(engine, channel_resp)) {
        ::close(fd);
        return 1;
    }
    const uint16_t short_addr = from_le16(short_addr_resp.value);
    const uint16_t panid = from_le16(panid_resp.value);
    (void)channel_resp;

    OpenNetworkReq open_req{.duration = 180};
    I32Resp open_resp{};
    if (!api::open_network(engine, open_req, open_resp) || from_le32_s(open_resp.value) != 0) {
        ZB_RPC_LOG(k_tag, "Failed to open network");
        ::close(fd);
        return 1;
    }
    ZB_RPC_LOG(k_tag, "Network(0x%04x) is open for %u seconds", panid, open_req.duration);

    U16Resp peer_resp{};
    if (!api::wait_annce(engine, peer_resp)) {
        ::close(fd);
        return 1;
    }
    const uint16_t peer = from_le16(peer_resp.value);
    ZB_RPC_LOG(k_tag, "New device commissioned or rejoined (short: 0x%04x)", peer);

    ZB_RPC_LOG(k_tag, "Attempt to find HA temperature sensor device on address(0x%04x)", peer);
    FindSensorReq find_req{.short_addr = to_le16(peer)};
    zb_addr_t sensor{};
    if (!api::find_sensor(engine, find_req, sensor) || from_le32_s(sensor.err) != 0) {
        ZB_RPC_LOG(k_tag, "Failed to find HA temperature sensor");
        ::close(fd);
        return 1;
    }

    ZB_RPC_LOG(k_tag, "Attempt to read manuf_code and model_id from device (0x%04x, 0x%02x)",
               from_le16(sensor.short_addr), sensor.ep);
    zb_basic_t basic{};
    if (!api::read_basic(engine, sensor, basic)) {
        ::close(fd);
        return 1;
    }
    ZB_RPC_LOG(k_tag,
               "ZCL Read Attribute Response message for endpoint(1) cluster(0x0000) client with "
               "status(0x00)");
    ZB_RPC_LOG(k_tag, "Model identifier: %s", basic.model);
    ZB_RPC_LOG(k_tag, "Manufacturer name: %s", basic.manufacturer);

    ZB_RPC_LOG(k_tag, "Attempt to bind temperature sensor device (0x%04x, 0x%02x) to local",
               from_le16(sensor.short_addr), sensor.ep);
    I32Resp bind_resp{};
    api::bind_sensor(engine, sensor, bind_resp);
    ZB_RPC_LOG(k_tag, "Attempt to subscribe temperature sensor device (0x%04x, 0x%02x) from local",
               from_le16(sensor.short_addr), sensor.ep);
    I32Resp sub_resp{};
    api::subscribe_sensor(engine, sensor, sub_resp);
    ZB_RPC_LOG(k_tag, "Bound HA temperature sensor device (0x%04x, 0x%02x) to local successfully",
               short_addr, sensor.ep);
    ZB_RPC_LOG(k_tag, "Attempt to configure reporting for HA temperature sensor");
    I32Resp cfg_resp{};
    api::config_report(engine, sensor, cfg_resp);
    ZB_RPC_LOG(k_tag, "Network(0x%04x) is open for %u seconds", panid, open_req.duration);
    ZB_RPC_LOG(k_tag,
               "Subscribed HA temperature sensor device (0x%04x, 0x%02x) from local successfully",
               from_le16(sensor.short_addr), sensor.ep);
    ZB_RPC_LOG(k_tag,
               "ZCL Report Config Response message for endpoint(1) cluster(0x0402) client with "
               "status(0x00)");

    for (int i = 0; i < 5; ++i) {
        I16Resp t{};
        if (!api::get_last_temp(engine, t)) {
            break;
        }
        ZB_RPC_LOG(k_tag,
                   "ZCL Report Attribute message for endpoint(1) cluster(0x0402) client with "
                   "status(0x00)");
        ZB_RPC_LOG(k_tag, "Temperature sensor measured value: %.2f degrees Celsius",
                   to_celsius(from_le16_s(t.value)));
    }

    zb_temp_t attrs{};
    if (!api::read_temp(engine, sensor, attrs)) {
        ::close(fd);
        return 1;
    }
    ZB_RPC_LOG(k_tag, "Read Attributes of Temperature Measurement");
    ZB_RPC_LOG(k_tag,
               "ZCL Read Attribute Response message for endpoint(1) cluster(0x0402) client with "
               "status(0x00)");
    ZB_RPC_LOG(k_tag, "Measured value: %.2f degrees Celsius",
               to_celsius(from_le16_s(attrs.measured)));
    ZB_RPC_LOG(k_tag, "Min measured value: %.2f degrees Celsius",
               to_celsius(from_le16_s(attrs.min_measured)));
    ZB_RPC_LOG(k_tag, "Max measured value: %.2f degrees Celsius",
               to_celsius(from_le16_s(attrs.max_measured)));

    ::close(fd);
    return 0;
}
