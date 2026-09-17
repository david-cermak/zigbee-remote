#include "sim_zigbee.hpp"

#include "zb_rpc/auto_rpc.hpp"
#include "zb_rpc/catalog.hpp"
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

    const invoke_result result = dispatch_ns<^^::sim>(engine, header);
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

    // Compile-time catalog is verified via static_assert; echo it once at startup.
    ZB_RPC_LOG("server", "%s", k_sim_catalog);

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
