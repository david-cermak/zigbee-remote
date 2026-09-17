#include "sim_zigbee.hpp"

#include "zb_rpc/log.hpp"

#include <cstring>

namespace sim {
namespace {

constexpr const char *k_tag = "sim";

uint16_t g_short_addr = 0x0000;
uint16_t g_panid = 0x4c19;
uint8_t g_channel = 13;
uint16_t g_sensor_short = 0x1956;
uint8_t g_sensor_ep = 0x0a;
int16_t g_temp = 1300; // 13.00 C, hundredths
bool g_permit_join = false;
bool g_joined = false;

void bump_temp()
{
    if (g_temp < 1800) {
        g_temp += 100;
    }
}

} // namespace

zb_rpc::U16Resp get_short_addr(zb_rpc::EmptyReq)
{
    ZB_RPC_LOG(k_tag, "ezb_nwk_get_short_address() -> 0x%04x", g_short_addr);
    return {.value = zb_rpc::to_le16(g_short_addr)};
}

zb_rpc::U16Resp get_panid(zb_rpc::EmptyReq)
{
    ZB_RPC_LOG(k_tag, "ezb_nwk_get_panid() -> 0x%04x", g_panid);
    return {.value = zb_rpc::to_le16(g_panid)};
}

zb_rpc::U8Resp get_channel(zb_rpc::EmptyReq)
{
    ZB_RPC_LOG(k_tag, "ezb_nwk_get_current_channel() -> %u", g_channel);
    return {.value = g_channel};
}

zb_rpc::I32Resp open_network(zb_rpc::OpenNetworkReq request)
{
    g_permit_join = request.duration != 0;
    ZB_RPC_LOG(k_tag, "ezb_bdb_open_network(%u)", request.duration);
    return {.value = zb_rpc::to_le32_s(0)};
}

zb_rpc::U16Resp wait_annce(zb_rpc::EmptyReq)
{
    g_joined = true;
    ZB_RPC_LOG(k_tag, "EZB_ZDO_SIGNAL_DEVICE_ANNCE short=0x%04x", g_sensor_short);
    return {.value = zb_rpc::to_le16(g_sensor_short)};
}

zb_rpc::zb_addr_t find_sensor(zb_rpc::FindSensorReq request)
{
    const uint16_t short_addr = zb_rpc::from_le16(request.short_addr);
    zb_rpc::zb_addr_t out{};
    ZB_RPC_LOG(k_tag, "ezb_zdo_match_desc_req(temp measurement) on 0x%04x", short_addr);
    if (!g_joined || short_addr != g_sensor_short) {
        out.err = zb_rpc::to_le32_s(-1);
        return out;
    }
    out.err = zb_rpc::to_le32_s(0);
    out.short_addr = zb_rpc::to_le16(g_sensor_short);
    out.ep = g_sensor_ep;
    return out;
}

zb_rpc::zb_basic_t read_basic(zb_rpc::zb_addr_t addr)
{
    zb_rpc::zb_basic_t out{};
    const uint16_t short_addr = zb_rpc::from_le16(addr.short_addr);
    ZB_RPC_LOG(k_tag, "ezb_zcl_read_attr_cmd_req BASIC manuf/model (0x%04x, 0x%02x)", short_addr,
               addr.ep);
    if (short_addr != g_sensor_short) {
        out.err = zb_rpc::to_le32_s(-1);
        return out;
    }
    out.err = zb_rpc::to_le32_s(0);
    std::strncpy(out.manufacturer, "ESPRESSIF", sizeof(out.manufacturer) - 1);
    std::strncpy(out.model, "esp32h2", sizeof(out.model) - 1);
    return out;
}

zb_rpc::I32Resp bind_sensor(zb_rpc::zb_addr_t addr)
{
    const uint16_t short_addr = zb_rpc::from_le16(addr.short_addr);
    ZB_RPC_LOG(k_tag, "ezb_zdo_bind_req sensor(0x%04x, 0x%02x) -> local", short_addr, addr.ep);
    return {.value = zb_rpc::to_le32_s(short_addr == g_sensor_short ? 0 : -1)};
}

zb_rpc::I32Resp subscribe_sensor(zb_rpc::zb_addr_t addr)
{
    const uint16_t short_addr = zb_rpc::from_le16(addr.short_addr);
    ZB_RPC_LOG(k_tag, "ezb_zdo_bind_req local -> sensor(0x%04x, 0x%02x)", short_addr, addr.ep);
    return {.value = zb_rpc::to_le32_s(short_addr == g_sensor_short ? 0 : -1)};
}

zb_rpc::I32Resp config_report(zb_rpc::zb_addr_t addr)
{
    const uint16_t short_addr = zb_rpc::from_le16(addr.short_addr);
    ZB_RPC_LOG(k_tag, "ezb_zcl_config_report_cmd_req measured_value (0x%04x, 0x%02x)", short_addr,
               addr.ep);
    return {.value = zb_rpc::to_le32_s(short_addr == g_sensor_short ? 0 : -1)};
}

zb_rpc::zb_temp_t read_temp(zb_rpc::zb_addr_t addr)
{
    zb_rpc::zb_temp_t out{};
    const uint16_t short_addr = zb_rpc::from_le16(addr.short_addr);
    ZB_RPC_LOG(k_tag, "ezb_zcl_read_attr_cmd_req TEMPERATURE_MEASUREMENT (0x%04x, 0x%02x)",
               short_addr, addr.ep);
    if (short_addr != g_sensor_short) {
        out.err = zb_rpc::to_le32_s(-1);
        return out;
    }
    bump_temp();
    out.err = zb_rpc::to_le32_s(0);
    out.measured = zb_rpc::to_le16_s(g_temp);
    out.min_measured = zb_rpc::to_le16_s(-1000);
    out.max_measured = zb_rpc::to_le16_s(8000);
    out.tolerance = zb_rpc::to_le16_s(0);
    return out;
}

zb_rpc::I16Resp get_last_temp(zb_rpc::EmptyReq)
{
    bump_temp();
    ZB_RPC_LOG(k_tag, "last measured_value -> %d (%.2f C)", g_temp, g_temp / 100.0);
    return {.value = zb_rpc::to_le16_s(g_temp)};
}

} // namespace sim
