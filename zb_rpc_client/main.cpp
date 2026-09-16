#include <arpa/inet.h>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <getopt.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include "ppp_link.h"
#include "serial_io.h"
#include "zb_rpc/engine.hpp"

namespace {

constexpr const char *k_tag = "THERMOSTAT";

struct Options {
    const char *device = "/dev/ttyUSB1";
    const char *host = zb_rpc::k_default_host;
    uint16_t port = zb_rpc::k_default_port;
    int baud = 115200;
    int wait_seconds = 60;
    bool verbose = false;
};

void print_usage(const char *program)
{
    std::fprintf(stderr,
                 "Usage: %s [--device PATH] [--baud RATE] [--host IP] "
                 "[--port PORT] [--wait-link SEC] [-v]\n",
                 program);
}

bool parse_options(int argc, char **argv, Options &options)
{
    const option long_options[] = {
        {"device", required_argument, nullptr, 'd'},
        {"baud", required_argument, nullptr, 'b'},
        {"host", required_argument, nullptr, 'H'},
        {"port", required_argument, nullptr, 'p'},
        {"wait-link", required_argument, nullptr, 'w'},
        {"verbose", no_argument, nullptr, 'v'},
        {"help", no_argument, nullptr, 'h'},
        {},
    };

    int selected;
    while ((selected = getopt_long(argc, argv, "d:b:H:p:w:vh", long_options, nullptr)) != -1) {
        switch (selected) {
        case 'd': options.device = optarg; break;
        case 'b': options.baud = std::atoi(optarg); break;
        case 'H': options.host = optarg; break;
        case 'p': options.port = static_cast<uint16_t>(std::atoi(optarg)); break;
        case 'w': options.wait_seconds = std::atoi(optarg); break;
        case 'v': options.verbose = true; break;
        case 'h': print_usage(argv[0]); std::exit(0);
        default: return false;
        }
    }
    return options.baud > 0 && options.port > 0 && options.wait_seconds > 0;
}

bool wait_for_link(int seconds)
{
    for (int elapsed = 0; elapsed < seconds * 10; ++elapsed) {
        if (ppp_link_is_up()) {
            return true;
        }
        usleep(100000);
    }
    return false;
}

int connect_rpc(const char *host, uint16_t port)
{
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    if (inet_pton(AF_INET, host, &address.sin_addr) != 1) {
        std::fprintf(stderr, "Invalid RPC host: %s\n", host);
        return -1;
    }

    for (int attempt = 0; attempt < 50; ++attempt) {
        int socket_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (socket_fd < 0) {
            return -1;
        }
        if (connect(socket_fd, reinterpret_cast<const sockaddr *>(&address), sizeof(address)) ==
            0) {
            return socket_fd;
        }
        close(socket_fd);
        usleep(200000);
    }
    std::fprintf(stderr, "RPC connect to %s:%u failed: %s\n", host, static_cast<unsigned>(port),
                 std::strerror(errno));
    return -1;
}

float to_celsius(int16_t hundredths)
{
    return hundredths / 100.0f;
}

bool run_thermostat(int socket_fd)
{
    using namespace zb_rpc;
    Engine engine(socket_fd);

    ZB_RPC_LOG(k_tag, "Start ESP Zigbee Stack");
    ZB_RPC_LOG(k_tag, "Initialize Zigbee stack");

    U16Resp short_addr{};
    U16Resp panid{};
    U8Resp channel{};
    if (!call<desc::get_short_addr>(engine, short_addr) ||
        !call<desc::get_panid>(engine, panid) || !call<desc::get_channel>(engine, channel)) {
        return false;
    }
    (void)channel;

    OpenNetworkReq open{.duration = 180};
    I32Resp open_status{};
    if (!call<desc::open_network>(engine, open, open_status) ||
        from_le32_s(open_status.value) != 0) {
        ZB_RPC_LOG(k_tag, "Failed to open network");
        return false;
    }
    ZB_RPC_LOG(k_tag, "Network(0x%04x) is open for %u seconds", from_le16(panid.value),
               open.duration);

    U16Resp peer{};
    if (!call<desc::wait_annce>(engine, peer) || from_le16(peer.value) == 0) {
        ZB_RPC_LOG(k_tag, "Timed out waiting for DEVICE_ANNCE");
        return false;
    }
    ZB_RPC_LOG(k_tag, "New device commissioned or rejoined (short: 0x%04x)",
               from_le16(peer.value));

    ZB_RPC_LOG(k_tag, "Attempt to find HA temperature sensor device on address(0x%04x)",
               from_le16(peer.value));
    zb_addr_t sensor{};
    if (!call<desc::find_sensor>(engine, FindSensorReq{.short_addr = peer.value}, sensor) ||
        from_le32_s(sensor.err) != 0) {
        ZB_RPC_LOG(k_tag, "Failed to find HA temperature sensor");
        return false;
    }

    ZB_RPC_LOG(k_tag, "Attempt to read manuf_code and model_id from device (0x%04x, 0x%02x)",
               from_le16(sensor.short_addr), sensor.ep);
    zb_basic_t basic{};
    if (!call<desc::read_basic>(engine, sensor, basic) || from_le32_s(basic.err) != 0) {
        ZB_RPC_LOG(k_tag, "Failed to read Basic attributes");
        return false;
    }
    ZB_RPC_LOG(k_tag, "Model identifier: %s", basic.model);
    ZB_RPC_LOG(k_tag, "Manufacturer name: %s", basic.manufacturer);

    ZB_RPC_LOG(k_tag, "Attempt to bind temperature sensor device (0x%04x, 0x%02x) to local",
               from_le16(sensor.short_addr), sensor.ep);
    I32Resp bind_status{};
    call<desc::bind_sensor>(engine, sensor, bind_status);
    ZB_RPC_LOG(k_tag, "Attempt to subscribe temperature sensor device (0x%04x, 0x%02x) from local",
               from_le16(sensor.short_addr), sensor.ep);
    I32Resp subscribe_status{};
    call<desc::subscribe_sensor>(engine, sensor, subscribe_status);
    ZB_RPC_LOG(k_tag, "Bound HA temperature sensor device (0x%04x, 0x%02x) to local successfully",
               from_le16(short_addr.value), sensor.ep);
    ZB_RPC_LOG(k_tag, "Attempt to configure reporting for HA temperature sensor");
    I32Resp report_status{};
    call<desc::config_report>(engine, sensor, report_status);
    ZB_RPC_LOG(k_tag, "Subscribed HA temperature sensor device (0x%04x, 0x%02x) from local successfully",
               from_le16(sensor.short_addr), sensor.ep);

    for (int i = 0; i < 5; ++i) {
        I16Resp last{};
        if (!call<desc::get_last_temp>(engine, last)) {
            return false;
        }
        ZB_RPC_LOG(k_tag, "Temperature sensor measured value: %.2f degrees Celsius",
                   to_celsius(from_le16_s(last.value)));
        usleep(2000000);
    }

    zb_temp_t attrs{};
    if (!call<desc::read_temp>(engine, sensor, attrs) || from_le32_s(attrs.err) != 0) {
        ZB_RPC_LOG(k_tag, "Failed to read temperature attributes");
        return false;
    }
    ZB_RPC_LOG(k_tag, "Read Attributes of Temperature Measurement");
    ZB_RPC_LOG(k_tag, "Measured value: %.2f degrees Celsius",
               to_celsius(from_le16_s(attrs.measured)));
    ZB_RPC_LOG(k_tag, "Min measured value: %.2f degrees Celsius",
               to_celsius(from_le16_s(attrs.min_measured)));
    ZB_RPC_LOG(k_tag, "Max measured value: %.2f degrees Celsius",
               to_celsius(from_le16_s(attrs.max_measured)));
    return true;
}

} // namespace

