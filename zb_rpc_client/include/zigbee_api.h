#pragma once

/*
 * Client-side implementation of the firmware Zigbee API.
 *
 * The names, argument types and return types deliberately mirror
 * zb_rpc_server/main/zigbee_api.h.  Application code written against this
 * header (for example thermostat_app.hpp) uses exactly the same calls as the
 * coordinator firmware; on the firmware side those functions are provided by
 * zb_rpc_server/main/zigbee_api.h and backed by the real ESP Zigbee stack.
 *
 * This header is header-only so the client also builds as a single
 * translation unit for Compiler Explorer.  Use
 * zigbee_rpc::attach()/detach() around the RPC connection.
 */

#include "zb_rpc/engine.hpp"

#include <cstdint>
#include <cstring>

typedef struct {
    int32_t err;
    uint16_t short_addr;
    uint8_t ep;
} zigbee_addr_t;

typedef struct {
    int32_t err;
    char manufacturer[16];
    char model[16];
} zigbee_basic_t;

typedef struct {
    int32_t err;
    int16_t measured;
    int16_t min_measured;
    int16_t max_measured;
    int16_t tolerance;
} zigbee_temp_t;

namespace zigbee_rpc {

/* Returned by zigbee_get_sensor_addr() when the RPC itself failed (for
 * example because the server predates that API).  A real sensor never has
 * this short address; 0 means "no sensor cached yet". */
inline constexpr uint16_t k_invalid_short_addr = 0xFFFF;

inline int g_fd = -1;

inline bool attach(int fd)
{
    g_fd = fd;
    return fd >= 0;
}

inline void detach()
{
    g_fd = -1;
}

inline bool attached()
{
    return g_fd >= 0;
}

inline zb_rpc::Engine engine()
{
    return zb_rpc::Engine(g_fd);
}

inline zb_rpc::zb_addr_t to_wire(uint16_t short_addr, uint8_t ep)
{
    return {
        .err = zb_rpc::to_le32_s(0),
        .short_addr = zb_rpc::to_le16(short_addr),
        .ep = ep,
    };
}

} // namespace zigbee_rpc

/* ------------------------------------------------------------------------- */
/* Same surface as zb_rpc_server/main/zigbee_api.h                           */
/* ------------------------------------------------------------------------- */

inline uint16_t zigbee_get_short_addr(void)
{
    if (!zigbee_rpc::attached()) {
        return 0;
    }
    zb_rpc::U16Resp response{};
    zb_rpc::Engine engine = zigbee_rpc::engine();
    if (!zb_rpc::call<zb_rpc::desc::get_short_addr>(engine, response)) {
        return 0;
    }
    return zb_rpc::from_le16(response.value);
}

inline uint16_t zigbee_get_panid(void)
{
    if (!zigbee_rpc::attached()) {
        return 0;
    }
    zb_rpc::U16Resp response{};
    zb_rpc::Engine engine = zigbee_rpc::engine();
    if (!zb_rpc::call<zb_rpc::desc::get_panid>(engine, response)) {
        return 0;
    }
    return zb_rpc::from_le16(response.value);
}

inline uint8_t zigbee_get_channel(void)
{
    if (!zigbee_rpc::attached()) {
        return 0;
    }
    zb_rpc::U8Resp response{};
    zb_rpc::Engine engine = zigbee_rpc::engine();
    if (!zb_rpc::call<zb_rpc::desc::get_channel>(engine, response)) {
        return 0;
    }
    return response.value;
}

inline uint16_t zigbee_get_sensor_addr(void)
{
    if (!zigbee_rpc::attached()) {
        return zigbee_rpc::k_invalid_short_addr;
    }
    zb_rpc::U16Resp response{};
    zb_rpc::Engine engine = zigbee_rpc::engine();
    if (!zb_rpc::call<zb_rpc::desc::get_sensor_addr>(engine, response)) {
        return zigbee_rpc::k_invalid_short_addr;
    }
    return zb_rpc::from_le16(response.value);
}

inline int32_t zigbee_open_network(uint8_t duration)
{
    if (!zigbee_rpc::attached()) {
        return -1;
    }
    zb_rpc::I32Resp response{};
    zb_rpc::Engine engine = zigbee_rpc::engine();
    if (!zb_rpc::call<zb_rpc::desc::open_network>(
            engine, zb_rpc::OpenNetworkReq{.duration = duration}, response)) {
        return -1;
    }
    return zb_rpc::from_le32_s(response.value);
}

/* The current RPC server applies its own fixed wait window.  The timeout
 * argument is kept to match the firmware signature. */
inline uint16_t zigbee_wait_annce(uint32_t timeout_ms)
{
    (void)timeout_ms;
    if (!zigbee_rpc::attached()) {
        return 0;
    }
    zb_rpc::U16Resp response{};
    zb_rpc::Engine engine = zigbee_rpc::engine();
    if (!zb_rpc::call<zb_rpc::desc::wait_annce>(engine, response)) {
        return 0;
    }
    return zb_rpc::from_le16(response.value);
}

