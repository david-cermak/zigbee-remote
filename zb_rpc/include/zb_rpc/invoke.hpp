#pragma once

#include "zb_rpc/impl.hpp"
#include "zb_rpc/log.hpp"

#include <type_traits>

namespace zb_rpc {

template <typename Desc>
auto rpc_call(RpcEngine &rpc) -> typename Desc::resp_type
{
    static_assert(std::is_void_v<typename Desc::req_type>);
    if (!rpc.send(Desc::id)) {
        return {};
    }
    RpcHeader h = rpc.get_header();
    return rpc.get_payload<typename Desc::resp_type>(Desc::id, h);
}

template <typename Desc>
auto rpc_call(RpcEngine &rpc, const typename Desc::req_type &req) -> typename Desc::resp_type
{
    static_assert(!std::is_void_v<typename Desc::req_type>);
    if (!rpc.send(Desc::id, &req)) {
        return {};
    }
    RpcHeader h = rpc.get_header();
    return rpc.get_payload<typename Desc::resp_type>(Desc::id, h);
}

template <typename Desc, auto Fn>
bool invoke_one(RpcEngine &rpc, const RpcHeader &h)
{
    if (h.id != Desc::id) {
        return false;
    }
    using Req = typename Desc::req_type;
    using Resp = typename Desc::resp_type;
    Resp resp{};
    if constexpr (std::is_void_v<Req>) {
        if (h.size != 0) {
            ZB_RPC_LOG("rpc", "%s expected empty payload", rpc_api_name(h.id));
            return true;
        }
        resp = [:Fn:]();
    } else {
        Req req = rpc.get_payload<Req>(Desc::id, h);
        resp = [:Fn:](req);
    }
    rpc.send(Desc::id, &resp);
    return true;
}

} // namespace zb_rpc
