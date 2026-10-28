#include <arpa/inet.h>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <getopt.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <sys/socket.h>
#include <unistd.h>

#ifdef SINGLE_FILE_COMPILATION
/* Header-only build for Compiler Explorer / plain `g++ main.cpp`:
 * generate it with ./generate_single_file.py. */
#include "header_zb_rpc_client.hpp"
#else
#include "ppp_link.h"
#include "serial_io.h"
/* The RPC-backed version of the firmware Zigbee API, and the portable
 * thermostat sequence that only uses that API. */
#include "thermostat_app.hpp"
#include "zigbee_api.h"
#endif

namespace {

volatile sig_atomic_t g_stop_requested = 0;
volatile sig_atomic_t g_rpc_socket = -1;

/* Shut the RPC socket down so a blocking read returns and the server sees an
 * orderly disconnect.  This is important when the user interrupts the long
 * DEVICE_ANNCE wait: without it the ESP stays blocked in that handler and the
 * next client run is never served. */
void on_signal(int)
{
    g_stop_requested = 1;
    const int fd = g_rpc_socket;
    if (fd >= 0) {
        ::shutdown(fd, SHUT_RDWR);
    }
}

void install_signal_handlers()
{
    struct sigaction action {};
    action.sa_handler = on_signal;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;
    sigaction(SIGINT, &action, nullptr);
    sigaction(SIGTERM, &action, nullptr);
}

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
        if (g_stop_requested) {
            return false;
        }
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
        if (g_stop_requested) {
            return -1;
        }
        int socket_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (socket_fd < 0) {
            return -1;
        }

        /* A blocking connect over the TUN can hang for minutes while the ESP
         * is still restarting PPP, which also makes Ctrl-C ineffective. */
        const int flags = fcntl(socket_fd, F_GETFL, 0);
        if (flags < 0 || fcntl(socket_fd, F_SETFL, flags | O_NONBLOCK) < 0) {
            close(socket_fd);
            return -1;
        }

        int result =
            connect(socket_fd, reinterpret_cast<const sockaddr *>(&address), sizeof(address));
        if (result == 0) {
            fcntl(socket_fd, F_SETFL, flags);
            return socket_fd;
        }

        if (errno == EINPROGRESS || errno == EWOULDBLOCK || errno == EINTR) {
            pollfd waiting{.fd = socket_fd, .events = POLLOUT, .revents = 0};
            int ready;
            do {
                ready = poll(&waiting, 1, 200);
            } while (ready < 0 && errno == EINTR && !g_stop_requested);
            if (ready > 0) {
                int error = 0;
                socklen_t length = sizeof(error);
                if (getsockopt(socket_fd, SOL_SOCKET, SO_ERROR, &error, &length) == 0 &&
                    error == 0) {
                    fcntl(socket_fd, F_SETFL, flags);
                    return socket_fd;
                }
            }
        }

        close(socket_fd);
        if (!g_stop_requested) {
            usleep(200000);
        }
    }
    std::fprintf(stderr, "RPC connect to %s:%u failed: %s\n", host, static_cast<unsigned>(port),
                 std::strerror(errno));
    return -1;
}

} // namespace

int main(int argc, char **argv)
{
    Options options;
    if (!parse_options(argc, argv, options)) {
        print_usage(argv[0]);
        return 1;
    }
    install_signal_handlers();

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
    if (!ppp_link_start(link)) {
        ppp_link_stop(link);
        serial_io_close(serial);
        ppp_link_destroy(link);
        return 1;
    }

    std::fprintf(stderr, "PPP server on %s @ %d; waiting for Zigbee RPC peer\n", options.device,
                 options.baud);
    bool success = false;
    if (g_stop_requested) {
        /* Interrupted while waiting for the PPP link. */
    } else if (!wait_for_link(options.wait_seconds)) {
        std::fprintf(stderr, "PPP link timeout\n");
    } else {
        int socket_fd = connect_rpc(options.host, options.port);
        if (socket_fd >= 0) {
            g_rpc_socket = socket_fd;
            zigbee_rpc::attach(socket_fd);
            thermostat_app::Result result = thermostat_app::Result::failure;
            if (!g_stop_requested) {
                result = thermostat_app::run(true);
                if (result == thermostat_app::Result::retry_without_cache && !g_stop_requested) {
                    /* Reconnect and repeat with the pre-cache commissioning flow. */
                    zigbee_rpc::detach();
                    g_rpc_socket = -1;
                    shutdown(socket_fd, SHUT_RDWR);
                    close(socket_fd);
                    socket_fd = connect_rpc(options.host, options.port);
                    if (socket_fd >= 0) {
                        g_rpc_socket = socket_fd;
                        zigbee_rpc::attach(socket_fd);
                        result = thermostat_app::run(false);
                    }
                }
            }
            zigbee_rpc::detach();
            if (socket_fd >= 0) {
                g_rpc_socket = -1;
                shutdown(socket_fd, SHUT_RDWR);
                close(socket_fd);
            }
            success = result == thermostat_app::Result::success;
        }
    }

    ppp_link_stop(link);
    serial_io_close(serial);
    ppp_link_destroy(link);
    return success ? 0 : 1;
}
