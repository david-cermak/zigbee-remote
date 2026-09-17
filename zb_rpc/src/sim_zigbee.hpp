#pragma once

#include "zb_rpc/protocol.hpp"

namespace sim {

zb_rpc::U16Resp get_short_addr(zb_rpc::EmptyReq);
zb_rpc::U16Resp get_panid(zb_rpc::EmptyReq);
zb_rpc::U8Resp get_channel(zb_rpc::EmptyReq);
zb_rpc::I32Resp open_network(zb_rpc::OpenNetworkReq request);
zb_rpc::U16Resp wait_annce(zb_rpc::EmptyReq);
zb_rpc::zb_addr_t find_sensor(zb_rpc::FindSensorReq request);
zb_rpc::zb_basic_t read_basic(zb_rpc::zb_addr_t addr);
zb_rpc::I32Resp bind_sensor(zb_rpc::zb_addr_t addr);
zb_rpc::I32Resp subscribe_sensor(zb_rpc::zb_addr_t addr);
zb_rpc::I32Resp config_report(zb_rpc::zb_addr_t addr);
zb_rpc::zb_temp_t read_temp(zb_rpc::zb_addr_t addr);
zb_rpc::I16Resp get_last_temp(zb_rpc::EmptyReq);

} // namespace sim
