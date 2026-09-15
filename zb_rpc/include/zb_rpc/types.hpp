#pragma once

#include <cstdint>

namespace zb_rpc {

inline constexpr const char *k_default_host = "127.0.0.1";
inline constexpr uint16_t k_default_port = 3333;

enum class api_id : uint32_t {
    ERROR = 0,
    UNDEF,
    GET_SHORT_ADDR,
    GET_PANID,
    GET_CHANNEL,
    OPEN_NETWORK,
    WAIT_ANNCE,
    FIND_SENSOR,
    READ_BASIC,
    BIND_SENSOR,
    SUBSCRIBE_SENSOR,
    CONFIG_REPORT,
    READ_TEMP,
    GET_LAST_TEMP,
};

struct RpcHeader {
    api_id id;
    uint32_t size;
} __attribute__((packed));

struct zb_addr_t {
    int32_t err;
    uint16_t short_addr;
    uint8_t ep;
} __attribute__((packed));

struct zb_basic_t {
    int32_t err;
    char manufacturer[16];
    char model[16];
} __attribute__((packed));

struct zb_temp_t {
    int32_t err;
    int16_t measured;
    int16_t min_measured;
    int16_t max_measured;
    int16_t tolerance;
} __attribute__((packed));

} // namespace zb_rpc
