#include "zb_rpc/catalog.hpp"
#include "zb_rpc/invoke.hpp"
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
    RpcEngine rpc;
    rpc.attach(fd);

    ZB_RPC_LOG(k_tag, "Initialize Zigbee stack");
    ZB_RPC_LOG(k_tag, "Deferred driver initialization successful");
    ZB_RPC_LOG(k_tag, "Device started up in non factory-reset mode");
    ZB_RPC_LOG(k_tag, "Device reboot");

    uint16_t short_addr = rpc_call<desc::get_short_addr>(rpc);
    uint16_t panid = rpc_call<desc::get_panid>(rpc);
    uint8_t channel = rpc_call<desc::get_channel>(rpc);
    (void)channel;

    uint8_t duration = 180;
    if (rpc_call<desc::open_network>(rpc, duration) != 0) {
        ZB_RPC_LOG(k_tag, "Failed to open network");
        ::close(fd);
        return 1;
    }
    ZB_RPC_LOG(k_tag, "Network(0x%04x) is open for %u seconds", panid, duration);

    uint16_t peer = rpc_call<desc::wait_annce>(rpc);
    ZB_RPC_LOG(k_tag, "New device commissioned or rejoined (short: 0x%04x)", peer);

    ZB_RPC_LOG(k_tag, "Attempt to find HA temperature sensor device on address(0x%04x)", peer);
    zb_addr_t sensor = rpc_call<desc::find_sensor>(rpc, peer);
    if (sensor.err != 0) {
        ZB_RPC_LOG(k_tag, "Failed to find HA temperature sensor");
        ::close(fd);
        return 1;
    }

    ZB_RPC_LOG(k_tag, "Attempt to read manuf_code and model_id from device (0x%04x, 0x%02x)",
               sensor.short_addr, sensor.ep);
    zb_basic_t basic = rpc_call<desc::read_basic>(rpc, sensor);
    ZB_RPC_LOG(k_tag, "ZCL Read Attribute Response message for endpoint(1) cluster(0x0000) client with status(0x00)");
    ZB_RPC_LOG(k_tag, "Model identifier: %s", basic.model);
    ZB_RPC_LOG(k_tag, "Manufacturer name: %s", basic.manufacturer);

    ZB_RPC_LOG(k_tag, "Attempt to bind temperature sensor device (0x%04x, 0x%02x) to local",
               sensor.short_addr, sensor.ep);
    rpc_call<desc::bind_sensor>(rpc, sensor);
    ZB_RPC_LOG(k_tag, "Attempt to subscribe temperature sensor device (0x%04x, 0x%02x) from local",
               sensor.short_addr, sensor.ep);
    rpc_call<desc::subscribe_sensor>(rpc, sensor);
    ZB_RPC_LOG(k_tag, "Bound HA temperature sensor device (0x%04x, 0x%02x) to local successfully",
               short_addr, sensor.ep);
    ZB_RPC_LOG(k_tag, "Attempt to configure reporting for HA temperature sensor");
    rpc_call<desc::config_report>(rpc, sensor);
    ZB_RPC_LOG(k_tag, "Network(0x%04x) is open for %u seconds", panid, duration);
    ZB_RPC_LOG(k_tag, "Subscribed HA temperature sensor device (0x%04x, 0x%02x) from local successfully",
               sensor.short_addr, sensor.ep);
    ZB_RPC_LOG(k_tag, "ZCL Report Config Response message for endpoint(1) cluster(0x0402) client with status(0x00)");

    for (int i = 0; i < 5; ++i) {
        int16_t t = rpc_call<desc::get_last_temp>(rpc);
        ZB_RPC_LOG(k_tag, "ZCL Report Attribute message for endpoint(1) cluster(0x0402) client with status(0x00)");
        ZB_RPC_LOG(k_tag, "Temperature sensor measured value: %.2f degrees Celsius", to_celsius(t));
    }

    zb_temp_t attrs = rpc_call<desc::read_temp>(rpc, sensor);
    ZB_RPC_LOG(k_tag, "Read Attributes of Temperature Measurement");
    ZB_RPC_LOG(k_tag, "ZCL Read Attribute Response message for endpoint(1) cluster(0x0402) client with status(0x00)");
    ZB_RPC_LOG(k_tag, "Measured value: %.2f degrees Celsius", to_celsius(attrs.measured));
    ZB_RPC_LOG(k_tag, "Min measured value: %.2f degrees Celsius", to_celsius(attrs.min_measured));
    ZB_RPC_LOG(k_tag, "Max measured value: %.2f degrees Celsius", to_celsius(attrs.max_measured));

    ::close(fd);
    return 0;
}
