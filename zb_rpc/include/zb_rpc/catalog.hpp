#pragma once

// Gradual L2 catalog: desc::* aliases derived from sim:: signatures,
// plus named inline client wrappers.

#include "sim_zigbee.hpp"

#include "zb_rpc/auto_rpc.hpp"

namespace zb_rpc {

static_assert(verify_catalog<^^::sim>());

namespace desc {
using get_short_addr = fn_desc<^^sim::get_short_addr>;
using get_panid = fn_desc<^^sim::get_panid>;
using get_channel = fn_desc<^^sim::get_channel>;
using open_network = fn_desc<^^sim::open_network>;
using wait_annce = fn_desc<^^sim::wait_annce>;
using find_sensor = fn_desc<^^sim::find_sensor>;
using read_basic = fn_desc<^^sim::read_basic>;
using bind_sensor = fn_desc<^^sim::bind_sensor>;
using subscribe_sensor = fn_desc<^^sim::subscribe_sensor>;
using config_report = fn_desc<^^sim::config_report>;
using read_temp = fn_desc<^^sim::read_temp>;
using get_last_temp = fn_desc<^^sim::get_last_temp>;
} // namespace desc

static_assert(desc::get_short_addr::id == api_id::get_short_addr);
static_assert(desc::find_sensor::id == api_id::find_sensor);
static_assert(desc::get_last_temp::id == api_id::get_last_temp);
static_assert(std::is_same_v<desc::open_network::req_type, OpenNetworkReq>);
static_assert(std::is_same_v<desc::get_panid::req_type, EmptyReq>);

namespace api {

#define ZB_RPC_WRAP_EMPTY(name)                                                                    \
    inline bool name(Engine &engine, typename fn_desc<^^sim::name>::resp_type &response)           \
    {                                                                                              \
        return rpc_call_fn<^^sim::name>(engine, response);                                         \
    }

#define ZB_RPC_WRAP_REQ(name)                                                                      \
    inline bool name(Engine &engine, const typename fn_desc<^^sim::name>::req_type &request,        \
                     typename fn_desc<^^sim::name>::resp_type &response)                            \
    {                                                                                              \
        return rpc_call_fn<^^sim::name>(engine, request, response);                                \
    }

ZB_RPC_WRAP_EMPTY(get_short_addr)
ZB_RPC_WRAP_EMPTY(get_panid)
ZB_RPC_WRAP_EMPTY(get_channel)
ZB_RPC_WRAP_REQ(open_network)
ZB_RPC_WRAP_EMPTY(wait_annce)
ZB_RPC_WRAP_REQ(find_sensor)
ZB_RPC_WRAP_REQ(read_basic)
ZB_RPC_WRAP_REQ(bind_sensor)
ZB_RPC_WRAP_REQ(subscribe_sensor)
ZB_RPC_WRAP_REQ(config_report)
ZB_RPC_WRAP_REQ(read_temp)
ZB_RPC_WRAP_EMPTY(get_last_temp)

#undef ZB_RPC_WRAP_EMPTY
#undef ZB_RPC_WRAP_REQ

} // namespace api

inline constexpr auto k_sim_catalog = k_catalog_text<^^::sim>;

} // namespace zb_rpc
