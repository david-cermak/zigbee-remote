#pragma once

#include "zb_rpc/types.hpp"

namespace sim {

uint16_t get_short_addr();
uint16_t get_panid();
uint8_t get_channel();
int32_t open_network(uint8_t duration);
uint16_t wait_annce();
zb_rpc::zb_addr_t find_sensor(uint16_t short_addr);
zb_rpc::zb_basic_t read_basic(zb_rpc::zb_addr_t addr);
int32_t bind_sensor(zb_rpc::zb_addr_t addr);
int32_t subscribe_sensor(zb_rpc::zb_addr_t addr);
int32_t config_report(zb_rpc::zb_addr_t addr);
zb_rpc::zb_temp_t read_temp(zb_rpc::zb_addr_t addr);
int16_t get_last_temp();

} // namespace sim