int main(int argc, char **argv)
{
    Options options;
    if (!parse_options(argc, argv, options)) {
        print_usage(argv[0]);
        return 1;
    }

    serial_io_t *serial =
        serial_io_open(options.device, options.baud, options.verbose ? 1 : 0);
    if (serial == nullptr) {
        return 1;
    }

    const ppp_config_t ppp{
        .mode = PPP_LINK_MODE_SERVER,
        .local_ip = "192.168.11.1",
        .peer_ip = "192.168.11.2",
        .dns_ip = "8.8.8.8",
        .ipv6 = 0,
        .netdev = 0,
    };
    const ppp_link_config_t link_config{
        .tun_dev = "/dev/net/tun",
        .tun_if = "tun0",
        .ppp = &ppp,
    };
    ppp_link_t *link = ppp_link_create(&link_config);
    if (link == nullptr) {
        serial_io_close(serial);
        return 1;
    }
    serial_io_attach(link, serial);

    std::fprintf(stderr, "PPP server on %s @ %d; waiting for Zigbee RPC peer\n", options.device,
                 options.baud);
    bool success = false;
    if (!wait_for_link(options.wait_seconds)) {
        std::fprintf(stderr, "PPP link timeout\n");
    } else {
        int socket_fd = connect_rpc(options.host, options.port);
        if (socket_fd >= 0) {
            success = run_thermostat(socket_fd);
            shutdown(socket_fd, SHUT_RDWR);
            close(socket_fd);
        }
    }

    serial_io_close(serial);
    ppp_link_destroy(link);
    return success ? 0 : 1;
}
