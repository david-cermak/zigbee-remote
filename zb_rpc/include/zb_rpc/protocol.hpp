#pragma once

#include <cstddef>
#include <cstdint>

namespace zb_rpc {

inline constexpr uint32_t k_magic = 0x32435052U; // "RPC2" little-endian
inline constexpr uint32_t k_max_payload = 64;
inline constexpr uint16_t k_default_port = 3333;
inline constexpr const char *k_default_host = "127.0.0.1";

struct le16 {
    uint8_t bytes[2];
};

struct le32 {
    uint8_t bytes[4];
};

constexpr le16 to_le16(uint16_t value)
{
    return {{static_cast<uint8_t>(value), static_cast<uint8_t>(value >> 8)}};
}

constexpr uint16_t from_le16(le16 value)
{
    return static_cast<uint16_t>(value.bytes[0]) |
           (static_cast<uint16_t>(value.bytes[1]) << 8);
}

constexpr le16 to_le16_s(int16_t value)
{
    return to_le16(static_cast<uint16_t>(value));
}

constexpr int16_t from_le16_s(le16 value)
{
    return static_cast<int16_t>(from_le16(value));
}

constexpr le32 to_le32(uint32_t value)
{
    return {{static_cast<uint8_t>(value), static_cast<uint8_t>(value >> 8),
             static_cast<uint8_t>(value >> 16), static_cast<uint8_t>(value >> 24)}};
}

constexpr uint32_t from_le32(le32 value)
{
    return static_cast<uint32_t>(value.bytes[0]) |
           (static_cast<uint32_t>(value.bytes[1]) << 8) |
           (static_cast<uint32_t>(value.bytes[2]) << 16) |
           (static_cast<uint32_t>(value.bytes[3]) << 24);
}

constexpr le32 to_le32_s(int32_t value)
{
    return to_le32(static_cast<uint32_t>(value));
}

constexpr int32_t from_le32_s(le32 value)
{
    return static_cast<int32_t>(from_le32(value));
}

enum class api_id : uint32_t {
    error = 0,
    get_short_addr = 1,
    get_panid = 2,
    get_channel = 3,
    open_network = 4,
    wait_annce = 5,
    find_sensor = 6,
    read_basic = 7,
    bind_sensor = 8,
    subscribe_sensor = 9,
    config_report = 10,
    read_temp = 11,
    get_last_temp = 12,
};

enum class error_code : uint32_t {
    none = 0,
    bad_magic = 1,
    oversized_payload = 2,
    unknown_api = 3,
    payload_mismatch = 4,
};

struct WireHeader {
    le32 magic;
    le32 id;
    le32 size;
};

struct FrameHeader {
    api_id id;
    uint32_t size;
};

struct ErrorResp {
    le32 code;
} __attribute__((packed));

struct EmptyReq {};

struct U8Resp {
    uint8_t value;
} __attribute__((packed));

struct U16Resp {
    le16 value;
} __attribute__((packed));

struct I16Resp {
    le16 value;
} __attribute__((packed));

struct I32Resp {
    le32 value;
} __attribute__((packed));

struct OpenNetworkReq {
    uint8_t duration;
} __attribute__((packed));

struct FindSensorReq {
    le16 short_addr;
} __attribute__((packed));

struct zb_addr_t {
    le32 err;
    le16 short_addr;
    uint8_t ep;
} __attribute__((packed));

struct zb_basic_t {
    le32 err;
    char manufacturer[16];
    char model[16];
} __attribute__((packed));

struct zb_temp_t {
    le32 err;
    le16 measured;
    le16 min_measured;
    le16 max_measured;
    le16 tolerance;
} __attribute__((packed));

template <api_id Id, typename Req, typename Resp>
struct rpc_desc {
    static constexpr api_id id = Id;
    using req_type = Req;
    using resp_type = Resp;
};

static_assert(sizeof(WireHeader) == 12);
static_assert(sizeof(ErrorResp) == 4);
static_assert(sizeof(U16Resp) == 2);
static_assert(sizeof(OpenNetworkReq) == 1);
static_assert(sizeof(FindSensorReq) == 2);
static_assert(sizeof(zb_addr_t) == 7);
static_assert(sizeof(zb_basic_t) == 36);
static_assert(sizeof(zb_temp_t) == 12);
static_assert(sizeof(zb_basic_t) <= k_max_payload);

} // namespace zb_rpc
