#include "sim_zigbee.hpp"

#include "zb_rpc/catalog.hpp"
#include "zb_rpc/invoke.hpp"
#include "zb_rpc/log.hpp"
#include "zb_rpc/sock.hpp"

#include <cstdlib>
#include <unistd.h>

using namespace zb_rpc;

static bool handle_commands(RpcEngine &rpc)
{
    RpcHeader h = rpc.get_header();
    if (h.id == api_id::ERROR) {
        return false;
    }

    bool hit = invoke_one<desc::get_short_addr, ^^sim::get_short_addr>(rpc, h) ||
               invoke_one<desc::get_panid, ^^sim::get_panid>(rpc, h) ||
               invoke_one<desc::get_channel, ^^sim::get_channel>(rpc, h) ||
               invoke_one<desc::open_network, ^^sim::open_network>(rpc, h) ||
               invoke_one<desc::wait_annce, ^^sim::wait_annce>(rpc, h) ||
               invoke_one<desc::find_sensor, ^^sim::find_sensor>(rpc, h) ||
               invoke_one<desc::read_basic, ^^sim::read_basic>(rpc, h) ||
               invoke_one<desc::bind_sensor, ^^sim::bind_sensor>(rpc, h) ||
               invoke_one<desc::subscribe_sensor, ^^sim::subscribe_sensor>(rpc, h) ||
               invoke_one<desc::config_report, ^^sim::config_report>(rpc, h) ||
               invoke_one<desc::read_temp, ^^sim::read_temp>(rpc, h) ||
               invoke_one<desc::get_last_temp, ^^sim::get_last_temp>(rpc, h);
    if (!hit) {
        ZB_RPC_LOG("server", "unknown api_id %s", rpc_api_name(h.id));
        return false;
    }
    return true;
}

int main(int argc, char **argv)
{
    const char *host = argc > 1 ? argv[1] : k_default_host;
    uint16_t port = argc > 2 ? static_cast<uint16_t>(std::atoi(argv[2])) : k_default_port;

    int listen_fd = rpc_listen(host, port);
    if (listen_fd < 0) {
        return 1;
    }

    ZB_RPC_LOG("server", "waiting for client");
    int conn = rpc_accept(listen_fd);
    ::close(listen_fd);
    if (conn < 0) {
        return 1;
    }
    ZB_RPC_LOG("server", "client connected");

    RpcEngine rpc;
    rpc.attach(conn);
    while (handle_commands(rpc)) {
    }

    ::close(conn);
    ZB_RPC_LOG("server", "done");
    return 0;
}
