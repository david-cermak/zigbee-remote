#include "sim_zigbee.hpp"

#include "zb_rpc/engine.hpp"
#include "zb_rpc/log.hpp"
#include "zb_rpc/sock.hpp"

#include <cstdlib>
#include <unistd.h>

using namespace zb_rpc;

static bool handle_commands(Engine &engine)
{
    FrameHeader header{};
    if (!engine.receive_header(header)) {
        if (engine.last_error() != error_code::none) {
            engine.send_error(engine.last_error());
        }
        return false;
    }
    if (header.id == api_id::error) {
        return false;
    }

    invoke_result result = invoke_one<desc::get_short_addr, ^^sim::get_short_addr>(engine, header);
    if (result == invoke_result::no_match) {
        result = invoke_one<desc::get_panid, ^^sim::get_panid>(engine, header);
    }
    if (result == invoke_result::no_match) {
        result = invoke_one<desc::get_channel, ^^sim::get_channel>(engine, header);
    }
    if (result == invoke_result::no_match) {
        result = invoke_one<desc::open_network, ^^sim::open_network>(engine, header);
    }
    if (result == invoke_result::no_match) {
        result = invoke_one<desc::wait_annce, ^^sim::wait_annce>(engine, header);
    }
    if (result == invoke_result::no_match) {
        result = invoke_one<desc::find_sensor, ^^sim::find_sensor>(engine, header);
    }
    if (result == invoke_result::no_match) {
        result = invoke_one<desc::read_basic, ^^sim::read_basic>(engine, header);
    }
    if (result == invoke_result::no_match) {
        result = invoke_one<desc::bind_sensor, ^^sim::bind_sensor>(engine, header);
    }
    if (result == invoke_result::no_match) {
        result = invoke_one<desc::subscribe_sensor, ^^sim::subscribe_sensor>(engine, header);
    }
    if (result == invoke_result::no_match) {
        result = invoke_one<desc::config_report, ^^sim::config_report>(engine, header);
    }
    if (result == invoke_result::no_match) {
        result = invoke_one<desc::read_temp, ^^sim::read_temp>(engine, header);
    }
    if (result == invoke_result::no_match) {
        result = invoke_one<desc::get_last_temp, ^^sim::get_last_temp>(engine, header);
    }
    if (result == invoke_result::no_match) {
        ZB_RPC_LOG("server", "unknown api_id %s", api_name(header.id));
        if (!engine.discard_payload(header.size)) {
            return false;
        }
        engine.send_error(error_code::unknown_api);
        return false;
    }
    return result == invoke_result::handled;
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

    Engine engine(conn);
    while (handle_commands(engine)) {
    }

    ::close(conn);
    ZB_RPC_LOG("server", "done");
    return 0;
}
