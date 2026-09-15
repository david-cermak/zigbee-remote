#pragma once

#include "zb_rpc/types.hpp"

#include <cstdint>

namespace zb_rpc {

template <api_id Id, typename Req, typename Resp>
struct rpc_desc {
    static constexpr api_id id = Id;
    using req_type = Req;
    using resp_type = Resp;
};

namespace desc {
using get_short_addr = rpc_desc<api_id::GET_SHORT_ADDR, void, uint16_t>;
using get_panid = rpc_desc<api_id::GET_PANID, void, uint16_t>;
using get_channel = rpc_desc<api_id::GET_CHANNEL, void, uint8_t>;
using open_network = rpc_desc<api_id::OPEN_NETWORK, uint8_t, int32_t>;
using wait_annce = rpc_desc<api_id::WAIT_ANNCE, void, uint16_t>;
using find_sensor = rpc_desc<api_id::FIND_SENSOR, uint16_t, zb_addr_t>;
using read_basic = rpc_desc<api_id::READ_BASIC, zb_addr_t, zb_basic_t>;
using bind_sensor = rpc_desc<api_id::BIND_SENSOR, zb_addr_t, int32_t>;
using subscribe_sensor = rpc_desc<api_id::SUBSCRIBE_SENSOR, zb_addr_t, int32_t>;
using config_report = rpc_desc<api_id::CONFIG_REPORT, zb_addr_t, int32_t>;
using read_temp = rpc_desc<api_id::READ_TEMP, zb_addr_t, zb_temp_t>;
using get_last_temp = rpc_desc<api_id::GET_LAST_TEMP, void, int16_t>;
} // namespace desc

} // namespace zb_rpc