/* timeout_ms is kept to match the firmware signature; the server currently
 * applies its own request timeout. */
inline zigbee_addr_t zigbee_find_sensor(uint16_t short_addr, uint32_t timeout_ms)
{
    (void)timeout_ms;
    zigbee_addr_t out{};
    out.err = -1;
    if (!zigbee_rpc::attached()) {
        return out;
    }

    zb_rpc::zb_addr_t response{};
    zb_rpc::Engine engine = zigbee_rpc::engine();
    if (!zb_rpc::call<zb_rpc::desc::find_sensor>(
            engine, zb_rpc::FindSensorReq{.short_addr = zb_rpc::to_le16(short_addr)}, response)) {
        return out;
    }

    out.err = zb_rpc::from_le32_s(response.err);
    out.short_addr = zb_rpc::from_le16(response.short_addr);
    out.ep = response.ep;
    return out;
}

inline zigbee_basic_t zigbee_read_basic(uint16_t short_addr, uint8_t ep, uint32_t timeout_ms)
{
    (void)timeout_ms;
    zigbee_basic_t out{};
    out.err = -1;
    if (!zigbee_rpc::attached()) {
        return out;
    }

    zb_rpc::zb_basic_t response{};
    zb_rpc::Engine engine = zigbee_rpc::engine();
    if (!zb_rpc::call<zb_rpc::desc::read_basic>(engine, zigbee_rpc::to_wire(short_addr, ep),
                                               response)) {
        return out;
    }

    out.err = zb_rpc::from_le32_s(response.err);
    std::memcpy(out.manufacturer, response.manufacturer, sizeof(out.manufacturer));
    std::memcpy(out.model, response.model, sizeof(out.model));
    return out;
}

inline int32_t zigbee_bind_sensor(uint16_t short_addr, uint8_t ep, uint32_t timeout_ms)
{
    (void)timeout_ms;
    if (!zigbee_rpc::attached()) {
        return -1;
    }
    zb_rpc::I32Resp response{};
    zb_rpc::Engine engine = zigbee_rpc::engine();
    if (!zb_rpc::call<zb_rpc::desc::bind_sensor>(engine, zigbee_rpc::to_wire(short_addr, ep),
                                                response)) {
        return -1;
    }
    return zb_rpc::from_le32_s(response.value);
}

inline int32_t zigbee_subscribe_sensor(uint16_t short_addr, uint8_t ep, uint32_t timeout_ms)
{
    (void)timeout_ms;
    if (!zigbee_rpc::attached()) {
        return -1;
    }
    zb_rpc::I32Resp response{};
    zb_rpc::Engine engine = zigbee_rpc::engine();
    if (!zb_rpc::call<zb_rpc::desc::subscribe_sensor>(engine,
                                                     zigbee_rpc::to_wire(short_addr, ep), response)) {
        return -1;
    }
    return zb_rpc::from_le32_s(response.value);
}

inline int32_t zigbee_config_report(uint16_t short_addr, uint8_t ep, uint32_t timeout_ms)
{
    (void)timeout_ms;
    if (!zigbee_rpc::attached()) {
        return -1;
    }
    zb_rpc::I32Resp response{};
    zb_rpc::Engine engine = zigbee_rpc::engine();
    if (!zb_rpc::call<zb_rpc::desc::config_report>(engine, zigbee_rpc::to_wire(short_addr, ep),
                                                  response)) {
        return -1;
    }
    return zb_rpc::from_le32_s(response.value);
}

inline zigbee_temp_t zigbee_read_temp(uint16_t short_addr, uint8_t ep, uint32_t timeout_ms)
{
    (void)timeout_ms;
    zigbee_temp_t out{};
    out.err = -1;
    if (!zigbee_rpc::attached()) {
        return out;
    }

    zb_rpc::zb_temp_t response{};
    zb_rpc::Engine engine = zigbee_rpc::engine();
    if (!zb_rpc::call<zb_rpc::desc::read_temp>(engine, zigbee_rpc::to_wire(short_addr, ep),
                                              response)) {
        return out;
    }

    out.err = zb_rpc::from_le32_s(response.err);
    out.measured = zb_rpc::from_le16_s(response.measured);
    out.min_measured = zb_rpc::from_le16_s(response.min_measured);
    out.max_measured = zb_rpc::from_le16_s(response.max_measured);
    out.tolerance = zb_rpc::from_le16_s(response.tolerance);
    return out;
}

inline int16_t zigbee_get_last_temp(void)
{
    if (!zigbee_rpc::attached()) {
        return 0;
    }
    zb_rpc::I16Resp response{};
    zb_rpc::Engine engine = zigbee_rpc::engine();
    if (!zb_rpc::call<zb_rpc::desc::get_last_temp>(engine, response)) {
        return 0;
    }
    return zb_rpc::from_le16_s(response.value);
}
